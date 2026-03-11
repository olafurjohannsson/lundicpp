add_executable(hello main.cpp)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/public)
configure_file(
    ${CMAKE_SOURCE_DIR}/examples/hello/public/index.html
    ${CMAKE_BINARY_DIR}/public/index.html
    COPYONLY
)
target_link_libraries(hello PRIVATE lundi)