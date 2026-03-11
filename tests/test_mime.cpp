#include <lundi/core.hpp>
#include <lundi/server.hpp>

#include <catch2/catch_test_macros.hpp>

static std::string test_mime_for( const std::string& path )
{
    auto ext = std::filesystem::path( path ).extension().string();
    std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );
    if( ext == ".html" || ext == ".htm" )
        return "text/html; charset=utf-8";
    if( ext == ".css" )
        return "text/css; charset=utf-8";
    if( ext == ".js" )
        return "application/javascript; charset=utf-8";
    if( ext == ".json" )
        return "application/json; charset=utf-8";
    if( ext == ".png" )
        return "image/png";
    if( ext == ".jpg" || ext == ".jpeg" )
        return "image/jpeg";
    if( ext == ".gif" )
        return "image/gif";
    if( ext == ".svg" )
        return "image/svg+xml";
    if( ext == ".ico" )
        return "image/x-icon";
    if( ext == ".woff2" )
        return "font/woff2";
    if( ext == ".woff" )
        return "font/woff";
    if( ext == ".wasm" )
        return "application/wasm";
    if( ext == ".txt" )
        return "text/plain; charset=utf-8";
    if( ext == ".xml" )
        return "application/xml; charset=utf-8";
    return "application/octet-stream";
}

TEST_CASE( "mime: html", "[mime]" ) {
    CHECK( test_mime_for( "index.html" ) == "text/html; charset=utf-8" );
    CHECK( test_mime_for( "page.htm" ) == "text/html; charset=utf-8" );
}

TEST_CASE( "mime: css", "[mime]" ) {
    CHECK( test_mime_for( "style.css" ) == "text/css; charset=utf-8" );
}

TEST_CASE( "mime: javascript", "[mime]" ) {
    CHECK( test_mime_for( "app.js" ) == "application/javascript; charset=utf-8" );
}

TEST_CASE( "mime: json", "[mime]" ) {
    CHECK( test_mime_for( "data.json" ) == "application/json; charset=utf-8" );
}

TEST_CASE( "mime: images", "[mime]" ) {
    CHECK( test_mime_for( "photo.png" ) == "image/png" );
    CHECK( test_mime_for( "photo.jpg" ) == "image/jpeg" );
    CHECK( test_mime_for( "photo.jpeg" ) == "image/jpeg" );
    CHECK( test_mime_for( "anim.gif" ) == "image/gif" );
    CHECK( test_mime_for( "icon.svg" ) == "image/svg+xml" );
    CHECK( test_mime_for( "favicon.ico" ) == "image/x-icon" );
}

TEST_CASE( "mime: fonts", "[mime]" ) {
    CHECK( test_mime_for( "font.woff2" ) == "font/woff2" );
    CHECK( test_mime_for( "font.woff" ) == "font/woff" );
}

TEST_CASE( "mime: wasm", "[mime]" ) {
    CHECK( test_mime_for( "module.wasm" ) == "application/wasm" );
}

TEST_CASE( "mime: text and xml", "[mime]" ) {
    CHECK( test_mime_for( "readme.txt" ) == "text/plain; charset=utf-8" );
    CHECK( test_mime_for( "feed.xml" ) == "application/xml; charset=utf-8" );
}

TEST_CASE( "mime: unknown extension", "[mime]" ) {
    CHECK( test_mime_for( "file.xyz" ) == "application/octet-stream" );
    CHECK( test_mime_for( "file.bin" ) == "application/octet-stream" );
    CHECK( test_mime_for( "file.tar.gz" ) == "application/octet-stream" );
}

TEST_CASE( "mime: case insensitive", "[mime]" ) {
    CHECK( test_mime_for( "FILE.HTML" ) == "text/html; charset=utf-8" );
    CHECK( test_mime_for( "STYLE.CSS" ) == "text/css; charset=utf-8" );
    CHECK( test_mime_for( "image.PNG" ) == "image/png" );
    CHECK( test_mime_for( "image.JPG" ) == "image/jpeg" );
}

TEST_CASE( "mime: no extension", "[mime]" ) {
    CHECK( test_mime_for( "Makefile" ) == "application/octet-stream" );
    CHECK( test_mime_for( "LICENSE" ) == "application/octet-stream" );
}

TEST_CASE( "mime: path with directories", "[mime]" ) {
    CHECK( test_mime_for( "/var/www/html/index.html" ) == "text/html; charset=utf-8" );
    CHECK( test_mime_for( "./public/css/app.css" ) == "text/css; charset=utf-8" );
}