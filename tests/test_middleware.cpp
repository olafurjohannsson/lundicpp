#include <lundi/middleware.hpp>

#include <asio.hpp>
#include <asio/co_spawn.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi;

// Helper to find a header value in response's vector
static std::string find_resp_header( const response& r, const std::string& name )
{
    for( size_t i = 0; i < r.num_headers; ++i )
    {
        if( r.headers[ i ].name == name )
            return r.headers[ i ].value;
    }
    return "";
}

static bool has_resp_header( const response& r, const std::string& name )
{
    for( size_t i = 0; i < r.num_headers; ++i )
    {
        if( r.headers[ i ].name == name )
            return true;
    }
    return false;
}

// run a handler synchronously
static response run_handler( next_t& handler, request req = {} )
{
    asio::io_context io;
    response result;
    asio::co_spawn( io, handler( req ), [ & ]( std::exception_ptr ep, response r ) {
        if( !ep )
            result = std::move( r );
    } );
    io.run();
    return result;
}

static next_t make_handler( int status, const std::string& body )
{
    return [=]( request& ) -> asio::awaitable< response > {
        co_return response::text( body, status );
    };
}

TEST_CASE( "middleware: single middleware runs", "[middleware]" ) {
    middleware_chain chain;
    chain.add( []( request& req, next_t& next ) -> asio::awaitable< response > {
        auto res = co_await next( req );
        res.set_header( "X-Test", "yes" );
        co_return res;
    } );

    auto compiled = chain.compile( make_handler( 200, "hello" ) );
    auto res = run_handler( compiled );
    CHECK( res.status == 200 );
    CHECK( res.body == "hello" );
    CHECK( find_resp_header( res, "X-Test" ) == "yes" );
}

TEST_CASE( "middleware: chain runs in order", "[middleware]" ) {
    middleware_chain chain;

    chain.add( []( request& req, next_t& next ) -> asio::awaitable< response > {
        auto res = co_await next( req );
        res.body += "1";
        co_return res;
    } );

    chain.add( []( request& req, next_t& next ) -> asio::awaitable< response > {
        auto res = co_await next( req );
        res.body += "2";
        co_return res;
    } );

    auto compiled = chain.compile( make_handler( 200, "base" ) );
    auto res = run_handler( compiled );
    CHECK( res.body == "base21" );
}

TEST_CASE( "middleware: short-circuit without calling next", "[middleware]" ) {
    middleware_chain chain;

    chain.add( []( request&, next_t& ) -> asio::awaitable< response > {
        co_return response::text( "blocked", 403 );
    } );

    auto compiled = chain.compile( make_handler( 200, "should not reach" ) );
    auto res = run_handler( compiled );
    CHECK( res.status == 403 );
    CHECK( res.body == "blocked" );
}

TEST_CASE( "middleware: cors adds headers", "[middleware]" ) {
    middleware_chain chain;
    chain.add( cors() );

    auto compiled = chain.compile( make_handler( 200, "ok" ) );
    auto res = run_handler( compiled );
    CHECK( has_resp_header( res, "Access-Control-Allow-Origin" ) );
    CHECK( find_resp_header( res, "Access-Control-Allow-Origin" ) == "*" );
}

TEST_CASE( "middleware: cors handles OPTIONS preflight", "[middleware]" ) {
    middleware_chain chain;
    chain.add( cors() );

    request req;
    req.method = "OPTIONS";

    auto compiled = chain.compile( make_handler( 200, "should not reach" ) );
    auto res = run_handler( compiled, req );
    CHECK( res.status == 204 );
    CHECK( has_resp_header( res, "Access-Control-Allow-Methods" ) );
}

TEST_CASE( "middleware: empty chain passes through", "[middleware]" ) {
    middleware_chain chain;
    CHECK( chain.empty() );

    auto compiled = chain.compile( make_handler( 200, "direct" ) );
    auto res = run_handler( compiled );
    CHECK( res.status == 200 );
    CHECK( res.body == "direct" );
}

TEST_CASE( "middleware: per-route middleware compilation", "[middleware]" ) {
    auto auth = []( request& req, next_t& next ) -> asio::awaitable< response > {
        if( req.header( "authorization" ) != "Bearer secret" )
        {
            co_return response::text( "unauthorized", 401 );
        }
        co_return co_await next( req );
    };

    middleware_chain chain;
    chain.add( std::move( auth ) );
    auto protected_handler = chain.compile( make_handler( 200, "secret data" ) );

    // Without auth header
    auto res1 = run_handler( protected_handler );
    CHECK( res1.status == 401 );
    CHECK( res1.body == "unauthorized" );

    // With auth header
    request authed_req;
    authed_req.headers_arr[ authed_req.num_headers++ ] = { "authorization", "Bearer secret" };
    auto res2 = run_handler( protected_handler, authed_req );
    CHECK( res2.status == 200 );
    CHECK( res2.body == "secret data" );
}

TEST_CASE( "middleware: three middlewares deep", "[middleware]" ) {
    middleware_chain chain;

    chain.add( []( request& req, next_t& next ) -> asio::awaitable< response > {
        auto res = co_await next( req );
        res.set_header( "X-First", "yes" );
        co_return res;
    } );

    chain.add( []( request& req, next_t& next ) -> asio::awaitable< response > {
        auto res = co_await next( req );
        res.set_header( "X-Second", "yes" );
        co_return res;
    } );

    chain.add( []( request& req, next_t& next ) -> asio::awaitable< response > {
        auto res = co_await next( req );
        res.set_header( "X-Third", "yes" );
        co_return res;
    } );

    auto compiled = chain.compile( make_handler( 200, "deep" ) );
    auto res = run_handler( compiled );
    CHECK( res.status == 200 );
    CHECK( res.body == "deep" );
    CHECK( find_resp_header( res, "X-First" ) == "yes" );
    CHECK( find_resp_header( res, "X-Second" ) == "yes" );
    CHECK( find_resp_header( res, "X-Third" ) == "yes" );
}