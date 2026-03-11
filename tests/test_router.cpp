#include <lundi/router.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi;

static handler_t make_dummy()
{
    return []( request& ) -> asio::awaitable< response > {
        co_return response::text( "ok" );
    };
}

//find param by name in vector
static const param_value* find_param( const std::vector< request::param_entry >& params,
                                      const std::string& name )
{
    for( const auto& p : params )
        if( p.name == name )
            return &p.value;
    return nullptr;
}

TEST_CASE( "router: literal path match", "[router]" ) {
    router r;
    r.add( "GET", "/hello", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/hello", params );
    CHECK( match );
}

TEST_CASE( "router: no match returns null", "[router]" ) {
    router r;
    r.add( "GET", "/hello", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/world", params );
    CHECK_FALSE( match );
}

TEST_CASE( "router: method mismatch", "[router]" ) {
    router r;
    r.add( "GET", "/hello", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "POST", "/hello", params );
    CHECK_FALSE( match );
}

TEST_CASE( "router: int path parameter", "[router]" ) {
    router r;
    r.add( "GET", "/users/<int:id>", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/users/42", params );
    CHECK( match );
    auto* val = find_param( params, "id" );
    REQUIRE( val != nullptr );
    CHECK( std::get< int >( *val ) == 42 );
}

TEST_CASE( "router: string path parameter", "[router]" ) {
    router r;
    r.add( "GET", "/files/<string:name>", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/files/readme.txt", params );
    CHECK( match );
    auto* val = find_param( params, "name" );
    REQUIRE( val != nullptr );
    CHECK( std::get< std::string >( *val ) == "readme.txt" );
}

TEST_CASE( "router: int param rejects non-numeric", "[router]" ) {
    router r;
    r.add( "GET", "/users/<int:id>", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/users/abc", params );
    CHECK_FALSE( match );
}

TEST_CASE( "router: multiple parameters", "[router]" ) {
    router r;
    r.add( "GET", "/users/<int:uid>/posts/<int:pid>", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/users/5/posts/99", params );
    CHECK( match );
    auto* uid = find_param( params, "uid" );
    auto* pid = find_param( params, "pid" );
    REQUIRE( uid != nullptr );
    REQUIRE( pid != nullptr );
    CHECK( std::get< int >( *uid ) == 5 );
    CHECK( std::get< int >( *pid ) == 99 );
}

TEST_CASE( "router: multiple routes picks correct one", "[router]" ) {
    router r;
    r.add( "GET", "/a", make_dummy() );
    r.add( "GET", "/b", make_dummy() );
    r.add( "POST", "/a", make_dummy() );
    std::vector< request::param_entry > params;
    CHECK( r.resolve( "GET", "/a", params ) );
    CHECK( r.resolve( "GET", "/b", params ) );
    CHECK( r.resolve( "POST", "/a", params ) );
    CHECK_FALSE( r.resolve( "POST", "/b", params ) );
}

TEST_CASE( "router: trailing slash treated as equivalent", "[router]" ) {
    router r;
    r.add( "GET", "/hello", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/hello/", params );
    CHECK( match );
}

TEST_CASE( "router: root path", "[router]" ) {
    router r;
    r.add( "GET", "/", make_dummy() );
    std::vector< request::param_entry > params;
    auto match = r.resolve( "GET", "/", params );
    CHECK( match );
}