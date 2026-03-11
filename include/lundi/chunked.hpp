#ifndef LUNDI_CHUNKED_HPP
#define LUNDI_CHUNKED_HPP

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>

#include <functional>
#include <string>
#include <string_view>

#include "core.hpp"

namespace lundi
{

/// @brief Chunked transfer-encoding writer for streaming responses.
///
/// Used when the total body size is unknown at response time. Each call
/// to write() sends a framed HTTP chunk. The stream is terminated with
/// a zero-length chunk when the handler returns or finish() is called.
///
/// @code
///   server.stream("/download", [](lundi::request& req, lundi::chunked_writer writer)
///       -> asio::awaitable<void> {
///       co_await writer.write("first chunk");
///       co_await writer.write("second chunk");
///   });
/// @endcode
class chunked_writer
{
public:
    /// @param socket TCP socket for the current connection (must outlive the writer)
    explicit chunked_writer( asio::ip::tcp::socket& socket ) : socket_( socket )
    {
    }

    /// @brief Write a chunk of data, framed as `<hex-length>\r\n<data>\r\n`.
    /// @param data Payload to send (empty data is a no-op, not a terminator)
    asio::awaitable< void > write( std::string_view data )
    {
        if( data.empty() )
            co_return;

        char hex_buf[ 18 ];
        int hex_len = std::snprintf( hex_buf, sizeof( hex_buf ), "%zx\r\n", data.size() );

        std::string frame;
        frame.reserve( hex_len + data.size() + 2 );
        frame.append( hex_buf, hex_len );
        frame.append( data );
        frame.append( "\r\n", 2 );

        co_await asio::async_write( socket_, asio::buffer( frame ), asio::use_awaitable );
    }

    /// @brief Write raw bytes as a chunk.
    asio::awaitable< void > write( const char* data, size_t len )
    {
        co_await write( std::string_view( data, len ) );
    }

    /// @brief Send the terminating zero-length chunk ("0\r\n\r\n"). Idempotent.
    asio::awaitable< void > finish()
    {
        if( !finished_ )
        {
            finished_ = true;
            co_await asio::async_write( socket_, asio::buffer( "0\r\n\r\n", 5 ), asio::use_awaitable );
        }
    }

    /// @brief Sleep between chunks.
    /// @param seconds Duration to wait
    asio::awaitable< void > sleep( int seconds )
    {
        asio::steady_timer timer( socket_.get_executor() );
        timer.expires_after( std::chrono::seconds( seconds ) );
        co_await timer.async_wait( asio::use_awaitable );
    }

    /// @brief Sleep between chunks (millisecond precision).
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

    ~chunked_writer()
    {
        // Note: can't co_await in destructor. The server's handle_connection
        // calls finish() after the handler returns.
    }

    /// @brief True if finish() has been called.
    bool is_finished() const
    {
        return finished_;
    }

private:
    asio::ip::tcp::socket& socket_;
    bool finished_ = false;
};

/// Handler type for chunked streaming routes.
using stream_handler_t = std::function< asio::awaitable< void >( request&, chunked_writer ) >;

/// @brief Build the initial HTTP response headers for a chunked transfer.
namespace detail
{

inline std::string chunked_initial_headers( const std::string& content_type )
{
    engine::write_buffer buf( 256 );
    buf.append( "HTTP/1.1 200 OK\r\n", 17 );
    buf.append( "Server: Lundi\r\nDate: ", 21 );
    auto& dc = engine::global_date_cache();
    buf.append( dc.get(), dc.length() );
    buf.append( "\r\n", 2 );
    buf.append( "Transfer-Encoding: chunked\r\n", 28 );

    static constexpr char CT_PREFIX[] = "Content-Type: ";
    buf.append( CT_PREFIX, sizeof( CT_PREFIX ) - 1 );
    buf.append( content_type.data(), content_type.size() );
    buf.append( "\r\n", 2 );

    buf.append( "Connection: keep-alive\r\n", 24 );
    buf.append( "\r\n", 2 );
    return std::string( buf.data(), buf.size() );
}

}  // namespace detail

}  // namespace lundi

#endif  // LUNDI_CHUNKED_HPP