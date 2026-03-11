#include <lundi/engine/date_cache.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <thread>

using namespace lundi::engine;

TEST_CASE( "date_cache: returns 29 bytes", "[engine][date]" ) {
    date_cache dc;
    CHECK( dc.length() == 29 );
    CHECK( std::strlen( dc.get() ) == 29 );
}

TEST_CASE( "date_cache: ends with GMT", "[engine][date]" ) {
    date_cache dc;
    std::string date( dc.get(), dc.length() );
    CHECK( date.substr( date.size() - 3 ) == "GMT" );
}

TEST_CASE( "date_cache: starts with day name", "[engine][date]" ) {
    date_cache dc;
    std::string date( dc.get(), dc.length() );
    // Day name is 3 chars followed by comma and space
    CHECK( date[ 3 ] == ',' );
    CHECK( date[ 4 ] == ' ' );
}

TEST_CASE( "date_cache: update is idempotent within same second", "[engine][date]" ) {
    date_cache dc;
    std::string first( dc.get(), dc.length() );
    dc.update();
    dc.update();
    dc.update();
    std::string after( dc.get(), dc.length() );
    CHECK( first == after );
}

TEST_CASE( "date_cache: global singleton returns valid date", "[engine][date]" ) {
    auto& dc = global_date_cache();
    dc.update();
    CHECK( dc.length() == 29 );
    std::string date( dc.get(), dc.length() );
    CHECK( date.size() == 29 );
    CHECK( date.substr( date.size() - 3 ) == "GMT" );
}

TEST_CASE( "date_cache: concurrent reads are safe", "[engine][date]" ) {
    auto& dc = global_date_cache();
    dc.update();

    // Multiple threads reading simultaneously shouldn't crash
    std::vector< std::jthread > threads;
    std::atomic< int > success{ 0 };
    for( int i = 0; i < 8; ++i )
    {
        threads.emplace_back( [ & ] {
            for( int j = 0; j < 1000; ++j )
            {
                auto* p = dc.get();
                if( p && std::strlen( p ) == 29 )
                {
                    success.fetch_add( 1, std::memory_order_relaxed );
                }
            }
        } );
    }
    threads.clear();  // join all
    CHECK( success.load() == 8000 );
}