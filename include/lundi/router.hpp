#ifndef LUNDI_ROUTER_HPP
#define LUNDI_ROUTER_HPP

#include <asio/awaitable.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core.hpp"

namespace lundi
{


// Handler type

using handler_t = handler_fn< asio::awaitable< response >( request& ) >;


// Route pattern matching (zero-allocation path splitting)

namespace detail
{

/// @brief A single segment of a parsed route pattern.
struct route_segment {
    /// @brief Segment type: literal text, integer parameter, or string parameter.
    enum kind : std::uint8_t { LITERAL, PARAM_INT, PARAM_STRING };
    kind type;         ///< Segment type
    std::string text;  ///< Literal value or parameter name
};

inline std::vector< route_segment > parse_pattern( const std::string& pattern )
{
    std::vector< route_segment > segments;
    std::string_view sv( pattern );
    if( !sv.empty() && sv[ 0 ] == '/' )
    {
        sv.remove_prefix( 1 );
    }

    while( !sv.empty() )
    {
        auto slash = sv.find( '/' );
        auto part = sv.substr( 0, slash );

        if( !part.empty() )
        {
            if( part.front() == '<' && part.back() == '>' )
            {
                auto inner = part.substr( 1, part.size() - 2 );
                auto colon = inner.find( ':' );
                if( colon != std::string_view::npos )
                {
                    auto type_str = inner.substr( 0, colon );
                    auto name = inner.substr( colon + 1 );
                    if( type_str == "int" )
                    {
                        segments.push_back( { route_segment::PARAM_INT, std::string( name ) } );
                    }
                    else
                    {
                        segments.push_back( { route_segment::PARAM_STRING, std::string( name ) } );
                    }
                }
                else
                {
                    segments.push_back( { route_segment::PARAM_STRING, std::string( inner ) } );
                }
            }
            else
            {
                segments.push_back( { route_segment::LITERAL, std::string( part ) } );
            }
        }

        if( slash == std::string_view::npos )
        {
            break;
        }
        sv.remove_prefix( slash + 1 );
    }
    return segments;
}

/// @brief Zero-allocation path splitter — fixed array of string_views into the path.
struct path_segments {
    static constexpr size_t MAX_SEGMENTS = 16;  ///< Maximum URL depth
    std::string_view segments[ MAX_SEGMENTS ];    ///< Segment views into the original path
    size_t count = 0;                           ///< Number of populated segments
};

inline path_segments split_path_sv( std::string_view path )
{
    path_segments result;
    if( !path.empty() && path[ 0 ] == '/' )
    {
        path.remove_prefix( 1 );
    }

    while( !path.empty() && result.count < path_segments::MAX_SEGMENTS )
    {
        auto slash = path.find( '/' );
        auto part = path.substr( 0, slash );

        if( !part.empty() )
        {
            result.segments[ result.count++ ] = part;
        }

        if( slash == std::string_view::npos )
        {
            break;
        }
        path.remove_prefix( slash + 1 );
    }
    return result;
}

// Backwards compatible: allocating version for tests that use std::vector<std::string>
inline std::vector< std::string > split_path( const std::string& path )
{
    auto sv = split_path_sv( path );
    std::vector< std::string > parts;
    parts.reserve( sv.count );
    for( size_t i = 0; i < sv.count; ++i )
    {
        parts.emplace_back( sv.segments[ i ] );
    }
    return parts;
}

inline bool match_route( const std::vector< route_segment >& pattern, const std::string& path,
                         std::vector< request::param_entry >& params )
{
    auto parts = split_path_sv( path );
    if( parts.count != pattern.size() )
    {
        return false;
    }

    size_t initial_size = params.size();

    for( size_t i = 0; i < pattern.size(); ++i )
    {
        const auto& seg = pattern[ i ];
        auto val = parts.segments[ i ];
        switch( seg.type )
        {
            case route_segment::LITERAL:
                if( val != seg.text )
                {
                    params.resize( initial_size );
                    return false;
                }
                break;
            case route_segment::PARAM_INT:
            {
                int num = 0;
                auto [ ptr, ec ] = std::from_chars( val.data(), val.data() + val.size(), num );
                if( ec != std::errc{} || ptr != val.data() + val.size() )
                {
                    params.resize( initial_size );
                    return false;
                }
                params.push_back( { seg.text, num } );
                break;
            }
            case route_segment::PARAM_STRING:
                params.push_back( { seg.text, std::string( val ) } );
                break;
        }
    }
    return true;
}

}  // namespace detail


// Router


/// @brief URL router — matches request method + path to a registered handler.
class router
{
public:
    /// @brief A registered route: method, parsed pattern, and handler.
    struct route_entry {
        std::string method;                          ///< HTTP method ("GET", "POST", etc.)
        std::vector< detail::route_segment > pattern;  ///< Parsed URL segments
        handler_t handler;                           ///< Handler coroutine
    };

    /// @brief Register a route.
    /// @param method  HTTP method (e.g. "GET")
    /// @param pattern URL pattern (e.g. "/users/<int:id>")
    /// @param handler Coroutine handler
    void add( const std::string& method, const std::string& pattern, handler_t handler )
    {
        routes_.push_back( { method, detail::parse_pattern( pattern ), std::move( handler ) } );
    }

    /// @brief Result of a route lookup. Falsy if no route matched.
    struct match_result {
        handler_t* handler = nullptr;  ///< Pointer to matched handler, or nullptr
        /// @brief True if a route was matched.
        explicit operator bool() const
        {
            return handler != nullptr;
        }
    };

    /// @brief Find a handler matching the given method and path.
    /// @param method HTTP method
    /// @param path   URL path (already decoded)
    /// @param params Output vector — cleared then populated with extracted parameters
    /// @return match_result with pointer to handler, or falsy if no match
    match_result resolve( std::string_view method, const std::string& path,
                          std::vector< request::param_entry >& params )
    {
        for( auto& route : routes_ )
        {
            if( route.method != method )
            {
                continue;
            }

            params.clear();
            if( detail::match_route( route.pattern, path, params ) )
            {
                return {&route.handler };
            }
        }
        return {};
    }

private:
    std::vector< route_entry > routes_;
};

}  // namespace lundi

#endif  // LUNDI_ROUTER_HPP