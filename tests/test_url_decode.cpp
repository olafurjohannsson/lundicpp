#include <lundi/core.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi::detail;

TEST_CASE( "url_decode: basic ASCII passthrough", "[url]" ) {
    CHECK( url_decode( "hello" ) == "hello" );
    CHECK( url_decode( "/users/42" ) == "/users/42" );
    CHECK( url_decode( "" ) == "" );
}

TEST_CASE( "url_decode: %20 and + decode to space", "[url]" ) {
    CHECK( url_decode( "hello%20world" ) == "hello world" );
    CHECK( url_decode( "hello+world" ) == "hello world" );
    CHECK( url_decode( "a%20b+c%20d" ) == "a b c d" );
}

TEST_CASE( "url_decode: common encoded characters", "[url]" ) {
    CHECK( url_decode( "%2F" ) == "/" );
    CHECK( url_decode( "%3A" ) == ":" );
    CHECK( url_decode( "%40" ) == "@" );
    CHECK( url_decode( "%26" ) == "&" );
    CHECK( url_decode( "%3D" ) == "=" );
    CHECK( url_decode( "%3F" ) == "?" );
}

TEST_CASE( "url_decode: lowercase hex", "[url]" ) {
    CHECK( url_decode( "%2f" ) == "/" );
    CHECK( url_decode( "%3a" ) == ":" );
}

TEST_CASE( "url_decode: invalid percent sequences pass through", "[url]" ) {
    CHECK( url_decode( "%GZ" ) == "%GZ" );
    CHECK( url_decode( "%" ) == "%" );
    CHECK( url_decode( "%2" ) == "%2" );
}

TEST_CASE( "url_decode: mixed encoded and plain", "[url]" ) {
    CHECK( url_decode( "/users/john%20doe?q=hello%26world" ) == "/users/john doe?q=hello&world" );
}
