#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE server

#include <boost/test/unit_test.hpp>
#include <http_parser.hpp>
#include <http_parser/server.hpp>

using namespace std::literals;


BOOST_AUTO_TEST_SUITE(core)
BOOST_AUTO_TEST_SUITE(server)

namespace test_details {
using buffer_type = std::array<char, 10>;
} // namespace test_details

struct tcp_socket : http_parser::tcp_socket<test_details::buffer_type> {
	void receive(std::string_view data) {
		;
	}
};

struct test_traits_factory {
	template<typename T>
	using container_t = std::pmr::vector<T>;
	using data_type = std::pmr::vector<std::byte>;
	static constexpr std::size_t max_body_size = 4 * 1024;
	static constexpr std::size_t max_head_size = 1 * 1024;

	using request_parser_t = http_parser::http1_req_parser<container_t, data_type, max_body_size, max_head_size>;
	using response_parser_t = http_parser::http1_resp_parser<container_t, data_type, max_body_size, max_head_size>;
};


using server_t = http_parser::server<test_details::buffer_type, test_traits_factory>;

struct test_hndl_desc : http_parser::handler_descriptor {
	std::string host, path, endpoint;
	std::uint32_t port;
	std::shared_ptr<server_t::handler_t> hndl;

	std::function<bool (const tcp_socket& sock)> check_sock_fnc;

	test_hndl_desc(std::string h, std::string p, std::string e, std::uint32_t port, std::shared_ptr<server_t::handler_t> hndl)
	    : host(std::move(h))
	    , path(std::move(p))
	    , endpoint(std::move(e))
	    , port(port)
	    , hndl(std::move(hndl))
	{}
};

BOOST_AUTO_TEST_CASE(example)
{
	server_t srv;
	tcp_socket sock1;

	auto* hndl = srv.reg( std::make_unique<test_hndl_desc>("host", "/path", "localhost", 8083, nullptr) );
	BOOST_TEST(srv.active_count() == 0);
	srv(&sock1); // incoming connection

	std::size_t hndl_cnt = 0;
//	hndl->hndl = [&hndl_cnt](server_t::message_t req, server_t::writer_t write){
//		++hndl_cnt;
//		BOOST_TEST( req.header("Host").value() == "host" );
//		BOOST_TEST( req.uri().uri() == "/path" );
//		BOOST_TEST( req.next_body() == "body" );
//		BOOST_TEST( req.next_body() == "" );

//		response_t resp;
//		resp.resp(200, "OK").header("test","v1").make_chunked();
//		write(resp.body("body1"));
//		write(resp.body("body2"));
//		write(resp.body(""));
//	};


	BOOST_TEST(srv.active_count() == 1);
	BOOST_TEST(hndl_cnt == 0);
	sock1.receive("GET /path HTTP/1.1\r\nHost:host\r\nTransfer-Encoding:chunked\r\n\r\n4\r\nbody0\r\n\r\n"sv);
	BOOST_TEST(srv.active_count() == 1);
	BOOST_TEST(hndl_cnt == 1);

//	srv.reg( [&unmatched_hndl_cnt](server_t::request_t req,server_t::writer_t write) {
//		++unmatched_hndl_cnt;
//		BOOST_TEST( req.header("Test").value() == "unmatched" );
//		write("ok");
//	});

//	io.write();
//	BOOST_TEST(hndl_cnt == 1);
//	BOOST_TEST(unmatched_hndl_cnt == 0);
//	BOOST_TEST(io.last_write == "HTTP/1.1 200 OK\r\ntest: v1\r\n"
//	                            "Transfer-Encoding: chunked\r\n\r\n5\r\nbody15\r\nbody20\r\n\r\n");
//	BOOST_TEST(io.resp_count == 1);

//	io.write("GET /path HTTP/1.1\r\nHost:nohost\r\n\r\n");
//	BOOST_TEST(hndl_cnt == 1);
//	BOOST_TEST(unmatched_hndl_cnt == 1);
//	BOOST_TEST(io.last_write == "ok");
//	BOOST_TEST(io.resp_count == 2);
}

BOOST_AUTO_TEST_SUITE_END() // server
BOOST_AUTO_TEST_SUITE_END() // core
