cmake_minimum_required(VERSION 3.20)
project(svarg)

include(cmake/deps.cmake)

option(svarg_ENABLE_TESTING "enables testing" OFF)
option(svarg_ENABLE_ASAN "build ASAN (for CI)" OFF)
option(svarg_ENABLE_UBSAN "build UNSAN (for CI)" OFF)

file(GLOB_RECURSE svarg_SRCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(svarglib STATIC
    ${svarg_SRCES}
)

target_include_directories(svarglib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(svarglib PUBLIC
    ${CMAKE_THREAD_LIBS_INIT}
    anyanylib
    kelcorolib
    zallib
    clinoklib
    Boost::json
    Boost::pfr
    ryml
    Boost::iostreams # mapped file
    strswitchlib
    http2lib
)

if(svarg_ENABLE_TESTING)
	include(CTest)
	add_subdirectory(test)
endif()

add_executable(svarg main.cpp)

target_link_libraries(svarg PUBLIC svarglib)

set_target_properties(svarg svarglib PROPERTIES
	LINKER_LANGUAGE CXX
	CXX_STANDARD 23
	CMAKE_CXX_STANDARD_REQUIRED ON
)
# generates launch.json, places compile_commands.json correctly for clangd
ccozy_on_configuration_end_vscode()
