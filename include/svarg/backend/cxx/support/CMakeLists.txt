

add_library(svarg_cxx_supportlib STATIC ${CMAKE_CURRENT_SOURCE_DIR}/stream_parsing.cpp)

target_link_libraries(svarg_cxx_supportlib PUBLIC svarglib)

set_target_properties(svarg_cxx_supportlib PROPERTIES
	LINKER_LANGUAGE CXX
	CXX_STANDARD 23
	CMAKE_CXX_STANDARD_REQUIRED ON
)
