#include <lundi/core.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace lundi;
using Catch::Matchers::ContainsSubstring;

TEST_CASE( "response: text factory sets content-type", "[response]" ) {
    auto r = response::text( "hello" );
    CHECK( r.status == 200 );
    CHECK( r.body == "hello" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Type: text/plain; charset=utf-8\r\n" ) );
}

TEST_CASE( "response: json factory sets content-type", "[response]" ) {
    auto r = response::json( R"({"ok":true})" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Type: application/json; charset=utf-8\r\n" ) );
}

TEST_CASE( "response: html factory sets content-type", "[response]" ) {
    auto r = response::html( "<h1>hi</h1>" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Type: text/html; charset=utf-8\r\n" ) );
}

TEST_CASE( "response: custom status code", "[response]" ) {
    auto r = response::text( "not found", 404 );
    CHECK( r.status == 404 );
}

TEST_CASE( "response: serialize includes status line", "[response]" ) {
    auto r = response::text( "hello" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "HTTP/1.1 200 OK\r\n" ) );
}

TEST_CASE( "response: serialize includes content-length", "[response]" ) {
    auto r = response::text( "hello" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Length: 5\r\n" ) );
}

TEST_CASE( "response: serialize defaults to keep-alive", "[response]" ) {
    auto r = response::text( "hello" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Connection: keep-alive\r\n" ) );
}

TEST_CASE( "response: serialize respects explicit Connection: close", "[response]" ) {
    auto r = response::text( "bye" );
    r.set_header( "Connection", "close" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Connection: close\r\n" ) );
    CHECK_THAT( s, !ContainsSubstring( "keep-alive" ) );
}

TEST_CASE( "response: serialize body appears after headers", "[response]" ) {
    auto r = response::text( "hello" );
    auto s = r.serialize();
    auto body_pos = s.find( "\r\n\r\nhello" );
    CHECK( body_pos != std::string::npos );
}

TEST_CASE( "response: 413 status text", "[response]" ) {
    auto r = response::text( "too big", 413 );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "413 Payload Too Large" ) );
}

TEST_CASE( "response: empty body has content-length 0", "[response]" ) {
    response r;
    r.status = 204;
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Length: 0\r\n" ) );
}