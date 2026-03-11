#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// Include just enough to test the writer functions
#include <lundi/engine/buffer.hpp>
#include <lundi/engine/date_cache.hpp>
#include <lundi/engine/fast_itoa.hpp>

// Define the structs locally so we don't need libpq
namespace lundi::bench
{
struct world_row {
    int id;
    int random_number;
};
struct fortune_row {
    int id;
    std::string message;
};
}  // namespace lundi::bench

// We need to provide the struct definitions BEFORE including
#define LUNDI_BENCH_PG_PIPELINE_HPP  // prevent pg_pipeline.hpp from being included
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// Inline the writer functions here to test without libpq dependency
namespace lundi::bench
{

inline void write_world_json( lundi::engine::write_buffer& buf, int id, int random_number )
{
    lundi::engine::fast_itoa itoa;
    buf.append( R"({"id":)", 6 );
    auto* s = itoa.format( static_cast< uint64_t >( id ) );
    buf.append( s, itoa.length() );
    buf.append( R"(,"randomNumber":)", 16 );
    s = itoa.format( static_cast< uint64_t >( random_number ) );
    buf.append( s, itoa.length() );
    buf.push( '}' );
}

inline void write_worlds_json( lundi::engine::write_buffer& buf,
                               const std::vector< world_row >& rows )
{
    buf.push( '[' );
    for( size_t i = 0; i < rows.size(); ++i )
    {
        if( i > 0 )
            buf.push( ',' );
        write_world_json( buf, rows[ i ].id, rows[ i ].random_number );
    }
    buf.push( ']' );
}

inline void html_escape( lundi::engine::write_buffer& buf, std::string_view sv )
{
    for( char c : sv )
    {
        switch( c )
        {
            case '&':
                buf.append( "&amp;", 5 );
                break;
            case '<':
                buf.append( "&lt;", 4 );
                break;
            case '>':
                buf.append( "&gt;", 4 );
                break;
            case '"':
                buf.append( "&quot;", 6 );
                break;
            case '\'':
                buf.append( "&#x27;", 6 );
                break;
            default:
                buf.push( c );
                break;
        }
    }
}

inline void write_fortunes_html( lundi::engine::write_buffer& buf,
                                 std::vector< fortune_row >& fortunes )
{
    fortunes.push_back( { 0, "Additional fortune added at request time." } );
    std::sort( fortunes.begin(), fortunes.end(),
               []( const fortune_row& a, const fortune_row& b ) {
        return a.message < b.message;
    } );
    static constexpr char HEADER[] =
        "<!DOCTYPE html><html><head><title>Fortunes</title></head>"
        "<body><table><tr><th>id</th><th>message</th></tr>";
    static constexpr char FOOTER[] = "</table></body></html>";
    buf.append( HEADER, sizeof( HEADER ) - 1 );
    lundi::engine::fast_itoa itoa;
    for( const auto& f : fortunes )
    {
        buf.append( "<tr><td>", 8 );
        auto* s = itoa.format( static_cast< uint64_t >( f.id ) );
        buf.append( s, itoa.length() );
        buf.append( "</td><td>", 9 );
        html_escape( buf, f.message );
        buf.append( "</td></tr>", 10 );
    }
    buf.append( FOOTER, sizeof( FOOTER ) - 1 );
}

}  // namespace lundi::bench

using namespace lundi::bench;
using namespace lundi::engine;
using Catch::Matchers::ContainsSubstring;

// World JSON serialization

TEST_CASE( "json_writer: single world row", "[bench][json]" ) {
    write_buffer buf( 128 );
    write_world_json( buf, 42, 9999 );
    std::string s( buf.data(), buf.size() );
    CHECK( s == R"({"id":42,"randomNumber":9999})" );
}

TEST_CASE( "json_writer: world row id=1", "[bench][json]" ) {
    write_buffer buf( 128 );
    write_world_json( buf, 1, 1 );
    CHECK( std::string( buf.data(), buf.size() ) == R"({"id":1,"randomNumber":1})" );
}

TEST_CASE( "json_writer: world row max values", "[bench][json]" ) {
    write_buffer buf( 128 );
    write_world_json( buf, 10000, 10000 );
    CHECK( std::string( buf.data(), buf.size() ) == R"({"id":10000,"randomNumber":10000})" );
}

TEST_CASE( "json_writer: worlds array single element", "[bench][json]" ) {
    write_buffer buf( 256 );
    std::vector< world_row > rows = { { 1, 100 } };
    write_worlds_json( buf, rows );
    CHECK( std::string( buf.data(), buf.size() ) == R"([{"id":1,"randomNumber":100}])" );
}

TEST_CASE( "json_writer: worlds array multiple elements", "[bench][json]" ) {
    write_buffer buf( 256 );
    std::vector< world_row > rows = { { 1, 100 }, { 2, 200 }, { 3, 300 } };
    write_worlds_json( buf, rows );
    std::string s( buf.data(), buf.size() );
    CHECK(
        s ==
        R"([{"id":1,"randomNumber":100},{"id":2,"randomNumber":200},{"id":3,"randomNumber":300}])" );
}

TEST_CASE( "json_writer: worlds empty array", "[bench][json]" ) {
    write_buffer buf( 256 );
    std::vector< world_row > rows;
    write_worlds_json( buf, rows );
    CHECK( std::string( buf.data(), buf.size() ) == "[]" );
}

TEST_CASE( "json_writer: worlds array 20 elements (TechEmpower max)", "[bench][json]" ) {
    write_buffer buf( 1024 );
    std::vector< world_row > rows;
    for( int i = 1; i <= 20; ++i )
        rows.push_back( { i, i * 100 } );
    write_worlds_json( buf, rows );
    std::string s( buf.data(), buf.size() );
    CHECK( s.front() == '[' );
    CHECK( s.back() == ']' );
    // Verify first and last entries
    CHECK_THAT( s, ContainsSubstring( R"({"id":1,"randomNumber":100})" ) );
    CHECK_THAT( s, ContainsSubstring( R"({"id":20,"randomNumber":2000})" ) );
    // Count commas — should be 19 for 20 elements
    int commas = 0;
    for( char c : s )
        if( c == ',' )
            ++commas;
    // Each element has an internal comma too (between id and randomNumber)
    // So total commas = 19 (separators) + 20 (internal) = 39
    CHECK( commas == 39 );
}

// HTML entity escaping

TEST_CASE( "html_escape: no special chars", "[bench][html]" ) {
    write_buffer buf( 64 );
    html_escape( buf, "Hello World" );
    CHECK( std::string( buf.data(), buf.size() ) == "Hello World" );
}

TEST_CASE( "html_escape: ampersand", "[bench][html]" ) {
    write_buffer buf( 64 );
    html_escape( buf, "A&B" );
    CHECK( std::string( buf.data(), buf.size() ) == "A&amp;B" );
}

TEST_CASE( "html_escape: angle brackets", "[bench][html]" ) {
    write_buffer buf( 64 );
    html_escape( buf, "<script>alert('xss')</script>" );
    std::string s( buf.data(), buf.size() );
    CHECK_THAT( s, ContainsSubstring( "&lt;script&gt;" ) );
    CHECK_THAT( s, ContainsSubstring( "&lt;/script&gt;" ) );
    CHECK_THAT( s, ContainsSubstring( "&#x27;" ) );  // single quote
}

TEST_CASE( "html_escape: double quotes", "[bench][html]" ) {
    write_buffer buf( 64 );
    html_escape( buf, R"(She said "hello")" );
    CHECK_THAT( std::string( buf.data(), buf.size() ), ContainsSubstring( "&quot;hello&quot;" ) );
}

TEST_CASE( "html_escape: all special chars at once", "[bench][html]" ) {
    write_buffer buf( 128 );
    html_escape( buf, R"(&<>"')" );
    CHECK( std::string( buf.data(), buf.size() ) == "&amp;&lt;&gt;&quot;&#x27;" );
}

TEST_CASE( "html_escape: empty string", "[bench][html]" ) {
    write_buffer buf( 64 );
    html_escape( buf, "" );
    CHECK( buf.size() == 0 );
}


// Fortunes HTML page

TEST_CASE( "fortunes_html: basic structure", "[bench][html]" ) {
    write_buffer buf( 4096 );
    std::vector< fortune_row > fortunes = {
        { 1, "fortune cookie. good luck!" },
        { 2, "A <b>bold</b> prediction." },
    };
    write_fortunes_html( buf, fortunes );
    std::string s( buf.data(), buf.size() );

    CHECK_THAT( s, ContainsSubstring( "<!DOCTYPE html>" ) );
    CHECK_THAT( s, ContainsSubstring( "<title>Fortunes</title>" ) );
    CHECK_THAT( s, ContainsSubstring( "<th>id</th><th>message</th>" ) );
    CHECK_THAT( s, ContainsSubstring( "</table></body></html>" ) );
}

TEST_CASE( "fortunes_html: extra fortune added", "[bench][html]" ) {
    write_buffer buf( 4096 );
    std::vector< fortune_row > fortunes = { { 1, "test" } };
    write_fortunes_html( buf, fortunes );
    std::string s( buf.data(), buf.size() );

    CHECK_THAT( s, ContainsSubstring( "Additional fortune added at request time." ) );
}

TEST_CASE( "fortunes_html: sorted by message", "[bench][html]" ) {
    write_buffer buf( 4096 );
    std::vector< fortune_row > fortunes = {
        { 3, "Zebra" },
        { 1, "Apple" },
        { 2, "Mango" },
    };
    write_fortunes_html( buf, fortunes );
    std::string s( buf.data(), buf.size() );

    auto pos_additional = s.find( "Additional fortune" );
    auto pos_apple = s.find( "Apple" );
    auto pos_mango = s.find( "Mango" );
    auto pos_zebra = s.find( "Zebra" );

    REQUIRE( pos_additional != std::string::npos );
    REQUIRE( pos_apple != std::string::npos );
    REQUIRE( pos_mango != std::string::npos );
    REQUIRE( pos_zebra != std::string::npos );

    // All should appear in sorted order
    CHECK( pos_additional < pos_apple );
    CHECK( pos_apple < pos_mango );
    CHECK( pos_mango < pos_zebra );
}

TEST_CASE( "fortunes_html: XSS escaped", "[bench][html]" ) {
    write_buffer buf( 4096 );
    std::vector< fortune_row > fortunes = {
        { 1, "<script>alert('xss')</script>" },
    };
    write_fortunes_html( buf, fortunes );
    std::string s( buf.data(), buf.size() );

    // Must NOT contain raw script tags
    CHECK( s.find( "<script>" ) == std::string::npos );
    // Must contain escaped version
    CHECK_THAT( s, ContainsSubstring( "&lt;script&gt;" ) );
}