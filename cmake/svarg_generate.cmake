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
#       FORMATS          string+date=my::date                   # optional, C++ types of schema formats
#       FILE_PREFIX      "#include \"my_types.hpp\""            # optional, inserted into every generated
#                                                               # header: own codecs, format types
#       SOURCES_PER_SPEC 8                                      # optional, default 1: into how many .cpp
#                                                               # the code of each spec is split
#       TASK_CONTEXT     my::task_ctx                           # optional: server handlers and client send
#                                                               # return dd::task<T, my::task_ctx>, the type
#                                                               # must be declared in FILE_PREFIX
#   )
#
#   target_link_libraries(my_app PRIVATE users_api_generated)
#
# Code is generated at build time by svarg built by the svarg project (in Release) and regenerated when svarg or
# any spec file it read changes (svarg writes a depfile)

cmake_minimum_required(VERSION 3.21)
include_guard(GLOBAL)

function(svarg_generate_cxx)
    set(oneValueArgs TARGET SPECS_DIR OUT_DIR SOURCES_PER_SPEC FILE_PREFIX TASK_CONTEXT)
    set(multiValueArgs SPECS INCLUDE_DIRS STREAM_CONTENT_TYPES FORMATS)
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
    # генератор: собранный в этом дереве (svarg_DEV_BUILD) или отдельно в Release (svarg_generator). От него
    # генерация зависит: после пересборки svarg код перегенерируется
    if(TARGET svarg)
        set(svarg_exe "$<TARGET_FILE:svarg>")
        set(svarg_depends svarg)
    elseif(TARGET svarg_generator)
        set(svarg_exe "${SVARG_GENERATOR_EXECUTABLE}")
        set(svarg_depends svarg_generator "${SVARG_GENERATOR_EXECUTABLE}")
    else()
        message(FATAL_ERROR "svarg_generate_cxx(${ARGS_TARGET}): svarg is built without runtime (svarg_GENERATOR_ONLY)")
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

    set(format_args "")
    foreach(f IN LISTS ARGS_FORMATS)
        list(APPEND format_args --add-format "${f}")
    endforeach()

    set(prefix_args "")
    if(DEFINED ARGS_FILE_PREFIX)
        list(APPEND prefix_args --file-prefix "${ARGS_FILE_PREFIX}")
    endif()

    set(task_context_args "")
    if(ARGS_TASK_CONTEXT)
        list(APPEND task_context_args --task-context "${ARGS_TASK_CONTEXT}")
    endif()

    set(sources_args "")
    if(DEFINED ARGS_SOURCES_PER_SPEC)
        list(APPEND sources_args --sources-per-spec "${ARGS_SOURCES_PER_SPEC}")
    endif()

    # Arguments shared between the discovery (--dry-run) and the real run.
    set(svarg_args "--out" "${out_dir}" "--specs-dir" "${specs_dir}" ${include_args} ${stream_args} ${format_args}
        ${prefix_args} ${task_context_args} ${sources_args}
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
    # остальные зависимости (спеки, на которые ссылаются входные) svarg пишет в депфайл: все прочитанные файлы
    set(depfile "${CMAKE_CURRENT_BINARY_DIR}/${ARGS_TARGET}.svarg.d")
    add_custom_command(
        OUTPUT ${srcs}
        COMMAND "${svarg_exe}" --depfile "${depfile}" ${svarg_args}
        DEPENDS ${spec_files} ${svarg_depends}
        DEPFILE "${depfile}"
        COMMENT "svarg: generating C++ from OpenAPI specs into ${out_dir}"
        VERBATIM
    )

    # --- 4. Цель -----------------------------------------------------------
    add_library(${ARGS_TARGET} STATIC ${srcs})
    target_include_directories(${ARGS_TARGET} PUBLIC "${out_dir}")
    target_link_libraries(${ARGS_TARGET} PUBLIC svarg_cxx_supportlib)
    set_target_properties(${ARGS_TARGET} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
endfunction()
