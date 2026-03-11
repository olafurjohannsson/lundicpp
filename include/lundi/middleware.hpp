#ifndef LUNDI_MIDDLEWARE_HPP
#define LUNDI_MIDDLEWARE_HPP

#include <asio/awaitable.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core.hpp"

namespace lundi
{

// Middleware types
using next_t = handler_fn< asio::awaitable< response >( request& ) >;
using middleware_t = handler_fn< asio::awaitable< response >( request&, next_t& ) >;

/// @brief Composable middleware chain, compiled once at server start
///
/// Middleware functions are added with add() and compiled into a single
/// callable with compile(). The compiled chain is invoked per-request
/// with zero overhead beyond the actual middleware logic.
class middleware_chain
{
public:
    /// @brief Append a middleware function to the chain.
    void add( middleware_t mw )
    {
        layers_.push_back( std::move( mw ) );
    }

    /// @brief True if no middleware has been added.
    bool empty() const
    {
        return layers_.empty();
    }

    /// @brief Compile all middleware into a single next_t callable.
    /// @param inner The final handler at the end of the chain
    /// @return A single callable that runs all middleware then the handler
    next_t compile( next_t inner ) const
    {
        auto chain = std::make_shared< next_t >( std::move( inner ) );
        for( auto it = layers_.rbegin(); it != layers_.rend(); ++it )
        {
            const auto* mw = &( *it );
            auto prev = chain;
            chain = std::make_shared< next_t >( [ mw, prev ]( request& r ) -> asio::awaitable< response > {
                co_return co_await (*mw)( r, *prev );
            } );
        }
        return std::move( *chain );
    }

private:
    std::vector< middleware_t > layers_;
};

//@brief construct a vector of middleware from a parameter pack, with perfect forwarding
inline std::vector< middleware_t > mw( auto&&... mws )
{
    std::vector< middleware_t > v;
    v.reserve( sizeof...( mws ) );
    ( v.push_back( std::forward< decltype( mws ) >( mws ) ), ... );
    return v;
}

/// @brief Configuration for the built-in CORS middleware.
struct cors_options {
    std::string allow_origin = "*";  ///< Access-Control-Allow-Origin value
    std::string allow_methods = "GET, POST, PUT, DELETE, PATCH, OPTIONS";  ///< Allowed methods
    std::string allow_headers = "Content-Type, Authorization";             ///< Allowed headers
    int max_age = 86400;  ///< Preflight cache duration in seconds
};

inline middleware_t cors( cors_options opts = {} )
{
    return [ opts = std::move( opts ) ]( request& req, next_t& next ) -> asio::awaitable< response > {
        if( req.method == "OPTIONS" )
        {
            response res;
            res.status = 204;
            res.set_header( "Access-Control-Allow-Origin", opts.allow_origin );
            res.set_header( "Access-Control-Allow-Methods", opts.allow_methods );
            res.set_header( "Access-Control-Allow-Headers", opts.allow_headers );
            res.set_header( "Access-Control-Max-Age", std::to_string( opts.max_age ) );
            co_return res;
        }

        auto res = co_await next( req );
        res.set_header( "Access-Control-Allow-Origin", opts.allow_origin );
        co_return res;
    };
}


// Built-in middleware: Logger

inline middleware_t logger()
{
    return []( request& req, next_t& next ) -> asio::awaitable< response > {
        auto start = std::chrono::steady_clock::now();
        auto method = req.method;
        auto path = req.raw_path;

        auto res = co_await next( req );

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto us = std::chrono::duration_cast< std::chrono::microseconds >( elapsed ).count();

        std::cout << "[lundi] " << method << " " << path << " → " << res.status << " (" << us
        << "μs)\n";

        co_return res;
    };
}

}  // namespace lundi

#endif  // LUNDI_MIDDLEWARE_HPP