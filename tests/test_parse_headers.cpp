#include <lundi/core.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi;
using namespace lundi::detail;

//find query param by name in vector
static std::string find_query( const request& req, const std::string& name )
{
    for( const auto& q : req.query_params )
        if( q.name == name )
            return q.value;
    return "";
}

TEST_CASE( "parse_headers: basic GET request", "[parser]" ) {
    auto req = parse_headers( "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n" );
    CHECK( req.method == "GET" );
    CHECK( req.path == "/hello" );
    CHECK( req.num_headers == 1 );
    CHECK( *req.find_header( "host" ) == "localhost" );
}

TEST_CASE( "parse_headers: POST with content-length", "[parser]" ) {
    auto req = parse_headers(
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n" );
    CHECK( req.method == "POST" );
    CHECK( req.path == "/echo" );
    CHECK( req.num_headers == 3 );
    CHECK( *req.find_header( "content-length" ) == "5" );
    CHECK( *req.find_header( "content-type" ) == "text/plain" );
}

TEST_CASE( "parse_headers: query string split from path", "[parser]" ) {
    auto req = parse_headers( "GET /search?q=hello&limit=10 HTTP/1.1\r\nHost: localhost\r\n\r\n" );
    CHECK( req.path == "/search" );
    CHECK( req.raw_path == "/search?q=hello&limit=10" );
    CHECK( find_query( req, "q" ) == "hello" );
    CHECK( find_query( req, "limit" ) == "10" );
}

TEST_CASE( "parse_headers: URL-decoded path", "[parser]" ) {
    auto req = parse_headers( "GET /users/john%20doe HTTP/1.1\r\nHost: localhost\r\n\r\n" );
    CHECK( req.path == "/users/john doe" );
}

TEST_CASE( "parse_headers: header lookup is case-insensitive", "[parser]" ) {
    auto req = parse_headers(
        "GET / HTTP/1.1\r\n"
        "Content-Type: text/html\r\n"
        "X-Custom-Header: value\r\n"
        "\r\n" );
    // All case variants should match (lookup is case-insensitive)
    CHECK( req.find_header( "content-type" ) != nullptr );
    CHECK( req.find_header( "Content-Type" ) != nullptr );
    CHECK( req.find_header( "CONTENT-TYPE" ) != nullptr );
    CHECK( req.find_header( "x-custom-header" ) != nullptr );
    CHECK( req.find_header( "X-Custom-Header" ) != nullptr );
}

TEST_CASE( "parse_headers: header value whitespace trimmed", "[parser]" ) {
    auto req = parse_headers( "GET / HTTP/1.1\r\nHost:   localhost  \r\n\r\n" );
    auto* host = req.find_header( "host" );
    REQUIRE( host != nullptr );
    CHECK( host->front() != ' ' );
}

TEST_CASE( "parse_headers: empty input returns empty method", "[parser]" ) {
    auto req = parse_headers( "" );
    CHECK( req.method.empty() );
}

TEST_CASE( "parse_headers: incomplete headers return empty method", "[parser]" ) {
    auto req = parse_headers( "GET /hello HTTP" );
    CHECK( req.method.empty() );
}

TEST_CASE( "parse_headers: multiple headers with same-prefix names", "[parser]" ) {
    auto req = parse_headers(
        "GET / HTTP/1.1\r\n"
        "X-Request-Id: abc\r\n"
        "X-Request-Time: 123\r\n"
        "\r\n" );
    CHECK( *req.find_header( "x-request-id" ) == "abc" );
    CHECK( *req.find_header( "x-request-time" ) == "123" );
}