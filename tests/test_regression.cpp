#include <lundi/core.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace lundi;
using namespace lundi::detail;
using Catch::Matchers::ContainsSubstring;


TEST_CASE( "serialize: 200 status line exact bytes", "[regression][serialize]" ) {
    auto r = response::text( "x" );
    auto s = r.serialize();
    CHECK( s.substr( 0, 17 ) == "HTTP/1.1 200 OK\r\n" );
}

TEST_CASE( "serialize: Server header present and correct", "[regression][serialize]" ) {
    auto r = response::text( "x" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Server: Lundi\r\n" ) );
}

TEST_CASE( "serialize: Date header present and well-formed", "[regression][serialize]" ) {
    auto r = response::text( "x" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Date: " ) );
    auto date_pos = s.find( "Date: " );
    REQUIRE( date_pos != std::string::npos );
    auto date_end = s.find( "\r\n", date_pos );
    REQUIRE( date_end != std::string::npos );
    auto date_val = s.substr( date_pos + 6, date_end - date_pos - 6 );
    CHECK( date_val.size() == 29 );
    CHECK( date_val.substr( date_val.size() - 3 ) == "GMT" );
}

TEST_CASE( "serialize: Connection: keep-alive exact format", "[regression][serialize]" ) {
    auto r = response::text( "x" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Connection: keep-alive\r\n" ) );
    CHECK( s.find( "keep-alive\r\r" ) == std::string::npos );
}

TEST_CASE( "serialize: Content-Length matches body size", "[regression][serialize]" ) {
    auto r = response::text( "hello world" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Length: 11\r\n" ) );
}

TEST_CASE( "serialize: empty body has Content-Length 0", "[regression][serialize]" ) {
    response r;
    r.status = 204;
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Length: 0\r\n" ) );
}

TEST_CASE( "serialize: body appears after double CRLF", "[regression][serialize]" ) {
    auto r = response::text( "TESTBODY" );
    auto s = r.serialize();
    auto sep = s.find( "\r\n\r\n" );
    REQUIRE( sep != std::string::npos );
    auto body = s.substr( sep + 4 );
    CHECK( body == "TESTBODY" );
}

TEST_CASE( "serialize: non-200 status includes code and text", "[regression][serialize]" ) {
    auto r = response::text( "nope", 404 );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "HTTP/1.1 404 Not Found\r\n" ) );
}

TEST_CASE( "serialize: 413 status text correct", "[regression][serialize]" ) {
    auto r = response::text( "too big", 413 );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "HTTP/1.1 413 Payload Too Large\r\n" ) );
}

TEST_CASE( "serialize: 408 status text correct", "[regression][serialize]" ) {
    auto r = response::text( "timeout", 408 );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "HTTP/1.1 408 Request Timeout\r\n" ) );
}

TEST_CASE( "serialize: 500 status text correct", "[regression][serialize]" ) {
    auto r = response::text( "err", 500 );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "HTTP/1.1 500 Internal Server Error\r\n" ) );
}

TEST_CASE( "serialize: user Connection: close overrides keep-alive", "[regression][serialize]" ) {
    auto r = response::text( "bye" );
    r.set_header( "Connection", "close" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Connection: close\r\n" ) );
    CHECK_THAT( s, !ContainsSubstring( "keep-alive" ) );
}

TEST_CASE( "serialize: user Content-Length not duplicated", "[regression][serialize]" ) {
    auto r = response::text( "hi" );
    r.set_header( "Content-Length", "99" );
    auto s = r.serialize();
    size_t first = s.find( "Content-Length:" );
    size_t second = s.find( "Content-Length:", first + 1 );
    CHECK( second == std::string::npos );
}

TEST_CASE( "serialize: large body content-length correct", "[regression][serialize]" ) {
    std::string big( 100000, 'X' );
    auto r = response::text( big );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Length: 100000\r\n" ) );
}

TEST_CASE( "parse_headers: content-length parsed correctly", "[regression][parser]" ) {
    auto req = parse_headers( "POST /data HTTP/1.1\r\nHost: x\r\nContent-Length: 42\r\n\r\n" );
    auto* cl = req.find_header( "content-length" );
    REQUIRE( cl != nullptr );
    CHECK( *cl == "42" );

    size_t len = 0;
    auto [ ptr, ec ] = std::from_chars( cl->data(), cl->data() + cl->size(), len );
    CHECK( ec == std::errc{} );
    CHECK( len == 42 );
}

TEST_CASE( "from_chars: rejects non-numeric content-length", "[regression][parser]" ) {
    size_t len = 0;
    std::string bad = "abc";
    auto [ ptr, ec ] = std::from_chars( bad.data(), bad.data() + bad.size(), len );
    CHECK( ec != std::errc{} );
}

TEST_CASE( "from_chars: rejects negative content-length", "[regression][parser]" ) {
    size_t len = 0;
    std::string bad = "-1";
    auto [ ptr, ec ] = std::from_chars( bad.data(), bad.data() + bad.size(), len );
    CHECK( ec != std::errc{} );
}

TEST_CASE( "from_chars: rejects overflow content-length", "[regression][parser]" ) {
    size_t len = 0;
    std::string bad = "99999999999999999999999";
    auto [ ptr, ec ] = std::from_chars( bad.data(), bad.data() + bad.size(), len );
    CHECK( ec != std::errc{} );
}