#include <lundi/engine/buffer.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi::engine;

TEST_CASE( "write_buffer: append and read back", "[engine][buffer]" ) {
    write_buffer buf( 64 );
    buf.append( "hello", 5 );
    CHECK( buf.size() == 5 );
    CHECK( std::string_view( buf.data(), buf.size() ) == "hello" );
}

TEST_CASE( "write_buffer: clear resets size but not capacity", "[engine][buffer]" ) {
    write_buffer buf( 64 );
    buf.append( "hello", 5 );
    buf.clear();
    CHECK( buf.size() == 0 );
    CHECK( buf.empty() );
    // Can write again without reallocation
    buf.append( "world", 5 );
    CHECK( std::string_view( buf.data(), buf.size() ) == "world" );
}

TEST_CASE( "write_buffer: grows beyond initial capacity", "[engine][buffer]" ) {
    write_buffer buf( 8 );
    buf.append( "this is more than eight bytes", 29 );
    CHECK( buf.size() == 29 );
    CHECK( std::string_view( buf.data(), buf.size() ) == "this is more than eight bytes" );
}

TEST_CASE( "write_buffer: push single byte", "[engine][buffer]" ) {
    write_buffer buf( 64 );
    buf.push( 'A' );
    buf.push( 'B' );
    CHECK( buf.size() == 2 );
    CHECK( buf.data()[ 0 ] == 'A' );
    CHECK( buf.data()[ 1 ] == 'B' );
}

TEST_CASE( "write_buffer: multiple appends accumulate", "[engine][buffer]" ) {
    write_buffer buf( 64 );
    buf.append( "aaa", 3 );
    buf.append( "bbb", 3 );
    buf.append( "ccc", 3 );
    CHECK( buf.size() == 9 );
    CHECK( std::string_view( buf.data(), 9 ) == "aaabbbccc" );
}

TEST_CASE( "read_buffer: write and consume", "[engine][buffer]" ) {
    read_buffer buf( 64 );
    std::memcpy( buf.write_pos(), "hello\r\n\r\nworld", 14 );
    buf.advance_write( 14 );
    CHECK( buf.size() == 14 );

    auto* end = buf.find_header_end();
    REQUIRE( end != nullptr );
    size_t header_len = end - buf.data();
    CHECK( header_len == 9 );  // "hello\r\n\r\n" = 9 bytes

    buf.consume( header_len );
    CHECK( buf.size() == 5 );
    CHECK( std::string_view( buf.data(), buf.size() ) == "world" );
}

TEST_CASE( "read_buffer: find_header_end returns null if incomplete", "[engine][buffer]" ) {
    read_buffer buf( 64 );
    std::memcpy( buf.write_pos(), "GET / HTTP/1.1\r\nHost: x\r\n", 25 );
    buf.advance_write( 25 );
    CHECK( buf.find_header_end() == nullptr );
}
