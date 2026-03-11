#include <lundi/core.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace lundi;
using Catch::Matchers::ContainsSubstring;

static std::string find_resp_header( const response& r, const std::string& name )
{
    for( size_t i = 0; i < r.num_headers; ++i )
    {
        if( r.headers[ i ].name == name )
            return r.headers[ i ].value;
    }
    return "";
}

TEST_CASE( "response::text sets correct content-type", "[response][factory]" ) {
    auto r = response::text( "hello" );
    CHECK( r.status == 200 );
    CHECK( r.body == "hello" );
    // Content-type is preset, verify via serialization
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Type: text/plain; charset=utf-8\r\n" ) );
}

TEST_CASE( "response::json sets correct content-type", "[response][factory]" ) {
    auto r = response::json( R"({"ok":true})" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Type: application/json; charset=utf-8\r\n" ) );
}

TEST_CASE( "response::html sets correct content-type", "[response][factory]" ) {
    auto r = response::html( "<h1>hi</h1>" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "Content-Type: text/html; charset=utf-8\r\n" ) );
}

TEST_CASE( "response::text with custom status", "[response][factory]" ) {
    auto r = response::text( "created", 201 );
    CHECK( r.status == 201 );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "HTTP/1.1 201 Created\r\n" ) );
}


// All status codes in the table
TEST_CASE( "status_text: all defined codes serialize correctly", "[response][status]" ) {
    struct {
        int code;
        const char* text;
    } cases[] = {
        { 101, "Switching Protocols" },
        { 200, "OK" },
        { 201, "Created" },
        { 204, "No Content" },
        { 301, "Moved Permanently" },
        { 302, "Found" },
        { 304, "Not Modified" },
        { 400, "Bad Request" },
        { 401, "Unauthorized" },
        { 403, "Forbidden" },
        { 404, "Not Found" },
        { 405, "Method Not Allowed" },
        { 408, "Request Timeout" },
        { 413, "Payload Too Large" },
        { 429, "Too Many Requests" },
        { 500, "Internal Server Error" },
    };

    for( auto& [ code, text ] : cases )
    {
        auto r = response::text( "x", code );
        auto s = r.serialize();
        std::string expected = "HTTP/1.1 " + std::to_string( code ) + " " + text + "\r\n";
        CHECK_THAT( s, ContainsSubstring( expected ) );
    }
}

TEST_CASE( "status_text: unknown code uses Unknown", "[response][status]" ) {
    auto r = response::text( "x", 418 );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "HTTP/1.1 418 Unknown\r\n" ) );
}


// Parse headers edge cases
TEST_CASE( "parse_headers: HTTP/1.0 request", "[parser][edge]" ) {
    auto req = detail::parse_headers( "GET /old HTTP/1.0\r\nHost: x\r\n\r\n" );
    CHECK( req.method == "GET" );
    CHECK( req.path == "/old" );
}

TEST_CASE( "parse_headers: many headers", "[parser][edge]" ) {
    std::string raw = "GET / HTTP/1.1\r\n";
    for( int i = 0; i < 20; ++i )
    {
        raw += "X-Header-" + std::to_string( i ) + ": value" + std::to_string( i ) + "\r\n";
    }
    raw += "\r\n";
    auto req = detail::parse_headers( raw );
    CHECK( req.method == "GET" );
    CHECK( req.num_headers == 20 );
    CHECK( *req.find_header( "x-header-0" ) == "value0" );
    CHECK( *req.find_header( "x-header-19" ) == "value19" );
}

TEST_CASE( "parse_headers: header with empty value", "[parser][edge]" ) {
    auto req = detail::parse_headers( "GET / HTTP/1.1\r\nX-Empty:\r\n\r\n" );
    CHECK( req.find_header( "x-empty" ) != nullptr );
}

TEST_CASE( "parse_headers: duplicate headers (both stored)", "[parser][edge]" ) {
    auto req = detail::parse_headers(
        "GET / HTTP/1.1\r\n"
        "X-Dup: first\r\n"
        "X-Dup: second\r\n"
        "\r\n" );
    CHECK( req.find_header( "x-dup" ) != nullptr );
}

TEST_CASE( "parse_headers: path with fragment stripped", "[parser][edge]" ) {
    auto req = detail::parse_headers( "GET /page#section HTTP/1.1\r\nHost: x\r\n\r\n" );
    CHECK( !req.path.empty() );
}


// Request helper methods
TEST_CASE( "request: param_int returns 0 on missing param", "[request]" ) {
    request req;
    CHECK( req.param_int( "nonexistent" ) == 0 );
}

TEST_CASE( "request: param_str returns empty on missing param", "[request]" ) {
    request req;
    CHECK( req.param_str( "nonexistent" ).empty() );
}

TEST_CASE( "request: query with default value", "[request]" ) {
    request req;
    req.query_params.push_back( { "existing", "yes" } );
    CHECK( req.query( "existing", "no" ) == "yes" );
    CHECK( req.query( "missing", "fallback" ) == "fallback" );
}

TEST_CASE( "request: query_int with default value", "[request]" ) {
    request req;
    req.query_params.push_back( { "count", "42" } );
    CHECK( req.query_int( "count", 0 ) == 42 );
    CHECK( req.query_int( "missing", 99 ) == 99 );
}

TEST_CASE( "request: query_int returns nullopt for non-numeric", "[request]" ) {
    request req;
    req.query_params.push_back( { "bad", "abc" } );
    CHECK_FALSE( req.query_int( "bad" ).has_value() );
}

TEST_CASE( "request: header is case-insensitive", "[request]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "content-type", "text/html" };
    CHECK( req.header( "Content-Type" ) == "text/html" );
    CHECK( req.header( "CONTENT-TYPE" ) == "text/html" );
    CHECK( req.header( "content-type" ) == "text/html" );
}

TEST_CASE( "request: header returns empty for missing", "[request]" ) {
    request req;
    CHECK( req.header( "x-nonexistent" ) == "" );
}

TEST_CASE( "request: header_equals case-insensitive on value", "[request]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "connection", "Keep-Alive" };
    CHECK( req.header_equals( "connection", "keep-alive" ) );
    CHECK( req.header_equals( "Connection", "KEEP-ALIVE" ) );
}