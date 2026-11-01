
# every file in /test/ dir matching test_*.cpp added as test

file(GLOB svarg_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
# TODO rm yap from svarg
list(FILTER svarg_TESTS EXCLUDE REGEX "/test_yap\\.cpp$")

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
    if (svarg_CI)
        target_compile_definitions(${test_name} PRIVATE SVARG_CI)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# отслеживаем любое изменение внутри файлов
file(GLOB TEST_SPEC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test_specs/*")
file(GLOB TEST_SPEC_FILES2 "${CMAKE_CURRENT_SOURCE_DIR}/test_raw_links_specs/*")
file(GLOB TEST_SPEC_FILES3 "${CMAKE_CURRENT_SOURCE_DIR}/test_api/*")
set_property(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${TEST_SPEC_FILES}
    ${TEST_SPEC_FILES2}
    ${TEST_SPEC_FILES3}
)
# изменение количества файлов, директории
set_property(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test_specs"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_raw_links_specs"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_api"
)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/test_specs" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/test_raw_links_specs" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/test_api" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# test_generated использует код, сгенерированный svarg из generated_api/api.yaml
set(SVARG_TEST_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(
    OUTPUT "${SVARG_TEST_GENERATED_DIR}/api/server.hpp" "${SVARG_TEST_GENERATED_DIR}/api/client.hpp"
           "${SVARG_TEST_GENERATED_DIR}/api/api.cpp"
    COMMAND svarg --out "${SVARG_TEST_GENERATED_DIR}" --specs-dir "${CMAKE_CURRENT_SOURCE_DIR}/generated_api" api.yaml
    DEPENDS svarg "${CMAKE_CURRENT_SOURCE_DIR}/generated_api/api.yaml"
    VERBATIM
)
# api.cpp - определения сервера, клиента и инстанциации (де)сериализации операций
target_sources(test_generated PRIVATE "${SVARG_TEST_GENERATED_DIR}/api/server.hpp"
                                      "${SVARG_TEST_GENERATED_DIR}/api/client.hpp"
                                      "${SVARG_TEST_GENERATED_DIR}/api/api.cpp")
target_include_directories(test_generated PRIVATE "${SVARG_TEST_GENERATED_DIR}")
target_link_libraries(test_generated PRIVATE svarg_cxx_supportlib)
