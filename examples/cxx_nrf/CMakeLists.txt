cmake_minimum_required(VERSION 3.20)
project(svarg_nrf_example CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${SVARG_SCRIPT_PATH})

# реальная спецификация 3GPP TS 29.510 (NRF, управление NF) и всё, на что она ссылается
# (CommonData и ещё десяток спек) - сгенерируется вместе с ними
svarg_generate_cxx(
    TARGET    svarg_nrf_api
    SPECS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../test/test_specs
    OUT_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated
    SPECS     TS29510_Nnrf_NFManagement.yaml
)
add_executable(svarg_nrf_example main.cpp)
target_link_libraries(svarg_nrf_example PRIVATE svarg_nrf_api)
