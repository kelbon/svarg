
# every file in /test/ dir matching test_*.cpp added as test

file(GLOB svarg_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

foreach(test_src ${svarg_TESTS})
    # file name without extension
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src})

    target_link_libraries(${test_name} PRIVATE svarglib moko3lib)
    
    set_target_properties(${test_name} PROPERTIES
		LINKER_LANGUAGE CXX
		CXX_STANDARD 23
		CMAKE_CXX_STANDARD_REQUIRED ON
	)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/test_specs" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
