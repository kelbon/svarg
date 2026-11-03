# Precondition: target `svarg_cxx_supportlib` (svarg added to the project, e.g. CPMAddPackage)
#
# Usage:
#   svarg_generate_cxx(
#       TARGET           users_api_generated
#       SPECS_DIR        ${CMAKE_SOURCE_DIR}/openapi
#       OUT_DIR          ${CMAKE_BINARY_DIR}/generated/users_api
#       SPECS            users.yaml admin/*
#       INCLUDE_DIRS     ${CMAKE_SOURCE_DIR}/openapi/common     # optional, each becomes "-I <dir>"
#       STREAM_CONTENT_TYPES text/event-stream                   # optional, response media types streamed
#                                                               # besides application/octet-stream
#       SOURCES_PER_SPEC 8                                      # optional, default 1: into how many .cpp
#                                                               # the code of each spec is split
#       SVARG_EXECUTABLE ${CMAKE_SOURCE_DIR}/tools/bin/svarg    # optional, by default svarg built by the
#                                                               # svarg project (Release), or found in PATH
#   )
#
#   target_link_libraries(my_app PRIVATE users_api_generated)
#
# Code is generated at build time and regenerated when specs or svarg change

cmake_minimum_required(VERSION 3.21)
include_guard(GLOBAL)

function(svarg_generate_cxx)
    set(oneValueArgs TARGET SPECS_DIR OUT_DIR SVARG_EXECUTABLE SOURCES_PER_SPEC)
    set(multiValueArgs SPECS INCLUDE_DIRS STREAM_CONTENT_TYPES)
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
    # генератор: указанный явно, собранный в этом дереве (svarg_DEV_BUILD), собранный отдельно в Release
    # (svarg_generator) или установленный. От цели генерация зависит: после пересборки svarg код перегенерируется
    set(svarg_depends "")
    if(ARGS_SVARG_EXECUTABLE)
        set(svarg_exe "${ARGS_SVARG_EXECUTABLE}")
    elseif(TARGET svarg)
        set(svarg_exe "$<TARGET_FILE:svarg>")
        set(svarg_depends svarg)
    elseif(TARGET svarg_generator)
        set(svarg_exe "${SVARG_GENERATOR_EXECUTABLE}")
        set(svarg_depends svarg_generator "${SVARG_GENERATOR_EXECUTABLE}")
    else()
        find_program(SVARG_PROGRAM NAMES svarg)
        if(NOT SVARG_PROGRAM)
            message(FATAL_ERROR "svarg_generate_cxx(${ARGS_TARGET}): svarg not found")
        endif()
        set(svarg_exe "${SVARG_PROGRAM}")
    endif()
    set(sources_per_spec 1)
    if(DEFINED ARGS_SOURCES_PER_SPEC)
        set(sources_per_spec ${ARGS_SOURCES_PER_SPEC})
    endif()
    if(sources_per_spec LESS 1)
        message(FATAL_ERROR "svarg_generate_cxx(${ARGS_TARGET}): SOURCES_PER_SPEC must be at least 1")
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

    set(stream_args "")
    foreach(ct IN LISTS ARGS_STREAM_CONTENT_TYPES)
        list(APPEND stream_args --stream-content-type "${ct}")
    endforeach()

    set(sources_args "")
    if(DEFINED ARGS_SOURCES_PER_SPEC)
        list(APPEND sources_args --sources-per-spec "${ARGS_SOURCES_PER_SPEC}")
    endif()

    # Arguments shared between the discovery (--dry-run) and the real run.
    set(svarg_args "--out" "${out_dir}" "--specs-dir" "${specs_dir}" ${include_args} ${stream_args} ${sources_args}
        ${ARGS_SPECS})

    # --- 1. Входные спеки -------------------------------------------------
    # маски "dir/*" раскрываются так же, как в svarg: все файлы в dir рекурсивно
    set(spec_files "")
    foreach(spec IN LISTS ARGS_SPECS)
        if(spec MATCHES "/\\*$")
            string(REGEX REPLACE "/\\*$" "" sub "${spec}")
            file(GLOB_RECURSE matched CONFIGURE_DEPENDS "${specs_dir}/${sub}/*")
            list(APPEND spec_files ${matched})
        else()
            get_filename_component(spec_abs "${spec}" ABSOLUTE BASE_DIR "${specs_dir}")
            list(APPEND spec_files "${spec_abs}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES spec_files)

    # --- 2. Сгенерированные .cpp ------------------------------------------
    # svarg на каждую входную спеку пишет <имя>/<имя>.cpp и <имя>/<имя>_1..N-1.cpp (имя - имя файла без
    # расширения), даже если они пустые. Заголовки - побочный продукт, их список CMake не нужен
    set(srcs "")
    foreach(spec_file IN LISTS spec_files)
        get_filename_component(name "${spec_file}" NAME_WLE)
        list(APPEND srcs "${out_dir}/${name}/${name}.cpp")
        math(EXPR last_part "${sources_per_spec} - 1")
        if(last_part GREATER 0)
            foreach(i RANGE 1 ${last_part})
                list(APPEND srcs "${out_dir}/${name}/${name}_${i}.cpp")
            endforeach()
        endif()
    endforeach()

    # --- 3. Кодогенерация при сборке --------------------------------------
    # спеки, на которые ссылаются входные, лежат в specs_dir и INCLUDE_DIRS: изменение любой перегенерирует код
    set(all_spec_files ${spec_files})
    foreach(dir IN ITEMS "${specs_dir}" ${ARGS_INCLUDE_DIRS})
        get_filename_component(dir "${dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        file(GLOB_RECURSE found CONFIGURE_DEPENDS "${dir}/*.yaml" "${dir}/*.yml" "${dir}/*.json")
        foreach(f IN LISTS found)
            # OUT_DIR может лежать внутри папки спек
            string(FIND "${f}" "${out_dir}/" in_out_dir)
            if(NOT in_out_dir EQUAL 0)
                list(APPEND all_spec_files "${f}")
            endif()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES all_spec_files)
    add_custom_command(
        OUTPUT ${srcs}
        COMMAND "${svarg_exe}" ${svarg_args}
        DEPENDS ${all_spec_files} ${svarg_depends}
        COMMENT "svarg: generating C++ from OpenAPI specs into ${out_dir}"
        VERBATIM
    )

    # --- 4. Цель -----------------------------------------------------------
    add_library(${ARGS_TARGET} STATIC ${srcs})
    target_include_directories(${ARGS_TARGET} PUBLIC "${out_dir}")
    target_link_libraries(${ARGS_TARGET} PUBLIC svarg_cxx_supportlib)
    set_target_properties(${ARGS_TARGET} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
endfunction()
