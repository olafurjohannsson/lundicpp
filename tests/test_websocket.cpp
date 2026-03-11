#include <lundi/ws.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace lundi;
using namespace lundi::detail;
using Catch::Matchers::ContainsSubstring;

// RFC 6455 Section 4.2.2 defines a test vector:
// Client key: "dGhlIHNhbXBsZSBub25jZQ=="
// Expected accept: "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="

TEST_CASE( "ws: accept key for known input", "[ws]" ) {
    auto accept = ws_accept_key( "dGhlIHNhbXBsZSBub25jZQ==" );
    CHECK( accept == "vTq0wc03KbKQkeDiHIoEWZ8DhJs=" );
}

TEST_CASE( "ws: accept key different input", "[ws]" ) {
    auto accept = ws_accept_key( "abc123" );
    CHECK( !accept.empty() );
    CHECK( accept.size() == 28 );
}

TEST_CASE( "ws: base64 encode empty", "[ws]" ) {
    auto result = base64_encode( nullptr, 0 );
    CHECK( result.empty() );
}

TEST_CASE( "ws: base64 encode 1 byte", "[ws]" ) {
    uint8_t data[] = { 'A' };
    auto result = base64_encode( data, 1 );
    CHECK( result == "QQ==" );
}

TEST_CASE( "ws: base64 encode 2 bytes", "[ws]" ) {
    uint8_t data[] = { 'A', 'B' };
    auto result = base64_encode( data, 2 );
    CHECK( result == "QUI=" );
}

TEST_CASE( "ws: base64 encode 3 bytes (no padding)", "[ws]" ) {
    uint8_t data[] = { 'A', 'B', 'C' };
    auto result = base64_encode( data, 3 );
    CHECK( result == "QUJD" );
}

TEST_CASE( "ws: base64 encode known string", "[ws]" ) {
    std::string input = "Hello";
    auto result = base64_encode( reinterpret_cast< const uint8_t* >( input.data() ), input.size() );
    CHECK( result == "SGVsbG8=" );
}

TEST_CASE( "ws: sha1 basic", "[ws]" ) {
    sha1 h;
    auto digest = h.finalize();
    CHECK( digest[ 0 ] == 0xda );
    CHECK( digest[ 1 ] == 0x39 );
    CHECK( digest[ 2 ] == 0xa3 );
    CHECK( digest[ 3 ] == 0xee );
}

TEST_CASE( "ws: sha1 hello", "[ws]" ) {
    sha1 h;
    h.update( "Hello", 5 );
    auto digest = h.finalize();
    CHECK( digest[ 0 ] == 0xf7 );
    CHECK( digest[ 1 ] == 0xff );
    CHECK( digest[ 2 ] == 0x9e );
    CHECK( digest[ 3 ] == 0x8b );
}

TEST_CASE( "ws: is_ws_upgrade detects upgrade", "[ws]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "upgrade", "websocket" };
    req.headers_arr[ req.num_headers++ ] = { "sec-websocket-key", "dGhlIHNhbXBsZSBub25jZQ==" };
    CHECK( is_ws_upgrade( req ) );
}

TEST_CASE( "ws: is_ws_upgrade rejects no upgrade header", "[ws]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "sec-websocket-key", "abc" };
    CHECK_FALSE( is_ws_upgrade( req ) );
}

TEST_CASE( "ws: is_ws_upgrade rejects no key", "[ws]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "upgrade", "websocket" };
    CHECK_FALSE( is_ws_upgrade( req ) );
}

TEST_CASE( "ws: is_ws_upgrade rejects wrong upgrade value", "[ws]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "upgrade", "h2c" };
    req.headers_arr[ req.num_headers++ ] = { "sec-websocket-key", "abc" };
    CHECK_FALSE( is_ws_upgrade( req ) );
}

TEST_CASE( "ws: handshake response format", "[ws]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "upgrade", "websocket" };
    req.headers_arr[ req.num_headers++ ] = { "sec-websocket-key", "dGhlIHNhbXBsZSBub25jZQ==" };

    auto resp = ws_handshake_response( req );
    CHECK_THAT( resp, ContainsSubstring( "HTTP/1.1 101 Switching Protocols\r\n" ) );
    CHECK_THAT( resp, ContainsSubstring( "Upgrade: websocket\r\n" ) );
    CHECK_THAT( resp, ContainsSubstring( "Connection: Upgrade\r\n" ) );
    CHECK_THAT( resp, ContainsSubstring( "Sec-WebSocket-Accept: vTq0wc03KbKQkeDiHIoEWZ8DhJs=" ) );
    CHECK( resp.substr( resp.size() - 4 ) == "\r\n\r\n" );
}