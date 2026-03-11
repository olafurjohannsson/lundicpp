#ifndef LUNDI_SSE_HPP
#define LUNDI_SSE_HPP

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>

#include <functional>
#include <string>
#include <string_view>

#include "core.hpp"

namespace lundi
{

/// @brief Server-Sent Events stream writer.
///
/// Wraps a TCP socket for sending SSE frames. The connection stays open
/// until the handler coroutine returns or the client disconnects.
///
/// @code
///   server.sse("/events", [](lundi::sse_stream stream) -> asio::awaitable<void> {
///       for (int i = 0; ; ++i) {
///           co_await stream.send(std::to_string(i), "counter");
///           co_await stream.sleep(1);
///       }
///   });
/// @endcode
class sse_stream
{
public:
    /// @param socket TCP socket for the current connection (must outlive the stream)
    explicit sse_stream( asio::ip::tcp::socket& socket ) : socket_( socket )
    {
    }

    /// @brief Send a data-only event.
    /// @param data Event payload (sent as `data: <payload>\n\n`)
    asio::awaitable< void > send( std::string_view data )
    {
        std::string frame = "data: ";
        frame += data;
        frame += "\n\n";
        co_await asio::async_write( socket_, asio::buffer( frame ), asio::use_awaitable );
    }

    /// @brief Send a named event with data.
    /// @param data  Event payload
    /// @param event Event name (sent as `event: <name>\n`)
    asio::awaitable< void > send( std::string_view data, std::string_view event )
    {
        std::string frame;
        frame.reserve( event.size() + data.size() + 20 );
        if( !event.empty() )
        {
            frame += "event: ";
            frame += event;
            frame += "\n";
        }
        frame += "data: ";
        frame += data;
        frame += "\n\n";
        co_await asio::async_write( socket_, asio::buffer( frame ), asio::use_awaitable );
    }

    /// @brief Send an event with name and ID (enables client reconnection via Last-Event-ID).
    /// @param data  Event payload
    /// @param event Event name
    /// @param id    Event ID for client reconnection
    asio::awaitable< void > send( std::string_view data, std::string_view event, std::string_view id )
    {
        std::string frame;
        frame.reserve( event.size() + data.size() + id.size() + 30 );
        if( !id.empty() )
        {
            frame += "id: ";
            frame += id;
            frame += "\n";
        }
        if( !event.empty() )
        {
            frame += "event: ";
            frame += event;
            frame += "\n";
        }
        frame += "data: ";
        frame += data;
        frame += "\n\n";
        co_await asio::async_write( socket_, asio::buffer( frame ), asio::use_awaitable );
    }

    /// @brief Send a comment line to keep the connection alive. Ignored by clients.
    asio::awaitable< void > heartbeat()
    {
        co_await asio::async_write( socket_, asio::buffer( ": heartbeat\n\n", 14 ),
                                    asio::use_awaitable );
    }

    /// @brief Sleep between events.
    /// @param seconds Duration to wait
    asio::awaitable< void > sleep( int seconds )
    {
        asio::steady_timer timer( socket_.get_executor() );
        timer.expires_after( std::chrono::seconds( seconds ) );
        co_await timer.async_wait( asio::use_awaitable );
    }

    /// @brief Sleep between events (millisecond precision).
    /// @param ms Duration to wait in milliseconds
    asio::awaitable< void > sleep_ms( int ms )
    {
        asio::steady_timer timer( socket_.get_executor() );
        timer.expires_after( std::chrono::milliseconds( ms ) );
        co_await timer.async_wait( asio::use_awaitable );
    }

    /// @brief Get the socket's executor (for creating timers or spawning work).
    auto get_executor()
    {
        return socket_.get_executor();
    }

private:
    asio::ip::tcp::socket& socket_;
};

/// Handler type for SSE routes.
using sse_handler_t = std::function< asio::awaitable< void >( sse_stream ) >;

/// @brief Build the initial HTTP response headers for an SSE stream.
namespace detail
{

inline std::string sse_initial_headers()
{
    engine::write_buffer buf( 256 );
    static constexpr char STATUS[] = "HTTP/1.1 200 OK\r\n";
    static constexpr char SERVER[] = "Server: Lundi\r\nDate: ";
    static constexpr char CT[] = "Content-Type: text/event-stream\r\n";
    static constexpr char CC[] = "Cache-Control: no-cache\r\n";
    static constexpr char CONN[] = "Connection: keep-alive\r\n";
    static constexpr char CORS[] = "Access-Control-Allow-Origin: *\r\n";

    buf.append( STATUS, sizeof( STATUS ) - 1 );
    buf.append( SERVER, sizeof( SERVER ) - 1 );
    auto& dc = engine::global_date_cache();
    buf.append( dc.get(), dc.length() );
    buf.append( "\r\n", 2 );
    buf.append( CT, sizeof( CT ) - 1 );
    buf.append( CC, sizeof( CC ) - 1 );
    buf.append( CONN, sizeof( CONN ) - 1 );
    buf.append( CORS, sizeof( CORS ) - 1 );
    buf.append( "\r\n", 2 );
    return std::string( buf.data(), buf.size() );
}

}  // namespace detail

}  // namespace lundi

#endif  // LUNDI_SSE_HPP