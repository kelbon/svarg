cmake_minimum_required(VERSION 3.20)
project(svarg_simple_cxx_example_project CXX)
 
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${SVARG_SCRIPT_PATH})
 
svarg_generate_cxx(
    TARGET    svarg_simple_cxx_example_generated
    SPECS_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    OUT_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/generated
    SPECS     api.yaml
    # SVARG_EXECUTABLE defaults to "svarg" resolved via PATH - override if needed:
    # SVARG_EXECUTABLE /path/to/svarg
)
add_executable(svarg_simple_cxx_example main.cpp)
target_link_libraries(svarg_simple_cxx_example PUBLIC svarg_simple_cxx_example_generated)
