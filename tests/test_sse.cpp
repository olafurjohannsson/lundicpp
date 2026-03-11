#include <lundi/sse.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace lundi;
using Catch::Matchers::ContainsSubstring;

TEST_CASE( "sse: initial headers contain correct content-type", "[sse]" ) {
    auto hdrs = detail::sse_initial_headers();
    CHECK_THAT( hdrs, ContainsSubstring( "HTTP/1.1 200 OK\r\n" ) );
    CHECK_THAT( hdrs, ContainsSubstring( "Content-Type: text/event-stream\r\n" ) );
}

TEST_CASE( "sse: initial headers contain no-cache", "[sse]" ) {
    auto hdrs = detail::sse_initial_headers();
    CHECK_THAT( hdrs, ContainsSubstring( "Cache-Control: no-cache\r\n" ) );
}

TEST_CASE( "sse: initial headers contain keep-alive", "[sse]" ) {
    auto hdrs = detail::sse_initial_headers();
    CHECK_THAT( hdrs, ContainsSubstring( "Connection: keep-alive\r\n" ) );
}

TEST_CASE( "sse: initial headers contain CORS", "[sse]" ) {
    auto hdrs = detail::sse_initial_headers();
    CHECK_THAT( hdrs, ContainsSubstring( "Access-Control-Allow-Origin: *\r\n" ) );
}

TEST_CASE( "sse: initial headers contain Server", "[sse]" ) {
    auto hdrs = detail::sse_initial_headers();
    CHECK_THAT( hdrs, ContainsSubstring( "Server: Lundi\r\n" ) );
}

TEST_CASE( "sse: initial headers contain Date", "[sse]" ) {
    auto hdrs = detail::sse_initial_headers();
    CHECK_THAT( hdrs, ContainsSubstring( "Date: " ) );
}

TEST_CASE( "sse: initial headers end with blank line", "[sse]" ) {
    auto hdrs = detail::sse_initial_headers();
    CHECK( hdrs.substr( hdrs.size() - 4 ) == "\r\n\r\n" );
}