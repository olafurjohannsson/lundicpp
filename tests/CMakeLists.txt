include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(lundi_tests
    test_coverage.cpp
    test_bench.cpp
    test_url_decode.cpp
    test_query_string.cpp
    test_parse_headers.cpp
    test_router.cpp
    test_router_advanced.cpp
    test_middleware.cpp
    test_response.cpp
    test_response_advanced.cpp
    test_regression.cpp
    test_cookies.cpp
    test_multipart.cpp
    test_websocket.cpp
    test_sse.cpp
    test_static_files.cpp
    test_chunked.cpp
    test_mime.cpp
    test_date_cache.cpp
    test_detail_functions.cpp
    test_engine_buffer.cpp
    test_engine_itoa.cpp
)

target_link_libraries(lundi_tests PRIVATE lundi Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(lundi_tests)