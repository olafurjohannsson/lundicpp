#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "lundi/static_files.hpp"

//create a temp directory with files for testing
struct test_dir {
    std::filesystem::path root;

    test_dir()
    {
        root = std::filesystem::temp_directory_path() / "lundi_static_test";
        std::filesystem::create_directories( root );
        std::filesystem::create_directories( root / "css" );
        std::filesystem::create_directories( root / "js" );
        std::filesystem::create_directories( root / "sub" );

        write( "index.html", "<html><body>hello</body></html>" );
        write( "style.css", "body { margin: 0; }" );
        write( "css/app.css", ".app { display: flex; }" );
        write( "js/main.js", "console.log('hello');" );
        write( "data.json", R"({"key":"value"})" );
        write( "photo.png", "fake-png-data" );
        write( "sub/index.html", "<html>sub</html>" );
        write( "sub/page.html", "<html>page</html>" );
    }

    ~test_dir()
    {
        std::filesystem::remove_all( root );
    }

    void write( const std::string& rel, const std::string& content )
    {
        std::ofstream f( root / rel, std::ios::binary );
        f << content;
    }

    std::string path() const
    {
        return root.string();
    }
};

//build a minimal request with optional headers
lundi::request make_req( const std::string& path_str,
                         const std::string& header_name = "",
                         const std::string& header_value = "" )
{
    lundi::request req;
    req.path = path_str;
    req.method = "GET";
    if( !header_name.empty() )
    {
        static std::string name_storage;
        static std::string value_storage;
        name_storage = header_name;
        value_storage = header_value;
        req.headers_arr[ 0 ] = { name_storage, value_storage };
        req.num_headers = 1;
    }
    return req;
}

TEST_CASE( "MIME detection for common web files", "[static]" )
{
    using lundi::detail::mime_for_extension;

    CHECK( mime_for_extension( ".html" ) == "text/html; charset=utf-8" );
    CHECK( mime_for_extension( ".htm" ) == "text/html; charset=utf-8" );
    CHECK( mime_for_extension( ".css" ) == "text/css; charset=utf-8" );
    CHECK( mime_for_extension( ".js" ) == "application/javascript; charset=utf-8" );
    CHECK( mime_for_extension( ".mjs" ) == "application/javascript; charset=utf-8" );
    CHECK( mime_for_extension( ".json" ) == "application/json; charset=utf-8" );
    CHECK( mime_for_extension( ".png" ) == "image/png" );
    CHECK( mime_for_extension( ".jpg" ) == "image/jpeg" );
    CHECK( mime_for_extension( ".jpeg" ) == "image/jpeg" );
    CHECK( mime_for_extension( ".gif" ) == "image/gif" );
    CHECK( mime_for_extension( ".svg" ) == "image/svg+xml" );
    CHECK( mime_for_extension( ".ico" ) == "image/x-icon" );
    CHECK( mime_for_extension( ".webp" ) == "image/webp" );
    CHECK( mime_for_extension( ".woff2" ) == "font/woff2" );
    CHECK( mime_for_extension( ".woff" ) == "font/woff" );
    CHECK( mime_for_extension( ".ttf" ) == "font/ttf" );
    CHECK( mime_for_extension( ".wasm" ) == "application/wasm" );
    CHECK( mime_for_extension( ".pdf" ) == "application/pdf" );
    CHECK( mime_for_extension( ".xyz" ) == "application/octet-stream" );
}

TEST_CASE( "Serve files from root mount", "[static]" )
{
    test_dir dir;
    lundi::static_file_handler handler;
    handler.mount( "/", dir.path() );

    SECTION( "Serve index.html at root" )
    {
        auto req = make_req( "/" );
        auto res = handler.try_serve( "/", req );
        REQUIRE( res.has_value() );
        CHECK( res->status == 200 );
        CHECK( res->body == "<html><body>hello</body></html>" );
    }

    SECTION( "Serve CSS file" )
    {
        auto req = make_req( "/style.css" );
        auto res = handler.try_serve( "/style.css", req );
        REQUIRE( res.has_value() );
        CHECK( res->status == 200 );
        CHECK( res->body == "body { margin: 0; }" );
    }

    SECTION( "Serve nested CSS file" )
    {
        auto req = make_req( "/css/app.css" );
        auto res = handler.try_serve( "/css/app.css", req );
        REQUIRE( res.has_value() );
        CHECK( res->body == ".app { display: flex; }" );
    }

    SECTION( "Serve JS file" )
    {
        auto req = make_req( "/js/main.js" );
        auto res = handler.try_serve( "/js/main.js", req );
        REQUIRE( res.has_value() );
        CHECK( res->body == "console.log('hello');" );
    }

    SECTION( "Serve JSON file" )
    {
        auto req = make_req( "/data.json" );
        auto res = handler.try_serve( "/data.json", req );
        REQUIRE( res.has_value() );
        CHECK( res->body == R"({"key":"value"})" );
    }

    SECTION( "Subdirectory index.html" )
    {
        auto req = make_req( "/sub/" );
        auto res = handler.try_serve( "/sub/", req );
        REQUIRE( res.has_value() );
        CHECK( res->body == "<html>sub</html>" );
    }

    SECTION( "File not found returns nullopt" )
    {
        auto req = make_req( "/nope.html" );
        auto res = handler.try_serve( "/nope.html", req );
        CHECK( !res.has_value() );
    }
}

TEST_CASE( "Multiple mount points", "[static]" )
{
    test_dir dir;
    lundi::static_file_handler handler;

    // Create a second dir
    auto dir2_path = std::filesystem::temp_directory_path() / "lundi_static_test2";
    std::filesystem::create_directories( dir2_path );
    {
        std::ofstream f( dir2_path / "asset.js", std::ios::binary );
        f << "var x = 1;";
    }

    handler.mount( "/static/", dir.path() );
    handler.mount( "/assets/", dir2_path.string() );

    SECTION( "First mount serves its files" )
    {
        auto req = make_req( "/static/style.css" );
        auto res = handler.try_serve( "/static/style.css", req );
        REQUIRE( res.has_value() );
        CHECK( res->body == "body { margin: 0; }" );
    }

    SECTION( "Second mount serves its files" )
    {
        auto req = make_req( "/assets/asset.js" );
        auto res = handler.try_serve( "/assets/asset.js", req );
        REQUIRE( res.has_value() );
        CHECK( res->body == "var x = 1;" );
    }

    SECTION( "Wrong prefix returns nullopt" )
    {
        auto req = make_req( "/other/style.css" );
        auto res = handler.try_serve( "/other/style.css", req );
        CHECK( !res.has_value() );
    }

    std::filesystem::remove_all( dir2_path );
}

TEST_CASE( "Path traversal is blocked", "[static]" )
{
    test_dir dir;
    lundi::static_file_handler handler;
    handler.mount( "/", dir.path() );

    SECTION( "Double dot in path" )
    {
        auto req = make_req( "/../etc/passwd" );
        auto res = handler.try_serve( "/../etc/passwd", req );
        CHECK( !res.has_value() );
    }

    SECTION( "Encoded double dot (already decoded by parser)" )
    {
        auto req = make_req( "/%2e%2e/etc/passwd" );
        // After URL decode this becomes /../etc/passwd
        // The ".." check catches it
        auto res = handler.try_serve( "/../etc/passwd", req );
        CHECK( !res.has_value() );
    }

    SECTION( "Mid-path traversal" )
    {
        auto req = make_req( "/sub/../../etc/passwd" );
        auto res = handler.try_serve( "/sub/../../etc/passwd", req );
        CHECK( !res.has_value() );
    }
}

TEST_CASE( "ETag and Last-Modified headers are set", "[static]" )
{
    test_dir dir;
    lundi::static_file_handler handler;
    handler.mount( "/", dir.path(), { .max_age = 3600, .etag = true, .last_modified = true } );

    auto req = make_req( "/style.css" );
    auto res = handler.try_serve( "/style.css", req );
    REQUIRE( res.has_value() );
    CHECK( res->status == 200 );

    // Check that caching headers exist
    bool has_etag = false;
    bool has_last_modified = false;
    bool has_cache_control = false;
    std::string etag_value;

    for( size_t i = 0; i < res->num_headers; ++i )
    {
        if( res->headers[ i ].name == "ETag" )
        {
            has_etag = true;
            etag_value = res->headers[ i ].value;
        }
        if( res->headers[ i ].name == "Last-Modified" )
        {
            has_last_modified = true;
        }
        if( res->headers[ i ].name == "Cache-Control" )
        {
            has_cache_control = true;
            CHECK( res->headers[ i ].value == "public, max-age=3600" );
        }
    }
    CHECK( has_etag );
    CHECK( has_last_modified );
    CHECK( has_cache_control );

    SECTION( "304 on matching ETag" )
    {
        auto req2 = make_req( "/style.css", "If-None-Match", etag_value );
        auto res2 = handler.try_serve( "/style.css", req2 );
        REQUIRE( res2.has_value() );
        CHECK( res2->status == 304 );
        CHECK( res2->body.empty() );
    }
}

TEST_CASE( "Cache-Control: no-cache when max_age is 0", "[static]" )
{
    test_dir dir;
    lundi::static_file_handler handler;
    handler.mount( "/", dir.path(), { .max_age = 0 } );

    auto req = make_req( "/style.css" );
    auto res = handler.try_serve( "/style.css", req );
    REQUIRE( res.has_value() );

    bool found_no_cache = false;
    for( size_t i = 0; i < res->num_headers; ++i )
    {
        if( res->headers[ i ].name == "Cache-Control" && res->headers[ i ].value == "no-cache" )
        {
            found_no_cache = true;
        }
    }
    CHECK( found_no_cache );
}

TEST_CASE( "ETag disabled", "[static]" )
{
    test_dir dir;
    lundi::static_file_handler handler;
    handler.mount( "/", dir.path(), { .etag = false } );

    auto req = make_req( "/style.css" );
    auto res = handler.try_serve( "/style.css", req );
    REQUIRE( res.has_value() );

    bool has_etag = false;
    for( size_t i = 0; i < res->num_headers; ++i )
    {
        if( res->headers[ i ].name == "ETag" )
        {
            has_etag = true;
        }
    }
    CHECK( !has_etag );
}

TEST_CASE( "Index file disabled", "[static]" )
{
    test_dir dir;
    lundi::static_file_handler handler;
    handler.mount( "/", dir.path(), { .index = "" } );

    auto req = make_req( "/" );
    auto res = handler.try_serve( "/", req );
    CHECK( !res.has_value() );  // No index fallback
}

TEST_CASE( "Correct Content-Type headers", "[static]" )
{
    test_dir dir;
    lundi::static_file_handler handler;
    handler.mount( "/", dir.path() );

    auto check_ct = [ & ]( const std::string& path, const std::string& expected_ct )
    {
        auto req = make_req( path );
        auto res = handler.try_serve( path, req );
        REQUIRE( res.has_value() );
        for( size_t i = 0; i < res->num_headers; ++i )
        {
            if( res->headers[ i ].name == "Content-Type" )
            {
                CHECK( res->headers[ i ].value == expected_ct );
                return;
            }
        }
        FAIL( "No Content-Type header for " + path );
    };

    check_ct( "/index.html", "text/html; charset=utf-8" );
    check_ct( "/style.css", "text/css; charset=utf-8" );
    check_ct( "/js/main.js", "application/javascript; charset=utf-8" );
    check_ct( "/data.json", "application/json; charset=utf-8" );
    check_ct( "/photo.png", "image/png" );
}