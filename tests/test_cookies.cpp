#include <lundi/core.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi::detail;

TEST_CASE( "parse_cookies: single cookie", "[cookie]" ) {
    auto c = parse_cookies( "session=abc123" );
    CHECK( c.size() == 1 );
    CHECK( c[ "session" ] == "abc123" );
}

TEST_CASE( "parse_cookies: multiple cookies", "[cookie]" ) {
    auto c = parse_cookies( "session=abc; user=olafur; theme=dark" );
    CHECK( c.size() == 3 );
    CHECK( c[ "session" ] == "abc" );
    CHECK( c[ "user" ] == "olafur" );
    CHECK( c[ "theme" ] == "dark" );
}

TEST_CASE( "parse_cookies: extra whitespace", "[cookie]" ) {
    auto c = parse_cookies( "a=1;  b=2;   c=3" );
    CHECK( c[ "a" ] == "1" );
    CHECK( c[ "b" ] == "2" );
    CHECK( c[ "c" ] == "3" );
}

TEST_CASE( "parse_cookies: empty string", "[cookie]" ) {
    auto c = parse_cookies( "" );
    CHECK( c.empty() );
}

TEST_CASE( "parse_cookies: value with equals sign", "[cookie]" ) {
    auto c = parse_cookies( "token=abc=def=ghi" );
    CHECK( c[ "token" ] == "abc=def=ghi" );
}

TEST_CASE( "parse_cookies: no value", "[cookie]" ) {
    auto c = parse_cookies( "flag" );
    CHECK( c.empty() );
}

TEST_CASE( "request: cookie access", "[cookie]" ) {
    lundi::request req;
    req.headers_arr[ req.num_headers++ ] = { "cookie", "session=xyz; user=test" };
    CHECK( req.cookie( "session" ) == "xyz" );
    CHECK( req.cookie( "user" ) == "test" );
    CHECK( req.cookie( "missing" ) == "" );
    CHECK( req.has_cookie( "session" ) );
    CHECK_FALSE( req.has_cookie( "missing" ) );
}

TEST_CASE( "request: cookies lazy parsed once", "[cookie]" ) {
    lundi::request req;
    req.headers_arr[ req.num_headers++ ] = { "cookie", "a=1" };
    CHECK( req.cookie( "a" ) == "1" );
    CHECK( req.cookie( "a" ) == "1" );
    CHECK( req.cookies().size() == 1 );
}

TEST_CASE( "request: no cookie header", "[cookie]" ) {
    lundi::request req;
    CHECK( req.cookie( "anything" ) == "" );
    CHECK_FALSE( req.has_cookie( "anything" ) );
    CHECK( req.cookies().empty() );
}