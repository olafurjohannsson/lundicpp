#ifndef LUNDI_SERVER_HPP
#define LUNDI_SERVER_HPP

/// @file server.hpp
/// @brief HTTP server, connection handling, route dispatch, and static files.
///
/// Architecture: main-thread acceptor with isolated worker io_contexts.
/// Each connection is assigned to the worker with the fewest active connections
/// (least-connections balancing). Connections are handled as C++20 coroutines.

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/read.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "chunked.hpp"
#include "core.hpp"
#include "engine/buffer.hpp"
#include "engine/date_cache.hpp"
#include "engine/header_scan.hpp"
#include "middleware.hpp"
#include "router.hpp"
#include "sse.hpp"
#include "static_files.hpp"
#include "ws.hpp"

namespace lundi
{

using namespace asio::experimental::awaitable_operators;


// Server options


/// @brief Configuration for the HTTP server.
struct server_options {
    uint16_t port = 8080;             ///< Listen port
    std::string address = "0.0.0.0";  ///< Bind address
    int threads = 0;                  ///< Worker threads (0 = hardware_concurrency)
    size_t max_body_size = static_cast< size_t >( 10 ) * 1024 * 1024;  ///< Max request body (10 MB)
    size_t max_header_size = static_cast< size_t >( 64 ) * 1024;       ///< Max header block (64 KB)
    int max_keepalive_requests = 100;  ///< Max requests per connection (0 = unlimited)
    int idle_timeout_seconds = 30;     ///< Idle timeout for first request
};


// App


/// @brief The main application class. Registers routes, middleware, and starts the server.
///
/// @code
///   lundi::app server;
///   server.get("/hello", [](lundi::request& req) -> asio::awaitable<lundi::response> {
///       co_return lundi::response::text("Hello, World!");
///   });
///   server.listen({.port = 8080, .threads = 8});
/// @endcode
class app
{
public:
    app() = default;

    /// @brief Register a GET handler with variadic per-route middleware.
    template< typename ... Mws >
    void get( const std::string& pattern, handler_t handler, Mws&&... mws )
    {
        std::vector< middleware_t > v;
        ( v.push_back( std::forward< Mws >( mws ) ), ... );
        router_.add( "GET", pattern, compile_route_handler( std::move( v ), std::move( handler ) ) );
    }

    /// @brief Register a GET handler.
    /// @param pattern URL pattern, e.g. "/users/<int:id>"
    /// @param handler Coroutine handler receiving request& and returning response
    void get( const std::string& pattern, handler_t handler )
    {
        router_.add( "GET", pattern, std::move( handler ) );
    }
    /// @brief Register a POST handler.
    void post( const std::string& pattern, handler_t handler )
    {
        router_.add( "POST", pattern, std::move( handler ) );
    }
    /// @brief Register a PUT handler.
    void put( const std::string& pattern, handler_t handler )
    {
        router_.add( "PUT", pattern, std::move( handler ) );
    }
    /// @brief Register a DELETE handler.
    void del( const std::string& pattern, handler_t handler )
    {
        router_.add( "DELETE", pattern, std::move( handler ) );
    }
    /// @brief Register a PATCH handler.
    void patch( const std::string& pattern, handler_t handler )
    {
        router_.add( "PATCH", pattern, std::move( handler ) );
    }

    /// @brief Register a GET handler with per-route middleware.
    /// @param pattern URL pattern
    /// @param mws     Middleware functions applied before the handler
    /// @param handler Final route handler
    void get( const std::string& pattern, std::vector< middleware_t > mws, handler_t handler )
    {
        router_.add( "GET", pattern, compile_route_handler( std::move( mws ), std::move( handler ) ) );
    }
    /// @brief Register a POST handler with per-route middleware.
    void post( const std::string& pattern, std::vector< middleware_t > mws, handler_t handler )
    {
        router_.add( "POST", pattern, compile_route_handler( std::move( mws ), std::move( handler ) ) );
    }
    /// @brief Register a PUT handler with per-route middleware.
    void put( const std::string& pattern, std::vector< middleware_t > mws, handler_t handler )
    {
        router_.add( "PUT", pattern, compile_route_handler( std::move( mws ), std::move( handler ) ) );
    }
    /// @brief Register a DELETE handler with per-route middleware.
    void del( const std::string& pattern, std::vector< middleware_t > mws, handler_t handler )
    {
        router_.add( "DELETE", pattern, compile_route_handler( std::move( mws ), std::move( handler ) ) );
    }
    /// @brief Register a PATCH handler with per-route middleware.
    void patch( const std::string& pattern, std::vector< middleware_t > mws, handler_t handler )
    {
        router_.add( "PATCH", pattern, compile_route_handler( std::move( mws ), std::move( handler ) ) );
    }

    /// @brief Register a Server-Sent Events endpoint.
    void sse( const std::string& path, sse_handler_t handler )
    {
        sse_routes_[ path ] = std::move( handler );
    }

    /// @brief Register a WebSocket endpoint.
    void ws( const std::string& path, ws_handler_t handler )
    {
        ws_routes_[ path ] = std::move( handler );
    }

    /// @brief Register a chunked transfer-encoding streaming endpoint.
    void stream( const std::string& path, stream_handler_t handler,
                 const std::string& content_type = "application/octet-stream" )
    {
        stream_routes_[ path ] = { std::move( handler ), content_type };
    }

    /// @brief Add global middleware applied to every request.
    /// Middleware is compiled into a single chain at server start (zero per-request overhead).
    void use( middleware_t mw )
    {
        middleware_.add( std::move( mw ) );
    }

    /// @brief Serve static files from a directory.
    /// @param url_prefix URL prefix to match (e.g. "/" or "/static/")
    /// @param directory  Filesystem directory to serve from
    /// @param opts       Caching options (ETag, Cache-Control max-age, etc.)
    ///
    /// Can be called multiple times for multiple mount points.
    /// Mount points are checked in registration order — first match wins.
    void serve_static(
        const std::string& url_prefix,  // NOLINT(bugprone-easily-swappable-parameters)
        const std::string& directory,
        static_options opts = {} )
    {
        static_files_.mount( url_prefix, directory, std::move( opts ) );
    }

    /// @brief Scoped route group with a shared URL prefix.
    /// Created via app::group("/api/v1").
    class route_group
    {
    public:
        /// @param parent The parent app instance
        /// @param prefix URL prefix for all routes in this group
        route_group( app& parent, std::string prefix ) : parent_( parent ), prefix_( std::move( prefix ) )
        {
            if( prefix_.empty() || prefix_[ 0 ] != '/' )
            {
                prefix_ = "/" + prefix_;
            }
            if( prefix_.size() > 1 && prefix_.back() == '/' )
            {
                prefix_.pop_back();
            }
        }

        /// @brief Register a GET handler in this group.
        void get( const std::string& pattern, handler_t handler )
        {
            parent_.get( prefix_ + pattern, std::move( handler ) );
        }
        /// @brief Register a POST handler in this group.
        void post( const std::string& pattern, handler_t handler )
        {
            parent_.post( prefix_ + pattern, std::move( handler ) );
        }
        /// @brief Register a PUT handler in this group.
        void put( const std::string& pattern, handler_t handler )
        {
            parent_.put( prefix_ + pattern, std::move( handler ) );
        }
        /// @brief Register a DELETE handler in this group.
        void del( const std::string& pattern, handler_t handler )
        {
            parent_.del( prefix_ + pattern, std::move( handler ) );
        }
        /// @brief Register a PATCH handler in this group.
        void patch( const std::string& pattern, handler_t handler )
        {
            parent_.patch( prefix_ + pattern, std::move( handler ) );
        }

        /// @brief Register a GET handler with middleware in this group.
        void get( const std::string& pattern, std::vector< middleware_t > mws, handler_t handler )
        {
            parent_.get( prefix_ + pattern, std::move( mws ), std::move( handler ) );
        }
        /// @brief Register a POST handler with middleware in this group.
        void post( const std::string& pattern, std::vector< middleware_t > mws, handler_t handler )
        {
            parent_.post( prefix_ + pattern, std::move( mws ), std::move( handler ) );
        }
        /// @brief Register a PUT handler with middleware in this group.
        void put( const std::string& pattern, std::vector< middleware_t > mws, handler_t handler )
        {
            parent_.put( prefix_ + pattern, std::move( mws ), std::move( handler ) );
        }
        /// @brief Register a DELETE handler with middleware in this group.
        void del( const std::string& pattern, std::vector< middleware_t > mws, handler_t handler )
        {
            parent_.del( prefix_ + pattern, std::move( mws ), std::move( handler ) );
        }

        /// @brief Register an SSE endpoint in this group.
        void sse( const std::string& path, sse_handler_t handler )
        {
            parent_.sse( prefix_ + path, std::move( handler ) );
        }
        /// @brief Register a WebSocket endpoint in this group.
        void ws( const std::string& path, ws_handler_t handler )
        {
            parent_.ws( prefix_ + path, std::move( handler ) );
        }
        /// @brief Register a chunked streaming endpoint in this group.
        void stream( const std::string& path, stream_handler_t handler,
                     const std::string& content_type = "application/octet-stream" )
        {
            parent_.stream( prefix_ + path, std::move( handler ), content_type );
        }

        /// @brief Create a nested sub-group.
        route_group group( const std::string& sub_prefix )
        {
            return route_group( parent_, prefix_ + sub_prefix );
        }

    private:
        app& parent_;
        std::string prefix_;
    };

    /// @brief Create a route group with a shared URL prefix.
    route_group group( const std::string& prefix )
    {
        return route_group( *this, prefix );
    }

    /// @brief Compile middleware, bind to port, and start serving.
    /// Blocks until SIGINT/SIGTERM. The calling thread runs the acceptor;
    /// worker threads handle connections.
    void listen( server_options opts = {} )
    {
        if( opts.threads <= 0 )
        {
            opts.threads = static_cast< int >( std::thread::hardware_concurrency() );
        }
        if( opts.threads < 1 )
        {
            opts.threads = 1;
        }

        has_middleware_ = !middleware_.empty();
        auto inner = [ this ]( request& r ) -> asio::awaitable< response > {
            co_return co_await dispatch( r );
        };
        compiled_handler_ =
            has_middleware_ ? middleware_.compile( std::move( inner ) ) : std::move( inner );

        try
        {
            asio::io_context main_ctx( 1 );

            std::vector< std::unique_ptr< asio::io_context > > workers;
            std::vector< asio::executor_work_guard< asio::io_context::executor_type > > guards;
            std::vector< std::shared_ptr< std::atomic< int > > > conn_counts;

            for( int i = 0; i < opts.threads; ++i )
            {
                workers.push_back( std::make_unique< asio::io_context >( 1 ) );
                guards.push_back( asio::make_work_guard( workers.back()->get_executor() ) );
                conn_counts.push_back( std::make_shared< std::atomic< int > >( 0 ) );
            }

            auto endpoint =
                asio::ip::tcp::endpoint( asio::ip::make_address( opts.address ), opts.port );

            asio::signal_set signals( main_ctx, SIGINT, SIGTERM );
            auto acceptor = std::make_shared< asio::ip::tcp::acceptor >( main_ctx, endpoint );
            acceptor->set_option( asio::ip::tcp::acceptor::reuse_address( true ) );

            signals.async_wait(
                [ this, acceptor, &guards, &main_ctx, &workers ]( std::error_code, int signo ) {
                std::cout << "\n[lundi] signal " << signo << " — shutting down gracefully...\n";
                shutting_down_.store( true, std::memory_order_relaxed );
                std::error_code ignore;
                acceptor->close( ignore );
                guards.clear();
                for( auto& w : workers )
                {
                    w->stop();
                }
                main_ctx.stop();
            } );

            spawn_date_timer( main_ctx );
            spawn_safe( main_ctx, accept_loop( main_ctx, acceptor, workers, conn_counts, opts ) );

            std::cout << "[lundi] listening on " << opts.address << ":" << opts.port << " ("
            << opts.threads << " worker thread" << ( opts.threads > 1 ? "s" : "" ) << ")\n";

            std::vector< detail::joining_thread > pool;
            pool.reserve( opts.threads );
            for( auto& w : workers )
            {
                pool.emplace_back( [ &ctx = *w ] {
                    ctx.run();
                } );
            }

            main_ctx.run();
            std::cout << "[lundi] server stopped.\n";

        }
        catch ( const std::exception& e )
        {
            std::cerr << "[lundi] fatal: " << e.what() << "\n";
        }
    }

private:
    router router_;
    middleware_chain middleware_;
    next_t compiled_handler_;
    static_file_handler static_files_;
    std::atomic< bool > shutting_down_{ false };
    bool has_middleware_{ false };
    std::unordered_map< std::string, sse_handler_t > sse_routes_;
    std::unordered_map< std::string, ws_handler_t > ws_routes_;

    struct stream_route_entry {
        stream_handler_t handler;
        std::string content_type;
    };
    std::unordered_map< std::string, stream_route_entry > stream_routes_;

    /// @brief Compile per-route middleware into a single handler (called at registration time).
    static handler_t compile_route_handler( std::vector< middleware_t > mws, handler_t handler )
    {
        if( mws.empty() )
        {
            return handler;
        }
        middleware_chain chain;
        for( auto& mw : mws )
        {
            chain.add( std::move( mw ) );
        }
        return chain.compile( std::move( handler ) );
    }

    /// @brief Safe co_spawn wrapper that logs uncaught exceptions.
    template< typename Awaitable >
    static void spawn_safe( asio::io_context& io_ctx, Awaitable&& aw )
    {
        asio::co_spawn( io_ctx, std::forward< Awaitable >( aw ), []( const std::exception_ptr& e ) {
            if( e )
            {
                try
                {
                    std::rethrow_exception( e );
                }
                catch ( const std::exception& ex )
                {
                    std::cerr << "[lundi] uncaught async exception: " << ex.what() << "\n";
                }
            }
        } );
    }

    /// @brief Update the cached HTTP Date header every second.
    static void spawn_date_timer( asio::io_context& ctx )
    {
        auto timer = std::make_shared< asio::steady_timer >( ctx );
        std::function< void() > tick = [ timer, &tick ]() {
            engine::global_date_cache().update();
            timer->expires_after( std::chrono::seconds( 1 ) );
            timer->async_wait( [ &tick ]( std::error_code ec ) {
                if( !ec )
                {
                    tick();
                }
            } );
        };
        tick();
    }

    /// @brief Read from socket until \\r\\n\\r\\n is found or max_size exceeded.
    asio::awaitable< size_t > read_headers( asio::ip::tcp::socket& socket, engine::read_buffer& buffer,
                                            size_t max_size )
    {
        size_t scan_start = buffer.size();

        if( !buffer.empty() )
        {
            auto end = engine::find_header_end( buffer.data(), buffer.size(), 0 );
            if( end > 0 )
            {
                co_return end;
            }
        }

        for(;;)
        {
            if( buffer.size() >= max_size )
            {
                co_return 0;
            }

            buffer.ensure_write( 4096 );
            auto n = co_await socket.async_read_some(
                asio::buffer( buffer.write_pos(), buffer.write_capacity() ), asio::use_awaitable );
            buffer.advance_write( n );

            auto end = engine::find_header_end( buffer.data(), buffer.size(), scan_start );
            if( end > 0 )
            {
                co_return end;
            }

            scan_start = buffer.size();
        }
    }

    /// @brief Read headers with a timeout (first request on a connection only).
    asio::awaitable< size_t > read_headers_with_timeout(
        asio::ip::tcp::socket& socket, engine::read_buffer& buffer,
        size_t max_size,  // NOLINT(bugprone-easily-swappable-parameters)
        int timeout_secs )
    {
        asio::steady_timer timer( socket.get_executor() );
        timer.expires_after( std::chrono::seconds( timeout_secs ) );

        auto result = co_await( read_headers( socket, buffer, max_size ) ||
                                timer.async_wait( asio::use_awaitable ) );

        if( result.index() == 0 )
        {
            co_return std::get< 0 >( result );
        }

        std::error_code ec;
        socket.close( ec );
        co_return 0;
    }

    /// @brief Match the request against registered routes and invoke the handler.
    /// Falls back to HEAD→GET, then static files, then 404.
    asio::awaitable< response > dispatch( request& req )
    {
        auto match = router_.resolve( req.method, req.path, req.params );
        if( match )
        {
            try
            {
                co_return co_await( *match.handler )( req );
            }
            catch ( const std::exception& e )
            {
                co_return response::text( std::string( "Internal Server Error: " ) + e.what(), 500 );
            }
        }

        if( req.method == "HEAD" )
        {
            auto head_match = router_.resolve( "GET", req.path, req.params );
            if( head_match )
            {
                try
                {
                    auto res = co_await( *head_match.handler )( req );
                    res.body.clear();
                    co_return res;
                }
                catch ( const std::exception& e )
                {
                    co_return response::text( "", 500 );
                }
            }
        }

        if( ( req.method == "GET" || req.method == "HEAD" ) && static_files_.has_mounts() )
        {
            auto file_res = static_files_.try_serve( req.path, req );
            if( file_res )
            {
                if( req.method == "HEAD" )
                {
                    file_res->body.clear();
                }
                co_return std::move( *file_res );
            }
        }

        co_return response::text( "Not Found", 404 );
    }

    /// @brief Accept incoming connections and dispatch to the least-loaded worker.
    asio::awaitable< void > accept_loop( asio::io_context& /*main_ctx*/,
                                         std::shared_ptr< asio::ip::tcp::acceptor > acceptor,
                                         std::vector< std::unique_ptr< asio::io_context > >& workers,
                                         std::vector< std::shared_ptr< std::atomic< int > > >& conn_counts,
                                         server_options opts )
    {
        try
        {
            for(;;)
            {
                int best = 0;
                int best_count = conn_counts[ 0 ]->load( std::memory_order_relaxed );
                for( size_t i = 1; i < workers.size(); ++i )
                {
                    int c = conn_counts[ i ]->load( std::memory_order_relaxed );
                    if( c < best_count )
                    {
                        best_count = c;
                        best = static_cast< int >( i );
                    }
                }

                auto& worker_ctx = *workers[ best ];
                auto socket = co_await acceptor->async_accept( worker_ctx, asio::use_awaitable );
                socket.set_option( asio::ip::tcp::no_delay( true ) );

                const auto& counter = conn_counts[ best ];
                counter->fetch_add( 1, std::memory_order_relaxed );
                spawn_safe( worker_ctx, handle_connection( std::move( socket ), opts, counter ) );
            }
        }
        catch ( const asio::system_error& e )
        {
            if( e.code() != asio::error::operation_aborted )
            {
                std::cerr << "[lundi] acceptor error: " << e.what() << "\n";
            }
        }
    }

    /// @brief Handle a single HTTP/1.1 keep-alive connection.
    ///
    /// Lifecycle per request:
    ///   1. Read headers (timeout on first request, plain read on keep-alive)
    ///   2. Parse headers into a request object
    ///   3. Check WebSocket/SSE/chunked upgrade → hand off to protocol handler
    ///   4. Read body if Content-Length present (with timeout)
    ///   5. Dispatch through middleware chain or directly to handler
    ///   6. Serialize response into reusable write_buffer
    ///   7. Consume processed bytes from read_buffer, loop
    asio::awaitable< void > handle_connection( asio::ip::tcp::socket socket, server_options opts,
                                               std::shared_ptr< std::atomic< int > > conn_counter )
    {
        struct conn_guard {
            std::shared_ptr< std::atomic< int > > counter;
            ~conn_guard()
            {
                if( counter )
                {
                    counter->fetch_sub( 1, std::memory_order_relaxed );
                }
            }
        } guard{ conn_counter };

        try
        {
            engine::read_buffer buffer( 8192 );
            engine::write_buffer out_buf( 4096 );
            bool keep_alive = true;
            int request_count = 0;

            while( keep_alive )
            {
                size_t header_end = 0;
                try
                {
                    if( request_count == 0 )
                    {
                        header_end = co_await read_headers_with_timeout(
                            socket, buffer, opts.max_header_size, opts.idle_timeout_seconds );
                    }
                    else
                    {
                        header_end = co_await read_headers( socket, buffer, opts.max_header_size );
                    }
                }
                catch ( const asio::system_error& e )
                {
                    if( e.code() == asio::error::eof || e.code() == asio::error::connection_reset )
                    {
                        break;
                    }
                    throw;
                }
                catch ( ... )    // NOLINT(bugprone-empty-catch)
                {
                    break;
                }

                if( header_end == 0 )
                {
                    break;
                }

                auto req = detail::parse_headers( std::string_view( buffer.data(), header_end ) );
                if( req.method.empty() ) [[unlikely]] {
                    break;
                }

                ++request_count;

                // WebSocket upgrade 
                if( !ws_routes_.empty() && detail::is_ws_upgrade( req ) )
                {
                    auto ws_it = ws_routes_.find( req.path );
                    if( ws_it != ws_routes_.end() )
                    {
                        auto handshake = detail::ws_handshake_response( req );
                        co_await asio::async_write( socket, asio::buffer( handshake ),
                                                    asio::use_awaitable );
                        websocket ws_conn( socket );
                        try
                        {
                            co_await ws_it->second( ws_conn );
                        }
                        catch ( ... )    // NOLINT(bugprone-empty-catch)
                        {
                        }
                        co_return;
                    }
                }

                // SSE 
                if( !sse_routes_.empty() && req.method == "GET" )
                {
                    auto sse_it = sse_routes_.find( req.path );
                    if( sse_it != sse_routes_.end() )
                    {
                        auto headers = detail::sse_initial_headers();
                        co_await asio::async_write( socket, asio::buffer( headers ),
                                                    asio::use_awaitable );
                        sse_stream sse_conn( socket );
                        try
                        {
                            co_await sse_it->second( sse_conn );
                        }
                        catch ( ... )    // NOLINT(bugprone-empty-catch)
                        {
                        }
                        co_return;
                    }
                }

                // Chunked streaming 
                if( !stream_routes_.empty() )
                {
                    auto stream_it = stream_routes_.find( req.path );
                    if( stream_it != stream_routes_.end() )
                    {
                        auto hdrs = detail::chunked_initial_headers( stream_it->second.content_type );
                        co_await asio::async_write( socket, asio::buffer( hdrs ), asio::use_awaitable );
                        try
                        {
                            co_await stream_it->second.handler( req, chunked_writer( socket ) );
                        }
                        catch ( ... )    // NOLINT(bugprone-empty-catch)
                        {
                            break;
                        }
                        co_return;
                    }
                }

                // Normal HTTP 

                bool client_wants_close = req.header_equals( "connection", "close" );
                bool hit_limit = ( opts.max_keepalive_requests > 0 &&
                                   request_count >= opts.max_keepalive_requests );
                bool server_stopping = shutting_down_.load( std::memory_order_relaxed );

                if( client_wants_close || hit_limit || server_stopping )
                {
                    keep_alive = false;
                }

                // Body reading 
                size_t content_length = 0;
                auto* cl_val = req.find_header( "content-length" );
                if( cl_val )
                {
                    auto [ ptr, ec ] = std::from_chars(
                        cl_val->data(), cl_val->data() + cl_val->size(), content_length );

                    if( ec != std::errc{} || content_length > opts.max_body_size ) [[unlikely]] {
                        auto err = response::text( "Payload Too Large", 413 );
                        err.set_header( "Connection", "close" );
                        auto data = err.serialize();
                        co_await asio::async_write( socket, asio::buffer( data ), asio::use_awaitable );
                        break;
                    }

                    size_t body_already = buffer.size() - header_end;
                    size_t body_remaining =
                        content_length > body_already ? content_length - body_already : 0;

                    if( body_remaining > 0 )
                    {
                        buffer.ensure_write( body_remaining );

                        asio::steady_timer body_timer( socket.get_executor() );
                        body_timer.expires_after( std::chrono::seconds( opts.idle_timeout_seconds ) );

                        auto body_result =
                            co_await( asio::async_read(
                                          socket, asio::buffer( buffer.write_pos(), body_remaining ),
                                          asio::use_awaitable ) ||
                                      body_timer.async_wait( asio::use_awaitable ) );

                        if( body_result.index() != 0 )
                        {
                            auto err = response::text( "Request Timeout", 408 );
                            err.set_header( "Connection", "close" );
                            auto data = err.serialize();
                            co_await asio::async_write( socket, asio::buffer( data ),
                                                        asio::use_awaitable );
                            break;
                        }

                        buffer.advance_write( body_remaining );
                    }

                    req.body = std::string( buffer.data() + header_end,
                                            content_length ); // NOLINT(bugprone-dangling-handle)
                }

                // Dispatch 
                auto res =
                    has_middleware_ ? co_await compiled_handler_( req ) : co_await dispatch( req );

                if( !keep_alive )
                {
                    res.set_header( "Connection", "close" );
                }

                // Write response 
                out_buf.clear();
                res.serialize_into( out_buf );
                co_await asio::async_write( socket, asio::buffer( out_buf.data(), out_buf.size() ),
                                            asio::use_awaitable );

                // Consume processed bytes 
                size_t consumed = header_end + content_length;
                if( consumed >= buffer.size() )
                {
                    buffer.clear();
                }
                else
                {
                    buffer.consume( consumed );
                }
            }
        }
        catch ( const std::exception& )    // NOLINT(bugprone-empty-catch)
        {
        }

        std::error_code ec;
        socket.shutdown( asio::ip::tcp::socket::shutdown_both, ec );
        socket.close( ec );
    }
};

}  // namespace lundi

#endif  // LUNDI_SERVER_HPP