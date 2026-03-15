#include <lundi/core.hpp>
#include <lundi/router.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi;
using namespace lundi::detail;

// Helper: find query param by name in vector
static std::string find_query( const request& req, const std::string& name )
{
    for( const auto& q : req.query_params )
        if( q.name == name )
            return q.value;
    return "";
}

TEST_CASE( "to_lower: basic", "[detail]" ) {
    CHECK( to_lower( "HELLO" ) == "hello" );
    CHECK( to_lower( "Hello World" ) == "hello world" );
    CHECK( to_lower( "already lower" ) == "already lower" );
    CHECK( to_lower( "" ) == "" );
}

TEST_CASE( "to_lower: mixed with numbers and symbols", "[detail]" ) {
    CHECK( to_lower( "Content-Type" ) == "content-type" );
    CHECK( to_lower( "X-Request-ID-123" ) == "x-request-id-123" );
}

TEST_CASE( "parse_headers: DELETE method", "[parser][methods]" ) {
    auto req = parse_headers( "DELETE /items/42 HTTP/1.1\r\nHost: x\r\n\r\n" );
    CHECK( req.method == "DELETE" );
    CHECK( req.path == "/items/42" );
}

TEST_CASE( "parse_headers: PUT method", "[parser][methods]" ) {
    auto req = parse_headers( "PUT /items/42 HTTP/1.1\r\nHost: x\r\n\r\n" );
    CHECK( req.method == "PUT" );
}

TEST_CASE( "parse_headers: PATCH method", "[parser][methods]" ) {
    auto req = parse_headers( "PATCH /items/42 HTTP/1.1\r\nHost: x\r\n\r\n" );
    CHECK( req.method == "PATCH" );
}

TEST_CASE( "parse_headers: OPTIONS method", "[parser][methods]" ) {
    auto req = parse_headers( "OPTIONS /api HTTP/1.1\r\nHost: x\r\n\r\n" );
    CHECK( req.method == "OPTIONS" );
}

TEST_CASE( "parse_headers: HEAD method", "[parser][methods]" ) {
    auto req = parse_headers( "HEAD /hello HTTP/1.1\r\nHost: x\r\n\r\n" );
    CHECK( req.method == "HEAD" );
}

TEST_CASE( "parse_headers: very long path", "[parser][edge]" ) {
    std::string long_path( 2000, 'a' );
    std::string raw = "GET /" + long_path + " HTTP/1.1\r\nHost: x\r\n\r\n";
    auto req = parse_headers( raw );
    CHECK( req.method == "GET" );
    CHECK( req.path.size() == 2001 );
}

TEST_CASE( "parse_headers: path with special characters", "[parser][edge]" ) {
    auto req = parse_headers( "GET /api/data?key=a%26b&foo=bar%3Dbaz HTTP/1.1\r\nHost: x\r\n\r\n" );
    CHECK( req.path == "/api/data" );
    CHECK( find_query( req, "key" ) == "a&b" );
    CHECK( find_query( req, "foo" ) == "bar=baz" );
}

TEST_CASE( "parse_headers: multiple query params with same prefix", "[parser][edge]" ) {
    auto req = parse_headers( "GET /s?q=test&q_lang=en HTTP/1.1\r\nHost: x\r\n\r\n" );
    CHECK( find_query( req, "q" ) == "test" );
    CHECK( find_query( req, "q_lang" ) == "en" );
}

TEST_CASE( "parse_headers: no headers at all", "[parser][edge]" ) {
    auto req = parse_headers( "GET / HTTP/1.1\r\n\r\n" );
    CHECK( req.method == "GET" );
    CHECK( req.path == "/" );
    CHECK( req.num_headers == 0 );
}

TEST_CASE( "parse_headers: header value with colon", "[parser][edge]" ) {
    auto req = parse_headers( "GET / HTTP/1.1\r\nReferer: http://example.com:8080/path\r\n\r\n" );
    CHECK( *req.find_header( "referer" ) == "http://example.com:8080/path" );
}

TEST_CASE( "parse_headers: Connection close detected", "[parser][edge]" ) {
    auto req = parse_headers( "GET / HTTP/1.1\r\nConnection: close\r\n\r\n" );
    CHECK( req.header_equals( "connection", "close" ) );
}

TEST_CASE( "parse_headers: multiple same-name headers", "[parser][edge]" ) {
    auto req = parse_headers(
        "GET / HTTP/1.1\r\n"
        "X-Multi: first\r\n"
        "X-Multi: second\r\n"
        "\r\n" );
    CHECK( req.find_header( "x-multi" ) != nullptr );
}

TEST_CASE( "parse_pattern: literal segments", "[router][pattern]" ) {
    auto segs = parse_pattern( "/api/v1/users" );
    REQUIRE( segs.size() == 3 );
    CHECK( segs[ 0 ].type == route_segment::LITERAL );
    CHECK( segs[ 0 ].text == "api" );
    CHECK( segs[ 1 ].text == "v1" );
    CHECK( segs[ 2 ].text == "users" );
}

TEST_CASE( "parse_pattern: int parameter", "[router][pattern]" ) {
    auto segs = parse_pattern( "/users/<int:id>" );
    REQUIRE( segs.size() == 2 );
    CHECK( segs[ 0 ].type == route_segment::LITERAL );
    CHECK( segs[ 1 ].type == route_segment::PARAM_INT );
    CHECK( segs[ 1 ].text == "id" );
}

TEST_CASE( "parse_pattern: string parameter", "[router][pattern]" ) {
    auto segs = parse_pattern( "/files/<string:name>" );
    REQUIRE( segs.size() == 2 );
    CHECK( segs[ 1 ].type == route_segment::PARAM_STRING );
    CHECK( segs[ 1 ].text == "name" );
}

TEST_CASE( "parse_pattern: multiple params", "[router][pattern]" ) {
    auto segs = parse_pattern( "/users/<int:uid>/posts/<int:pid>" );
    REQUIRE( segs.size() == 4 );
    CHECK( segs[ 0 ].text == "users" );
    CHECK( segs[ 1 ].type == route_segment::PARAM_INT );
    CHECK( segs[ 1 ].text == "uid" );
    CHECK( segs[ 2 ].text == "posts" );
    CHECK( segs[ 3 ].type == route_segment::PARAM_INT );
    CHECK( segs[ 3 ].text == "pid" );
}

TEST_CASE( "parse_pattern: root path", "[router][pattern]" ) {
    auto segs = parse_pattern( "/" );
    CHECK( segs.empty() );
}

TEST_CASE( "parse_pattern: param with explicit string type", "[router][pattern]" ) {
    auto segs = parse_pattern( "/<string:slug>" );
    REQUIRE( segs.size() == 1 );
    CHECK( segs[ 0 ].type == route_segment::PARAM_STRING );
    CHECK( segs[ 0 ].text == "slug" );
}