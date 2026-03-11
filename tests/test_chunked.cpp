#include <lundi/chunked.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace lundi;
using Catch::Matchers::ContainsSubstring;

TEST_CASE( "chunked: initial headers contain transfer-encoding", "[chunked]" ) {
    auto hdrs = detail::chunked_initial_headers( "text/plain" );
    CHECK_THAT( hdrs, ContainsSubstring( "Transfer-Encoding: chunked\r\n" ) );
}

TEST_CASE( "chunked: initial headers contain content-type", "[chunked]" ) {
    auto hdrs = detail::chunked_initial_headers( "application/json" );
    CHECK_THAT( hdrs, ContainsSubstring( "Content-Type: application/json\r\n" ) );
}

TEST_CASE( "chunked: initial headers contain status line", "[chunked]" ) {
    auto hdrs = detail::chunked_initial_headers( "text/html" );
    CHECK_THAT( hdrs, ContainsSubstring( "HTTP/1.1 200 OK\r\n" ) );
}

TEST_CASE( "chunked: initial headers contain Server", "[chunked]" ) {
    auto hdrs = detail::chunked_initial_headers( "text/plain" );
    CHECK_THAT( hdrs, ContainsSubstring( "Server: Lundi\r\n" ) );
}

TEST_CASE( "chunked: initial headers contain Date", "[chunked]" ) {
    auto hdrs = detail::chunked_initial_headers( "text/plain" );
    CHECK_THAT( hdrs, ContainsSubstring( "Date: " ) );
}

TEST_CASE( "chunked: initial headers do NOT contain Content-Length", "[chunked]" ) {
    auto hdrs = detail::chunked_initial_headers( "text/plain" );
    CHECK_THAT( hdrs, !ContainsSubstring( "Content-Length" ) );
}

TEST_CASE( "chunked: initial headers end with blank line", "[chunked]" ) {
    auto hdrs = detail::chunked_initial_headers( "text/plain" );
    CHECK( hdrs.substr( hdrs.size() - 4 ) == "\r\n\r\n" );
}