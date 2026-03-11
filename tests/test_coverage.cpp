#include <lundi/core.hpp>
#include <lundi/engine/buffer.hpp>
#include <lundi/engine/header_scan.hpp>
#include <lundi/middleware.hpp>
#include <lundi/router.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace lundi;
using namespace lundi::engine;
using Catch::Matchers::ContainsSubstring;


TEST_CASE( "handler_fn: default constructed is falsy", "[handler_fn]" ) {
    handler_fn< int( int ) > fn;
    CHECK_FALSE( fn );
}

TEST_CASE( "handler_fn: constructed from lambda is truthy", "[handler_fn]" ) {
    handler_fn< int( int ) > fn( []( int x ) {
        return x * 2;
    } );
    CHECK( fn );
}

TEST_CASE( "handler_fn: invokes correctly", "[handler_fn]" ) {
    handler_fn< int( int ) > fn( []( int x ) {
        return x + 10;
    } );
    CHECK( fn( 5 ) == 15 );
}

TEST_CASE( "handler_fn: captures state", "[handler_fn]" ) {
    int multiplier = 3;
    handler_fn< int( int ) > fn( [ multiplier ]( int x ) {
        return x * multiplier;
    } );
    CHECK( fn( 7 ) == 21 );
}

TEST_CASE( "handler_fn: move constructor transfers ownership", "[handler_fn]" ) {
    handler_fn< int( int ) > a( []( int x ) {
        return x;
    } );
    CHECK( a );

    handler_fn< int( int ) > b( std::move( a ) );
    CHECK( b );
    CHECK_FALSE( a );
    CHECK( b( 42 ) == 42 );
}

TEST_CASE( "handler_fn: move assignment transfers ownership", "[handler_fn]" ) {
    handler_fn< int( int ) > a( []( int x ) {
        return x + 1;
    } );
    handler_fn< int( int ) > b( []( int x ) {
        return x + 2;
    } );

    b = std::move( a );
    CHECK( b );
    CHECK_FALSE( a );
    CHECK( b( 10 ) == 11 );
}

TEST_CASE( "handler_fn: self move assignment is safe", "[handler_fn]" ) {
    handler_fn< int( int ) > fn( []( int x ) {
        return x;
    } );
    auto* ptr = &fn;
    fn = std::move( *ptr );
    CHECK( fn );
    CHECK( fn( 5 ) == 5 );
}

TEST_CASE( "handler_fn: destructor cleans up heap allocation", "[handler_fn]" ) {
    static int destructor_count = 0;
    struct Counter {
        int val;
        Counter( int v ) : val( v )
        {
        }
        Counter( const Counter& o ) : val( o.val )
        {
        }
        ~Counter()
        {
            destructor_count++;
        }
        int operator()( int x ) const
        {
            return x + val;
        }
    };

    destructor_count = 0;
    {
        handler_fn< int( int ) > fn( Counter{ 100 } );
    }
    CHECK( destructor_count > 0 );
}

TEST_CASE( "handler_fn: works with void return", "[handler_fn]" ) {
    int called = 0;
    handler_fn< void() > fn( [ &called ]() {
        called++;
    } );
    fn();
    CHECK( called == 1 );
}

TEST_CASE( "handler_fn: works with string return", "[handler_fn]" ) {
    handler_fn< std::string( int ) > fn( []( int x ) {
        return std::to_string( x );
    } );
    CHECK( fn( 42 ) == "42" );
}

TEST_CASE( "handler_fn: works with multiple args", "[handler_fn]" ) {
    handler_fn< int( int, int ) > fn( []( int a, int b ) {
        return a + b;
    } );
    CHECK( fn( 3, 4 ) == 7 );
}

TEST_CASE( "find_header_end: finds terminator at start", "[engine][scan]" ) {
    const char* data = "\r\n\r\nBody";
    auto pos = find_header_end( data, 8, 0 );
    CHECK( pos == 4 );
}

TEST_CASE( "find_header_end: finds terminator after headers", "[engine][scan]" ) {
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\nBody";
    auto pos = find_header_end( raw.data(), raw.size(), 0 );
    CHECK( pos > 0 );
    CHECK( raw.substr( pos ) == "Body" );
}

TEST_CASE( "find_header_end: returns 0 when not found", "[engine][scan]" ) {
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\n";
    auto pos = find_header_end( raw.data(), raw.size(), 0 );
    CHECK( pos == 0 );
}

TEST_CASE( "find_header_end: scan_start skips already scanned bytes", "[engine][scan]" ) {
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    // Scan from offset 10 — should still find the terminator
    auto pos = find_header_end( raw.data(), raw.size(), 10 );
    CHECK( pos > 0 );
}

TEST_CASE( "find_header_end: scan_start past terminator misses it", "[engine][scan]" ) {
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\nBody";
    size_t term_pos = raw.find( "\r\n\r\n" );
    // Start scanning after the terminator
    auto pos = find_header_end( raw.data(), raw.size(), term_pos + 4 );
    CHECK( pos == 0 );
}

TEST_CASE( "find_header_end: too short to contain terminator", "[engine][scan]" ) {
    CHECK( find_header_end( "abc", 3, 0 ) == 0 );
    CHECK( find_header_end( "\r\n\r", 3, 0 ) == 0 );
}

TEST_CASE( "find_header_end: exactly 4 bytes is the terminator", "[engine][scan]" ) {
    auto pos = find_header_end( "\r\n\r\n", 4, 0 );
    CHECK( pos == 4 );
}

TEST_CASE( "find_header_end: large buffer with terminator near end", "[engine][scan]" ) {
    std::string big( 4096, 'X' );
    big += "\r\n\r\n";
    auto pos = find_header_end( big.data(), big.size(), 0 );
    CHECK( pos == 4100 );
}

TEST_CASE( "find_header_end: incremental scanning simulates socket reads", "[engine][scan]" ) {
    // First chunk: partial headers
    std::string chunk1 = "GET / HTTP/1.1\r\nHost: x";
    auto pos = find_header_end( chunk1.data(), chunk1.size(), 0 );
    CHECK( pos == 0 );

    // Second chunk arrives, append and scan from where we left off
    std::string full = chunk1 + "\r\n\r\nBody";
    pos = find_header_end( full.data(), full.size(), chunk1.size() );
    CHECK( pos > 0 );
    CHECK( full.substr( pos ) == "Body" );
}


// mw() helper — variadic middleware vector construction
TEST_CASE( "mw: empty call returns empty vector", "[middleware][mw]" ) {
    auto v = mw();
    CHECK( v.empty() );
}

TEST_CASE( "mw: single middleware", "[middleware][mw]" ) {
    auto v = mw( []( request& req, next_t& next ) -> asio::awaitable< response > {
        co_return co_await next( req );
    } );
    CHECK( v.size() == 1 );
}

TEST_CASE( "mw: multiple middlewares", "[middleware][mw]" ) {
    auto v = mw( []( request& req,
                     next_t& next ) -> asio::awaitable< response > {
        co_return co_await next( req );
    },
                 []( request& req, next_t& next ) -> asio::awaitable< response > {
        co_return co_await next( req );
    } );
    CHECK( v.size() == 2 );
}


// response::set_header
TEST_CASE( "set_header: adds header", "[response][header]" ) {
    response r;
    r.set_header( "X-Custom", "value" );
    CHECK( r.num_headers == 1 );
    CHECK( r.headers[ 0 ].name == "X-Custom" );
    CHECK( r.headers[ 0 ].value == "value" );
}

TEST_CASE( "set_header: multiple headers accumulate", "[response][header]" ) {
    response r;
    r.set_header( "X-First", "1" );
    r.set_header( "X-Second", "2" );
    r.set_header( "X-Third", "3" );
    CHECK( r.num_headers == 3 );
}

TEST_CASE( "set_header: appears in serialized output", "[response][header]" ) {
    auto r = response::text( "ok" );
    r.set_header( "X-Request-Id", "abc123" );
    auto s = r.serialize();
    CHECK_THAT( s, ContainsSubstring( "X-Request-Id: abc123\r\n" ) );
}


// header_equals edge cases
TEST_CASE( "header_equals: mismatched length returns false", "[request][header]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "content-type", "text/html" };
    CHECK_FALSE( req.header_equals( "content-type", "text/html; charset=utf-8" ) );
}

TEST_CASE( "header_equals: empty value matches empty header", "[request][header]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "x-empty", "" };
    CHECK( req.header_equals( "x-empty", "" ) );
}

TEST_CASE( "header_equals: missing header returns false", "[request][header]" ) {
    request req;
    CHECK_FALSE( req.header_equals( "nonexistent", "anything" ) );
}

TEST_CASE( "header_equals: case-insensitive value comparison", "[request][header]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "upgrade", "WebSocket" };
    CHECK( req.header_equals( "upgrade", "websocket" ) );
    CHECK( req.header_equals( "upgrade", "WEBSOCKET" ) );
    CHECK( req.header_equals( "upgrade", "WebSocket" ) );
}

TEST_CASE( "header_equals: case-insensitive name lookup", "[request][header]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "Content-Type", "text/plain" };
    CHECK( req.header_equals( "content-type", "text/plain" ) );
    CHECK( req.header_equals( "CONTENT-TYPE", "text/plain" ) );
}


// read_buffer pipelining simulation
TEST_CASE( "read_buffer: consume with leftover simulates pipelining", "[engine][buffer]" ) {
    read_buffer buf( 1024 );

    // Simulate two pipelined requests arriving together
    std::string two_requests =
        "GET /first HTTP/1.1\r\nHost: x\r\n\r\n"
        "GET /second HTTP/1.1\r\nHost: x\r\n\r\n";

    std::memcpy( buf.write_pos(), two_requests.data(), two_requests.size() );
    buf.advance_write( two_requests.size() );

    // Find first request's end
    auto first_end = buf.find_header_end();
    REQUIRE( first_end != nullptr );
    size_t first_len = first_end - buf.data();

    // Consume first request
    buf.consume( first_len );

    // Second request should still be there
    CHECK( buf.size() > 0 );
    auto second_end = buf.find_header_end();
    CHECK( second_end != nullptr );
}

TEST_CASE( "read_buffer: consume entire buffer resets cleanly", "[engine][buffer]" ) {
    read_buffer buf( 256 );
    std::string data = "Hello";
    std::memcpy( buf.write_pos(), data.data(), data.size() );
    buf.advance_write( data.size() );

    buf.consume( data.size() );
    CHECK( buf.size() == 0 );
    CHECK( buf.empty() );
}

TEST_CASE( "read_buffer: consume partial leaves remainder", "[engine][buffer]" ) {
    read_buffer buf( 256 );
    std::string data = "AABBCC";
    std::memcpy( buf.write_pos(), data.data(), data.size() );
    buf.advance_write( data.size() );

    buf.consume( 2 );
    CHECK( buf.size() == 4 );
    CHECK( std::string_view( buf.data(), buf.size() ) == "BBCC" );
}

TEST_CASE( "read_buffer: ensure_write grows buffer", "[engine][buffer]" ) {
    read_buffer buf( 16 );
    CHECK( buf.write_capacity() == 16 );

    buf.ensure_write( 1024 );
    CHECK( buf.write_capacity() >= 1024 );
}

TEST_CASE( "read_buffer: clear resets but capacity remains", "[engine][buffer]" ) {
    read_buffer buf( 256 );
    std::string data = "test data";
    std::memcpy( buf.write_pos(), data.data(), data.size() );
    buf.advance_write( data.size() );

    buf.clear();
    CHECK( buf.size() == 0 );
    CHECK( buf.empty() );
    // Can still write after clear
    CHECK( buf.write_capacity() >= 256 );
}


// write_buffer consume

TEST_CASE( "write_buffer: consume shifts data forward", "[engine][buffer]" ) {
    write_buffer buf( 256 );
    buf.append( "AABBCC", 6 );
    buf.consume( 2 );
    CHECK( buf.size() == 4 );
    CHECK( std::string_view( buf.data(), buf.size() ) == "BBCC" );
}

TEST_CASE( "write_buffer: consume entire clears", "[engine][buffer]" ) {
    write_buffer buf( 256 );
    buf.append( "test", 4 );
    buf.consume( 4 );
    CHECK( buf.size() == 0 );
    CHECK( buf.empty() );
}

TEST_CASE( "write_buffer: consume more than size clears", "[engine][buffer]" ) {
    write_buffer buf( 256 );
    buf.append( "ab", 2 );
    buf.consume( 100 );
    CHECK( buf.size() == 0 );
}

TEST_CASE( "write_buffer: direct write with ensure and advance", "[engine][buffer]" ) {
    write_buffer buf( 256 );
    buf.ensure( 5 );
    std::memcpy( buf.write_pos(), "hello", 5 );
    buf.advance( 5 );
    CHECK( buf.size() == 5 );
    CHECK( std::string_view( buf.data(), buf.size() ) == "hello" );
}