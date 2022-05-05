#pragma once

/*************************************************************************
 * Copyright © 2022 Hudyaev Alexy <hudyaev.alexy@gmail.com>
 * This file is part of http_parser.
 * Distributed under the MIT License.
 * See accompanying file LICENSE (at the root of this repository)
 *************************************************************************/

#include <variant>
#include "message.hpp"
#include "utils/headers_parser.hpp"
#include "utils/http1_head_parsers.hpp"
#include "utils/chunked_body_parser.hpp"

namespace http_parser {


template<typename Head, typename DataContainer>
struct http1_acceptor_traits {
	using data_view = basic_position_string_view<DataContainer>;

	virtual ~http1_acceptor_traits () noexcept =default ;

	virtual DataContainer create_data_container() =0 ;
	virtual typename Head::headers_container create_headers_container() =0 ;

	virtual void on_head(const Head& head) {}
	virtual void on_request(const Head& head, const data_view& body) {}
};

template<
        template<class> class Container,
        typename DataContainer,
        std::size_t max_body_size = 4 * 1024,
        std::size_t max_head_size = 1 * 1024
        >
class http1_req_acceptor final {
public:
	using message_t = http1_message<Container, req_head_message, DataContainer>;
	using traits_type = http1_acceptor_traits<message_t, DataContainer>;
private:
	enum class state_t { wait, head, headers, body, finish };

	state_t cur_state = state_t::wait;
	traits_type* traits;
	DataContainer data, body_data;
	basic_position_string_view<DataContainer> head_view;
	basic_position_string_view<DataContainer> body_view;

	message_t result_request;

	headers_parser<DataContainer, Container> parser_hrds;

	void parse_head() {
		assert(traits);
		http1_request_head_parser prs(head_view);
		auto st = prs();
//		if(st == http1_head_state::wait) return;
		if(st != http1_head_state::http1_req)
			throw std::runtime_error("request was await");
		cur_state = state_t::head;
		result_request.head() = prs.req_msg();
		parser_hrds.skip_first_bytes(prs.end_position());
	}
	void parse_headers() {
		parser_hrds();
		require_head_limit(parser_hrds.finish_position());
		if(!parser_hrds.is_finished()) return;
		result_request.headers() = parser_hrds.extract_result();
		cur_state = state_t::headers;
	}
	void headers_ready() {
		const bool body_exists = result_request.headers().body_exists();
		move_to_body_container(parser_hrds.finish_position());
		if(body_exists) traits->on_head(result_request);
		else traits->on_request( result_request, body_view );
		cur_state = body_exists ? state_t::body : state_t::finish;
	}
	void parse_body() {
		body_view.advance_to_end();
		if(auto size=result_request.headers().content_size(); size) {
			if(*size <= body_view.size()) {
				body_view.resize(*size);
				traits->on_request(result_request, body_view);
				cur_state = state_t::finish;
			}
		} else if(result_request.headers().is_chunked()) {
			chunked_body_parser prs(body_view);
			while(prs()) {
				if(prs.ready()) traits->on_request(result_request, prs.result());
				else if(prs.error()) traits->on_request(result_request, head_view);
			}
			clean_body(prs.end_pos());
			if(prs.finish()) cur_state = state_t::finish;
		}
	}

	void clean_body(std::size_t actual_pos)
	{
		auto body = traits->create_data_container();
		for(std::size_t i=actual_pos;i<body_data.size();++i)
			body.push_back(body_data[i]);
		body_data = std::move(body);
		body_view.reset();
	}

	void move_to_body_container(std::size_t start)
	{
		for(std::size_t i=start;i<data.size();++i)
			body_data.push_back(data[i]);
		body_view.advance_to_end();
	}

	void require_limits(std::size_t size_to_add)
	{
		using namespace std::literals;
		if(cur_state == state_t::body) {
			if(max_body_size < body_data.size() + size_to_add)
				throw std::out_of_range("maximum body size reached"s);
		} else {
			require_head_limit(data.size() + size_to_add);
		}
	}

	void require_head_limit(std::size_t size_now) const
	{
		using namespace std::literals;
		if(max_head_size < size_now)
			    throw std::out_of_range("maximum head size reached"s);
	}
public:
	http1_req_acceptor(traits_type* traits)
	    : traits(traits)
	    , data(traits->create_data_container())
	    , body_data(traits->create_data_container())
	    , head_view(&data, 0, 0)
	    , body_view(&body_data, 0, 0)
	    , result_request(&data)
	    , parser_hrds(&data, traits->create_headers_container())
	{}

	template<typename S>
	void operator()(S&& buf) {
		assert( cur_state <= state_t::finish );
		if(!data.empty()) require_limits( buf.size() );
		if(cur_state == state_t::body)
			for(auto& s:buf) body_data.push_back((typename DataContainer::value_type) s);
		else for(auto& s:buf) data.push_back((typename DataContainer::value_type) s);
		if(cur_state == state_t::wait) parse_head();
		if(cur_state == state_t::head) parse_headers();
		if(cur_state == state_t::headers) headers_ready();
		if(cur_state == state_t::body) parse_body();
	}
};


template<template<class> class Container, typename DataContainer>
struct acceptor_traits {
	virtual ~acceptor_traits() noexcept =default ;
	virtual void on_http1_request(
	        const http1_message<Container, req_head_message, DataContainer>& header,
	        const DataContainer& body) {}
	virtual void on_http1_response() {}
	virtual DataContainer create_data_container() =0 ;
};

template<
        template<class> class Container,
        typename DataContainer
        >
class acceptor final {
public:
	using traits_type = acceptor_traits<Container, DataContainer>;
	using request_head = req_head_message<DataContainer>;
	using response_head = resp_head_message<DataContainer>;

	using http1_message_t = http1_message<Container, req_head_message, DataContainer>;
private:
	traits_type* traits;
	enum class state_t { start, wait, http1, http2, websocket, headers, body };

	state_t cur_state = state_t::start;
	DataContainer data, body;
	basic_position_string_view<DataContainer> parsing;

	std::variant<request_head, response_head> result_head;
	http1_message_t result_request;

	void create_container()
	{
		if(cur_state != state_t::start) return;
		data = traits->create_data_container();
		cur_state = state_t::wait;
	}

	void determine()
	{
		assert(traits);
		basic_position_string_view view(&data);
		http1_request_head_parser prs(view);
		auto st = prs();
		if(st == http1_head_state::http1_resp) {
			cur_state = state_t::http1;
			result_head = prs.resp_msg();
			traits->on_http1_response();
			cur_state = state_t::headers;
			parsing = parsing.substr(prs.end_position());
		}
		else if(st == http1_head_state::http1_req) {
			cur_state = state_t::http1;
			result_request.head() = prs.req_msg();
			traits->on_http1_request(result_request, body);
			cur_state = state_t::headers;
			parsing = parsing.substr(prs.end_position());
		}
	}

	void parse_headers()
	{
	}
public:
	acceptor(traits_type* traits)
	    : traits(traits)
	    , data(traits->create_data_container())
	    , body(traits->create_data_container())
	    , parsing(&data)
	    , result_head(request_head{&data})
	    , result_request(&body)
	{}

	template<typename S>
	void operator()(S&& buf){
		create_container();
		for(auto& s:buf) { data.push_back((typename DataContainer::value_type) s); }
		if(cur_state == state_t::wait) determine();
	}
};

} // namespace http_parser
