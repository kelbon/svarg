# Preconditions (must already exist in the project when this is called):
#   - target `svarg_cxx_supportlib`
#
# Usage:
#   include(SvargGenerate.cmake)
#
#   svarg_generate_cxx(
#       TARGET           users_api_generated
#       SPECS_DIR        ${CMAKE_SOURCE_DIR}/openapi
#       OUT_DIR          ${CMAKE_SOURCE_DIR}/generated/users_api
#       SPECS            users.yaml admin/*
#       INCLUDE_DIRS     ${CMAKE_SOURCE_DIR}/openapi/common     # optional, each becomes "-I <dir>"
#       SVARG_EXECUTABLE ${CMAKE_SOURCE_DIR}/tools/bin/svarg    # optional, defaults to "svarg" (PATH)
#   )
#
#   target_link_libraries(my_app PRIVATE users_api_generated)

cmake_minimum_required(VERSION 3.14)
include_guard(GLOBAL)

function(svarg_generate_cxx)
    set(oneValueArgs TARGET SPECS_DIR OUT_DIR SVARG_EXECUTABLE)
    set(multiValueArgs SPECS INCLUDE_DIRS)
    cmake_parse_arguments(ARGS "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARGS_TARGET)
        message(FATAL_ERROR "svarg_generate_cxx: TARGET is required")
    endif()
    if(TARGET ${ARGS_TARGET})
        message(FATAL_ERROR "svarg_generate_cxx: target '${ARGS_TARGET}' already exists")
    endif()
    if(NOT ARGS_SPECS_DIR)
        message(FATAL_ERROR "svarg_generate_cxx: SPECS_DIR is required")
    endif()
    if(NOT ARGS_OUT_DIR)
        message(FATAL_ERROR "svarg_generate_cxx: OUT_DIR is required")
    endif()
    if(NOT ARGS_SPECS)
        message(FATAL_ERROR "svarg_generate_cxx: SPECS is required (at least one spec/mask)")
    endif()
    if(NOT TARGET svarg_cxx_supportlib)
        message(FATAL_ERROR "svarg_generate_cxx: target 'svarg_cxx_supportlib' must be defined before this call")
    endif()
    if(NOT ARGS_SVARG_EXECUTABLE)
        find_program(SVARG_PROGRAM NAMES svarg)
        if(NOT SVARG_PROGRAM AND NOT TARGET svarg)
            message(WARNING "svarg_generate_cxx(${ARGS_TARGET}): svarg not found in PATH and no 'svarg' target - skipping")
            return()
        endif()
        set(ARGS_SVARG_EXECUTABLE "svarg")
    endif()

    get_filename_component(specs_dir "${ARGS_SPECS_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(out_dir "${ARGS_OUT_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    file(MAKE_DIRECTORY "${out_dir}")

    # -I <dir> for each include dir, passed through to svarg verbatim.
    set(include_args "")
    foreach(dir IN LISTS ARGS_INCLUDE_DIRS)
        get_filename_component(dir_abs "${dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND include_args -I "${dir_abs}")
    endforeach()

    # Arguments shared between the discovery (--dry-run) and the real run.
    set(svarg_args "--out" "${out_dir}" "--specs-dir" "${specs_dir}" ${include_args} ${ARGS_SPECS})

    # --- 1. Auto re-run configure when a spec changes ---------------------
    # Expand "dir/*" masks too, so files matched only through a mask are tracked.
    set(dep_files "")
    foreach(spec IN LISTS ARGS_SPECS)
        if(spec MATCHES "/\\*$")
            string(REGEX REPLACE "/\\*$" "" sub "${spec}")
            if(sub STREQUAL "")
                file(GLOB matched CONFIGURE_DEPENDS "${specs_dir}/*")
            else()
                file(GLOB matched CONFIGURE_DEPENDS "${specs_dir}/${sub}/*")
            endif()
            list(APPEND dep_files ${matched})
        else()
            list(APPEND dep_files "${specs_dir}/${spec}")
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${dep_files})

    # --- 2. Discover the exact output file list (--dry-run) --------
    execute_process(
        COMMAND "${ARGS_SVARG_EXECUTABLE}" --dry-run ${svarg_args}
        RESULT_VARIABLE rv OUTPUT_VARIABLE ov ERROR_VARIABLE ev)
    if(NOT rv EQUAL 0)
        message(FATAL_ERROR
            "'${ARGS_SVARG_EXECUTABLE} --dry-run ${svarg_args}' failed\n${ov}\n${ev}\n"
            "Make sure svarg is built and either on PATH or pointed to via SVARG_EXECUTABLE.")
    endif()

    set(manifest "${out_dir}/generated_files.txt")
    if(NOT EXISTS "${manifest}")
        message(FATAL_ERROR "svarg_generate_cxx(${ARGS_TARGET}): --dry-run did not produce ${manifest}")
    endif()

    file(STRINGS "${manifest}" rel_files)
    file(REMOVE ${manifest})
    # rm empty
    list(FILTER rel_files EXCLUDE REGEX "^$")
    if(NOT rel_files)
        message(FATAL_ERROR "svarg_generate_cxx(${ARGS_TARGET}): ${manifest} lists no files")
    endif()

    set(srcs "")
    set(hdrs "")
    set(abs_files "")
    foreach(rel IN LISTS rel_files)
        set(abs "${out_dir}/${rel}")
        list(APPEND abs_files "${abs}")
        if(abs MATCHES "\\.(cpp|cc|cxx)$")
            list(APPEND srcs "${abs}")
        else()
            list(APPEND hdrs "${abs}")
        endif()
    endforeach()

    # --- 3. Build-time codegen command ----------
    add_custom_command(
        OUTPUT ${abs_files}
        COMMAND "${ARGS_SVARG_EXECUTABLE}" ${svarg_args}
        DEPENDS ${dep_files}
        COMMENT "svarg: generating C++ from OpenAPI specs into ${out_dir}"
        VERBATIM
    )
    add_custom_target(${ARGS_TARGET}_codegen DEPENDS ${abs_files})

    # --- 4. Wrap the known file list into a target -------------------------
    if(srcs)
        add_library(${ARGS_TARGET} STATIC ${srcs} ${hdrs})
        target_include_directories(${ARGS_TARGET} PUBLIC "${out_dir}")
        target_link_libraries(${ARGS_TARGET} PUBLIC svarg_cxx_supportlib)
    else()
        add_library(${ARGS_TARGET} INTERFACE)
        target_sources(${ARGS_TARGET} INTERFACE ${hdrs})
        target_include_directories(${ARGS_TARGET} INTERFACE "${out_dir}")
        target_link_libraries(${ARGS_TARGET} INTERFACE svarg_cxx_supportlib)
    endif()
endfunction()
