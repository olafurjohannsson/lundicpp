#include <lundi/router.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi;
using namespace lundi::detail;


// Zero-alloc path splitter (split_path_sv)
TEST_CASE( "split_path_sv: simple path", "[router][split]" ) {
    auto parts = split_path_sv( "/users/42/posts" );
    CHECK( parts.count == 3 );
    CHECK( parts.segments[ 0 ] == "users" );
    CHECK( parts.segments[ 1 ] == "42" );
    CHECK( parts.segments[ 2 ] == "posts" );
}

TEST_CASE( "split_path_sv: root path", "[router][split]" ) {
    auto parts = split_path_sv( "/" );
    CHECK( parts.count == 0 );
}

TEST_CASE( "split_path_sv: single segment", "[router][split]" ) {
    auto parts = split_path_sv( "/hello" );
    CHECK( parts.count == 1 );
    CHECK( parts.segments[ 0 ] == "hello" );
}

TEST_CASE( "split_path_sv: trailing slash ignored", "[router][split]" ) {
    auto parts = split_path_sv( "/hello/" );
    CHECK( parts.count == 1 );
    CHECK( parts.segments[ 0 ] == "hello" );
}

TEST_CASE( "split_path_sv: double slash collapsed", "[router][split]" ) {
    auto parts = split_path_sv( "/a//b" );
    CHECK( parts.count == 2 );
    CHECK( parts.segments[ 0 ] == "a" );
    CHECK( parts.segments[ 1 ] == "b" );
}

TEST_CASE( "split_path_sv: empty string", "[router][split]" ) {
    auto parts = split_path_sv( "" );
    CHECK( parts.count == 0 );
}

TEST_CASE( "split_path_sv: max segments", "[router][split]" ) {
    auto parts = split_path_sv( "/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p" );
    CHECK( parts.count == 16 );
    CHECK( parts.segments[ 0 ] == "a" );
    CHECK( parts.segments[ 15 ] == "p" );
}

TEST_CASE( "split_path_sv: over max segments truncates", "[router][split]" ) {
    auto parts = split_path_sv( "/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q" );
    CHECK( parts.count == 16 );
}

TEST_CASE( "split_path_sv: no leading slash", "[router][split]" ) {
    auto parts = split_path_sv( "hello/world" );
    CHECK( parts.count == 2 );
    CHECK( parts.segments[ 0 ] == "hello" );
    CHECK( parts.segments[ 1 ] == "world" );
}


// Route groups (tested via router groups just prefix paths)
static handler_t make_dummy()
{
    return []( request& ) -> asio::awaitable< response > {
        co_return response::text( "ok" );
    };
}

TEST_CASE( "router: grouped route matches full path", "[router][group]" ) {
    router r;
    r.add( "GET", "/api/v1/users", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/api/v1/users", params );
    CHECK( match );
}

TEST_CASE( "router: grouped route with params", "[router][group]" ) {
    router r;
    r.add( "GET", "/api/v1/users/<int:id>", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/api/v1/users/42", params );
    CHECK( match );
    CHECK( params.size() == 1 );
    CHECK( params[ 0 ].name == "id" );
    CHECK( std::get< int >( params[ 0 ].value ) == 42 );
}

TEST_CASE( "router: grouped route does not match partial prefix", "[router][group]" ) {
    router r;
    r.add( "GET", "/api/v1/users", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/api/v1", params );
    CHECK_FALSE( match );
}

TEST_CASE( "router: nested grouped route", "[router][group]" ) {
    router r;
    r.add( "GET", "/api/v1/admin/stats", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/api/v1/admin/stats", params );
    CHECK( match );
}

TEST_CASE( "router: grouped and ungrouped routes coexist", "[router][group]" ) {
    router r;
    r.add( "GET", "/health", make_dummy() );
    r.add( "GET", "/api/v1/users", make_dummy() );
    r.add( "GET", "/api/v2/users", make_dummy() );
    std::vector< request::param_entry > params;
    CHECK( r.resolve( "GET", "/health", params ) );
    CHECK( r.resolve( "GET", "/api/v1/users", params ) );
    CHECK( r.resolve( "GET", "/api/v2/users", params ) );
    CHECK_FALSE( r.resolve( "GET", "/api/v3/users", params ) );
}