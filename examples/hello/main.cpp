#include <iostream>
#include <lundi.hpp>

// A simple auth middleware
lundi::middleware_t require_token()
{
    return []( lundi::request& req, lundi::next_t& next ) -> asio::awaitable< lundi::response > {
        auto auth = req.header( "authorization" );
        if( auth.empty() || auth != "Bearer secret123" )
        {
            co_return lundi::response::json( R"({"error":"unauthorized"})", 401 );
        }
        co_return co_await next( req );
    };
}

int main()
{
    lundi::app server;

    server.use( lundi::logger() );
    server.use( lundi::cors() );

    server.serve_static( "/", "./public" );

    server.get( "/hello", []( lundi::request& ) -> asio::awaitable< lundi::response > {
        co_return lundi::response::text( "Hello from Lundi!" );
    } );

    server.listen( { .port = 8080 } );
    return 0;
}