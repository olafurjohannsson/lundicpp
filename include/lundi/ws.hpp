#ifndef LUNDI_WS_HPP
#define LUNDI_WS_HPP

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/read.hpp>
#include <asio/use_awaitable.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core.hpp"

namespace lundi
{


// Minimal SHA-1 (RFC 3174) — needed for WebSocket handshake only.
// Not for general cryptographic use.

namespace detail
{

/// @brief Minimal SHA-1 implementation for WebSocket handshake (RFC 3174).
/// @warning Not for general cryptographic use — only used to compute Sec-WebSocket-Accept.
struct sha1 {
    uint32_t state[ 5 ] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                            0xC3D2E1F0 }; ///< Hash state
    uint64_t count = 0;                ///< Bytes processed
    uint8_t buffer[ 64 ]{};              ///< Partial block buffer

    /// @brief Rotate left.
    static uint32_t rol( uint32_t v, int bits )
    {
        return ( v << bits ) | ( v >> ( 32 - bits ) );
    }

    /// @brief Process a single 64-byte block.
    void transform( const uint8_t block[ 64 ] )    // NOLINT(readability-non-const-parameter)
    {
        uint32_t w[ 80 ];
        for( int i = 0; i < 16; i++ )
        {
            auto idx = static_cast< size_t >( i ) * 4;
            w[ i ] = ( uint32_t( block[ idx ] ) << 24 ) | ( uint32_t( block[ idx + 1 ] ) << 16 ) |
                     ( uint32_t( block[ idx + 2 ] ) << 8 ) | block[ idx + 3 ];
        }
        for( int i = 16; i < 80; i++ )
        {
            w[ i ] = rol( w[ i - 3 ] ^ w[ i - 8 ] ^ w[ i - 14 ] ^ w[ i - 16 ], 1 );
        }

        uint32_t a = state[ 0 ], b = state[ 1 ], c = state[ 2 ], d = state[ 3 ], e = state[ 4 ];
        for( int i = 0; i < 80; i++ )
        {
            uint32_t f, k;
            if( i < 20 )
            {
                f = ( b & c ) | ( ( ~b ) & d );
                k = 0x5A827999;
            }
            else if( i < 40 )
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if( i < 60 )
            {
                f = ( b & c ) | ( b & d ) | ( c & d );
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t t = rol( a, 5 ) + f + e + k + w[ i ];
            e = d;
            d = c;
            c = rol( b, 30 );
            b = a;
            a = t;
        }
        state[ 0 ] += a;
        state[ 1 ] += b;
        state[ 2 ] += c;
        state[ 3 ] += d;
        state[ 4 ] += e;
    }

    /// @brief Feed data into the hash.
    void update( const void* data, size_t len )
    {
        auto* p = static_cast< const uint8_t* >( data );
        size_t idx = count % 64;
        count += len;
        for( size_t i = 0; i < len; i++ )
        {
            buffer[ idx++ ] = p[ i ];
            if( idx == 64 )
            {
                transform( buffer );
                idx = 0;
            }
        }
    }

    /// @brief Finalize and return the 20-byte digest.
    std::array< uint8_t, 20 > finalize()
    {
        uint64_t bits = count * 8;
        uint8_t pad = 0x80;
        update( &pad, 1 );
        pad = 0;
        while( count % 64 != 56 )
        {
            update( &pad, 1 );
        }
        uint8_t len_be[ 8 ];
        for( int i = 7; i >= 0; i-- )
        {
            len_be[ i ] = bits & 0xFF;
            bits >>= 8;
        }
        update( len_be, 8 );

        std::array< uint8_t, 20 > digest;
        for( int i = 0; i < 5; i++ )
        {
            auto idx = static_cast< size_t >( i ) * 4;
            digest[ idx ] = ( state[ i ] >> 24 ) & 0xFF;
            digest[ idx + 1 ] = ( state[ i ] >> 16 ) & 0xFF;
            digest[ idx + 2 ] = ( state[ i ] >> 8 ) & 0xFF;
            digest[ idx + 3 ] = state[ i ] & 0xFF;
        }
        return digest;
    }
};

// Base64 encode
inline std::string base64_encode( const uint8_t* data, size_t len )
{
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve( ( len + 2 ) / 3 * 4 );
    for( size_t i = 0; i < len; i += 3 )
    {
        uint32_t n = uint32_t( data[ i ] ) << 16;
        if( i + 1 < len )
        {
            n |= uint32_t( data[ i + 1 ] ) << 8;
        }
        if( i + 2 < len )
        {
            n |= data[ i + 2 ];
        }
        out += table[ ( n >> 18 ) & 0x3F ];
        out += table[ ( n >> 12 ) & 0x3F ];
        out += ( i + 1 < len ) ? table[ ( n >> 6 ) & 0x3F ] : '=';
        out += ( i + 2 < len ) ? table[ n & 0x3F ] : '=';
    }
    return out;
}

// Compute WebSocket accept key from client key
inline std::string ws_accept_key( std::string_view client_key )
{
    static constexpr char magic[] = "258EAFA5-E914-47DA-95CA-5AB964C0FA65";
    sha1 h;
    h.update( client_key.data(), client_key.size() );
    h.update( magic, 36 );
    auto digest = h.finalize();
    return base64_encode( digest.data(), digest.size() );
}

}  // namespace detail


// WebSocket opcodes

enum class ws_opcode : uint8_t {
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    close = 0x8,
    ping = 0x9,
    pong = 0xA,
};


// WebSocket message


/// @brief A complete WebSocket message (text or binary).
struct ws_message {
    ws_opcode opcode;  ///< Message type (text, binary, close, etc.)
    std::string data;  ///< Message payload
    /// @brief True if this is a text message.
    bool is_text() const
    {
        return opcode == ws_opcode::text;
    }
    /// @brief True if this is a binary message.
    bool is_binary() const
    {
        return opcode == ws_opcode::binary;
    }
};


// WebSocket connection — read/write/close


/// @brief WebSocket connection for bidirectional messaging.
///
/// @code
///   server.ws("/chat", [](lundi::websocket ws) -> asio::awaitable<void> {
///       while (auto msg = co_await ws.read()) {
///           co_await ws.send("echo: " + msg->data);
///       }
///   });
/// @endcode
class websocket
{
public:
    /// @brief Maximum payload size before the connection is closed with 1009.
    static constexpr size_t DEFAULT_MAX_MESSAGE_SIZE =
        static_cast< size_t >( 16 ) * 1024 * 1024;  // 16 MB

    /// @param socket           TCP socket (already upgraded via 101 handshake)
    /// @param max_message_size Maximum allowed payload per message (DoS protection)
    explicit websocket( asio::ip::tcp::socket& socket,
                        size_t max_message_size = DEFAULT_MAX_MESSAGE_SIZE )
        : socket_( socket ), max_message_size_( max_message_size )
    {
    }

    /// @brief Read the next message. Returns nullopt on close or error.
    asio::awaitable< std::optional< ws_message > > read()
    {
        try
        {
            uint8_t header[ 2 ];
            co_await asio::async_read( socket_, asio::buffer( header, 2 ), asio::use_awaitable );

            bool fin = ( header[ 0 ] & 0x80 ) != 0;
            auto op = static_cast< ws_opcode >( header[ 0 ] & 0x0F );
            bool masked = ( header[ 1 ] & 0x80 ) != 0;
            uint64_t payload_len = header[ 1 ] & 0x7F;

            if( payload_len == 126 )
            {
                uint8_t ext[ 2 ];
                co_await asio::async_read( socket_, asio::buffer( ext, 2 ), asio::use_awaitable );
                payload_len = ( uint64_t( ext[ 0 ] ) << 8 ) | ext[ 1 ];
            }
            else if( payload_len == 127 )
            {
                uint8_t ext[ 8 ];
                co_await asio::async_read( socket_, asio::buffer( ext, 8 ), asio::use_awaitable );
                payload_len = 0;
                for( int i = 0; i < 8; i++ )
                {
                    payload_len = ( payload_len << 8 ) | ext[ i ];
                }
            }

            if( payload_len > max_message_size_ ) [[unlikely]] {
                co_await close( 1009 );
                co_return std::nullopt;
            }

            uint8_t mask_key[ 4 ] = {};
            if( masked )
            {
                co_await asio::async_read( socket_, asio::buffer( mask_key, 4 ), asio::use_awaitable );
            }

            std::string payload( payload_len, '\0' );
            if( payload_len > 0 )
            {
                co_await asio::async_read( socket_, asio::buffer( payload.data(), payload_len ),
                                           asio::use_awaitable );
            }

            if( masked )
            {
                for( size_t i = 0; i < payload_len; i++ )
                {
                    payload[ i ] =
                        static_cast< char >( static_cast< unsigned char >( payload[ i ] ) ^ mask_key[ i & 3 ] );
                }
            }

            if( op == ws_opcode::ping )
            {
                co_await write_frame( ws_opcode::pong, payload );
                co_return co_await read();
            }

            if( op == ws_opcode::close )
            {
                co_await write_frame( ws_opcode::close, "" );
                co_return std::nullopt;
            }

            if( op == ws_opcode::pong )
            {
                co_return co_await read();
            }

            if( op == ws_opcode::continuation || !fin ) [[unlikely]] {
                co_await close( 1003 );
                co_return std::nullopt;
            }

            co_return ws_message{ op, std::move( payload ) };
        }
        catch ( ... )
        {
            co_return std::nullopt;
        }
    }

    /// @brief Send a text message.
    asio::awaitable< void > send( std::string_view data )
    {
        co_await write_frame( ws_opcode::text, data );
    }

    /// @brief Send a binary message.
    asio::awaitable< void > send_binary( std::string_view data )
    {
        co_await write_frame( ws_opcode::binary, data );
    }

    /// @brief Send a ping frame.
    asio::awaitable< void > ping()
    {
        co_await write_frame( ws_opcode::ping, "" );
    }

    /// @brief Close the connection gracefully.
    /// @param code WebSocket close code (default 1000 = normal closure)
    asio::awaitable< void > close( uint16_t code = 1000 )
    {
        uint8_t payload[ 2 ] = { static_cast< uint8_t >( code >> 8 ), static_cast< uint8_t >( code & 0xFF ) };
        co_await write_frame( ws_opcode::close,
                              std::string_view( reinterpret_cast< char* >( payload ), 2 ) );
    }

    /// @brief Get the socket's executor.
    auto get_executor()
    {
        return socket_.get_executor();
    }

private:
    asio::ip::tcp::socket& socket_;
    size_t max_message_size_;

    asio::awaitable< void > write_frame( ws_opcode op, std::string_view payload )
    {
        std::string frame;
        frame.reserve( 10 + payload.size() );

        frame += static_cast< char >( 0x80 | static_cast< uint8_t >( op ) );

        if( payload.size() < 126 )
        {
            frame += static_cast< char >( payload.size() );
        }
        else if( payload.size() <= 0xFFFF )
        {
            frame += static_cast< char >( 126 );
            frame += static_cast< char >( ( payload.size() >> 8 ) & 0xFF );
            frame += static_cast< char >( payload.size() & 0xFF );
        }
        else
        {
            frame += static_cast< char >( 127 );
            for( int i = 7; i >= 0; i-- )
            {
                frame += static_cast< char >( ( payload.size() >> ( static_cast< size_t >( i ) * 8 ) ) & 0xFF );
            }
        }

        frame += payload;

        co_await asio::async_write( socket_, asio::buffer( frame ), asio::use_awaitable );
    }
};

/// Handler type for WebSocket routes.
using ws_handler_t = std::function< asio::awaitable< void >( websocket ) >;


// WebSocket handshake — validates upgrade headers and sends 101 response

namespace detail
{

inline bool is_ws_upgrade( const request& req )
{
    return req.header_equals( "upgrade", "websocket" ) && !req.header( "sec-websocket-key" ).empty();
}

inline std::string ws_handshake_response( const request& req )
{
    auto client_key = req.header( "sec-websocket-key" );
    auto accept = ws_accept_key( client_key );

    engine::write_buffer buf( 256 );
    buf.append( "HTTP/1.1 101 Switching Protocols\r\n", 34 );
    buf.append( "Upgrade: websocket\r\n", 20 );
    buf.append( "Connection: Upgrade\r\n", 21 );
    buf.append( "Sec-WebSocket-Accept: ", 22 );
    buf.append( accept.data(), accept.size() );
    buf.append( "\r\n\r\n", 4 );
    return std::string( buf.data(), buf.size() );
}

}  // namespace detail

}  // namespace lundi

#endif  // LUNDI_WS_HPP