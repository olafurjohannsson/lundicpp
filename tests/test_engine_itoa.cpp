#include <lundi/engine/fast_itoa.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace lundi::engine;

TEST_CASE( "fast_itoa: zero", "[engine][itoa]" ) {
    fast_itoa itoa;
    CHECK( std::string( itoa.format( 0 ) ) == "0" );
    CHECK( itoa.length() == 1 );
}

TEST_CASE( "fast_itoa: single digit", "[engine][itoa]" ) {
    fast_itoa itoa;
    CHECK( std::string( itoa.format( 7 ) ) == "7" );
}

TEST_CASE( "fast_itoa: typical content-length values", "[engine][itoa]" ) {
    fast_itoa itoa;
    CHECK( std::string( itoa.format( 13 ) ) == "13" );
    CHECK( std::string( itoa.format( 27 ) ) == "27" );
    CHECK( std::string( itoa.format( 4096 ) ) == "4096" );
}

TEST_CASE( "fast_itoa: large number", "[engine][itoa]" ) {
    fast_itoa itoa;
    CHECK( std::string( itoa.format( 1234567890 ) ) == "1234567890" );
    CHECK( itoa.length() == 10 );
}

TEST_CASE( "fast_itoa: max uint64", "[engine][itoa]" ) {
    fast_itoa itoa;
    CHECK( std::string( itoa.format( 18446744073709551615ULL ) ) == "18446744073709551615" );
    CHECK( itoa.length() == 20 );
}

TEST_CASE( "fast_itoa: reuse same instance", "[engine][itoa]" ) {
    fast_itoa itoa;
    CHECK( std::string( itoa.format( 100 ) ) == "100" );
    CHECK( std::string( itoa.format( 200 ) ) == "200" );
    CHECK( std::string( itoa.format( 0 ) ) == "0" );
}
