cmake_minimum_required(VERSION 3.20)
project(lundi LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch Asio 
include(FetchContent)

FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
)
FetchContent_MakeAvailable(asio)

find_package(Threads REQUIRED)

# Lundi interface
add_library(lundi INTERFACE)

target_include_directories(lundi INTERFACE
    ${CMAKE_SOURCE_DIR}/include
    ${asio_SOURCE_DIR}/asio/include
)

target_compile_definitions(lundi INTERFACE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
)

if(WIN32)
    target_compile_definitions(lundi INTERFACE
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# target_compile_options(${PROJECT_NAME} INTERFACE
#     -Wall -Wextra -Wpedantic -Wshadow -Wconversion
#     -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
#     -Woverloaded-virtual -Wsign-conversion -Wnull-dereference
#     -Wformat=2
# )

target_link_libraries(lundi INTERFACE Threads::Threads)

# Examples 
option(LUNDI_BUILD_EXAMPLES "Build example programs" ON)
#option(LUNDI_BUILD_BENCH "Build benchmark programs" OFF)
option(LUNDI_BUILD_TESTS "Build tests" ON)

if(LUNDI_BUILD_EXAMPLES)
    add_subdirectory(examples/hello)
endif()

#if(LUNDI_BUILD_BENCH)
    #add_subdirectory(bench)
#endif()

if(LUNDI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()