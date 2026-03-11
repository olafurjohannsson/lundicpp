#include <lundi/core.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace lundi;
using namespace lundi::detail;

static const std::string BOUNDARY = "WebKitFormBoundary7MA4YWxkTrZu0gW";

static std::string make_multipart( const std::string& boundary, std::vector< std::string > parts )
{
    std::string body;
    for( auto& part : parts )
    {
        body += "--" + boundary + "\r\n" + part;
    }
    body += "--" + boundary + "--\r\n";
    return body;
}

TEST_CASE( "multipart: single text field", "[multipart]" ) {
    auto body = make_multipart(
        BOUNDARY, { "Content-Disposition: form-data; name=\"username\"\r\n\r\nolafur\r\n" } );
    auto form = parse_multipart( body, BOUNDARY );
    CHECK( form.fields.size() == 1 );
    CHECK( form.fields[ "username" ] == "olafur" );
    CHECK( form.files.empty() );
}

TEST_CASE( "multipart: multiple text fields", "[multipart]" ) {
    auto body = make_multipart(
        BOUNDARY, {
        "Content-Disposition: form-data; name=\"first\"\r\n\r\nOlafur\r\n",
        "Content-Disposition: form-data; name=\"last\"\r\n\r\nJohannsson\r\n",
        "Content-Disposition: form-data; name=\"country\"\r\n\r\nIceland\r\n",
    } );
    auto form = parse_multipart( body, BOUNDARY );
    CHECK( form.fields.size() == 3 );
    CHECK( form.fields[ "first" ] == "Olafur" );
    CHECK( form.fields[ "last" ] == "Johannsson" );
    CHECK( form.fields[ "country" ] == "Iceland" );
}

TEST_CASE( "multipart: file upload", "[multipart]" ) {
    auto body = make_multipart(
        BOUNDARY, { "Content-Disposition: form-data; name=\"doc\"; filename=\"readme.txt\"\r\n"
                    "Content-Type: text/plain\r\n\r\nHello World!\r\n" } );
    auto form = parse_multipart( body, BOUNDARY );
    CHECK( form.files.size() == 1 );
    auto* f = form.file( "doc" );
    REQUIRE( f != nullptr );
    CHECK( f->filename == "readme.txt" );
    CHECK( f->content_type == "text/plain" );
    CHECK( f->data == "Hello World!" );
}

TEST_CASE( "multipart: file with default content-type", "[multipart]" ) {
    auto body = make_multipart( BOUNDARY, { "Content-Disposition: form-data; name=\"bin\"; "
                                            "filename=\"data.bin\"\r\n\r\n\x01\x02\x03\r\n" } );
    auto form = parse_multipart( body, BOUNDARY );
    auto* f = form.file( "bin" );
    REQUIRE( f != nullptr );
    CHECK( f->content_type == "application/octet-stream" );
    CHECK( f->data.size() == 3 );
}

TEST_CASE( "multipart: mixed fields and files", "[multipart]" ) {
    auto body = make_multipart(
        BOUNDARY, {
        "Content-Disposition: form-data; name=\"title\"\r\n\r\nMy Document\r\n",
        "Content-Disposition: form-data; name=\"file\"; filename=\"doc.pdf\"\r\n"
        "Content-Type: application/pdf\r\n\r\nPDF_CONTENT_HERE\r\n",
        "Content-Disposition: form-data; name=\"description\"\r\n\r\nA test file\r\n",
    } );
    auto form = parse_multipart( body, BOUNDARY );
    CHECK( form.fields.size() == 2 );
    CHECK( form.fields[ "title" ] == "My Document" );
    CHECK( form.fields[ "description" ] == "A test file" );
    CHECK( form.files.size() == 1 );
    CHECK( form.files[ "file" ].filename == "doc.pdf" );
    CHECK( form.files[ "file" ].data == "PDF_CONTENT_HERE" );
}

TEST_CASE( "multipart: empty body", "[multipart]" ) {
    auto form = parse_multipart( "", BOUNDARY );
    CHECK( form.fields.empty() );
    CHECK( form.files.empty() );
}

TEST_CASE( "multipart: wrong boundary", "[multipart]" ) {
    auto body =
        make_multipart( BOUNDARY, { "Content-Disposition: form-data; name=\"x\"\r\n\r\nval\r\n" } );
    auto form = parse_multipart( body, "wrong-boundary" );
    CHECK( form.fields.empty() );
}

TEST_CASE( "extract_boundary: standard format", "[multipart]" ) {
    CHECK( extract_boundary( "multipart/form-data; boundary=abc123" ) == "abc123" );
}

TEST_CASE( "extract_boundary: quoted boundary", "[multipart]" ) {
    CHECK( extract_boundary( "multipart/form-data; boundary=\"abc123\"" ) == "abc123" );
}

TEST_CASE( "extract_boundary: long webkit boundary", "[multipart]" ) {
    auto b =
        extract_boundary( "multipart/form-data; boundary=WebKitFormBoundary7MA4YWxkTrZu0gW" );
    CHECK( b == "WebKitFormBoundary7MA4YWxkTrZu0gW" );
}

TEST_CASE( "extract_boundary: missing boundary", "[multipart]" ) {
    CHECK( extract_boundary( "multipart/form-data" ).empty() );
    CHECK( extract_boundary( "application/json" ).empty() );
}

TEST_CASE( "request: form_field and form_file_ptr", "[multipart]" ) {
    request req;
    req.headers_arr[ req.num_headers++ ] = { "content-type", "multipart/form-data; boundary=TESTBND" };

    // Keep string alive req.body is string_view, must not dangle
    auto body_str = make_multipart(
        "TESTBND", {
        "Content-Disposition: form-data; name=\"name\"\r\n\r\ntest_user\r\n",
        "Content-Disposition: form-data; name=\"avatar\"; filename=\"pic.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\nJPEGDATA\r\n",
    } );
    req.body = body_str;

    CHECK( req.form_field( "name" ) == "test_user" );
    CHECK( req.form_field( "missing", "default" ) == "default" );

    auto* f = req.form_file_ptr( "avatar" );
    REQUIRE( f != nullptr );
    CHECK( f->filename == "pic.jpg" );
    CHECK( f->content_type == "image/jpeg" );
    CHECK( f->data == "JPEGDATA" );

    CHECK( req.form_file_ptr( "missing" ) == nullptr );
}

TEST_CASE( "request: form lazy parsing", "[multipart]" ) {
    request req;
    std::string body_str = "not multipart data";
    req.body = body_str;
    CHECK( req.form_field( "anything" ) == "" );
    CHECK( req.form().fields.empty() );
}