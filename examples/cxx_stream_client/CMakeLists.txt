cmake_minimum_required(VERSION 3.20)
project(svarg_stream_client_example CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${SVARG_SCRIPT_PATH})

# та же спека, что у примера сервера: клиент обращается к нему
svarg_generate_cxx(
    TARGET    svarg_stream_client_api
    SPECS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cxx_stream_server
    OUT_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated
    SPECS     api.yaml
)
add_executable(svarg_stream_client_example main.cpp)
target_link_libraries(svarg_stream_client_example PRIVATE svarg_stream_client_api)
