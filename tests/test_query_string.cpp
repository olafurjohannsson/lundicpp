#include <lundi/core.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi::detail;

//find query param by name in vector
static std::string find_query( const std::vector< query_entry >& params, const std::string& name )
{
    for( const auto& q : params )
        if( q.name == name )
            return q.value;
    return "";
}

TEST_CASE( "query_string: basic key=value", "[query]" ) {
    auto params = parse_query_string( "key=value" );
    CHECK( params.size() == 1 );
    CHECK( find_query( params, "key" ) == "value" );
}

TEST_CASE( "query_string: multiple params", "[query]" ) {
    auto params = parse_query_string( "a=1&b=2&c=3" );
    CHECK( params.size() == 3 );
    CHECK( find_query( params, "a" ) == "1" );
    CHECK( find_query( params, "b" ) == "2" );
    CHECK( find_query( params, "c" ) == "3" );
}

TEST_CASE( "query_string: URL-encoded keys and values", "[query]" ) {
    auto params = parse_query_string( "my%20key=my%20value" );
    CHECK( find_query( params, "my key" ) == "my value" );
}

TEST_CASE( "query_string: key with no value", "[query]" ) {
    auto params = parse_query_string( "flag&key=val" );
    CHECK( params.size() == 2 );
    CHECK( find_query( params, "flag" ) == "" );
    CHECK( find_query( params, "key" ) == "val" );
}

TEST_CASE( "query_string: empty string", "[query]" ) {
    auto params = parse_query_string( "" );
    CHECK( params.empty() );
}

TEST_CASE( "query_string: value with equals sign", "[query]" ) {
    auto params = parse_query_string( "expr=a=b" );
    CHECK( find_query( params, "expr" ) == "a=b" );
}

TEST_CASE( "query_string: plus decodes to space in values", "[query]" ) {
    auto params = parse_query_string( "q=hello+world" );
    CHECK( find_query( params, "q" ) == "hello world" );
}