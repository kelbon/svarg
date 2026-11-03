cmake_minimum_required(VERSION 3.21)
project(svarg)

# svarg - это генератор (svarg) и рантайм сгенерированного кода (svarg_cxx_supportlib). Проекту пользователя
# нужен только рантайм и svarg_generate_cxx, генератор собирается отдельно в Release (см. ниже)
option(svarg_DEV_BUILD "svarg development: the generator is built in this tree with the project configuration" OFF)
option(svarg_GENERATOR_ONLY "build only the generator, without the runtime" OFF)
option(svarg_ENABLE_NETWORK_REFS "the generator can load $ref by URL (requires hidi)" OFF)
option(svarg_ENABLE_TESTING "enables testing" OFF)
option(svarg_ENABLE_ASAN "build ASAN (for CI)" OFF)
option(svarg_ENABLE_UBSAN "build UNSAN (for CI)" OFF)
option(svarg_CI "marks that building on CI (for testing)" OFF)
option(svarg_ENABLE_EXAMPLES "enables examples" OFF)

# тесты используют генератор как библиотеку
if(svarg_ENABLE_TESTING AND NOT svarg_DEV_BUILD)
  message(STATUS "svarg: svarg_ENABLE_TESTING requires svarg_DEV_BUILD, enabling it")
  set(svarg_DEV_BUILD ON)
endif()

set(svarg_BUILD_GENERATOR OFF)
set(svarg_BUILD_RUNTIME ON)
if(svarg_GENERATOR_ONLY)
  set(svarg_BUILD_GENERATOR ON)
  set(svarg_BUILD_RUNTIME OFF)
elseif(svarg_DEV_BUILD)
  set(svarg_BUILD_GENERATOR ON)
endif()

include(cmake/deps.cmake)

# общее для генератора и рантайма: ошибки, медиа типы, пути, (де)сериализация JSON. Без hidi
add_library(svarg_utilslib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/json_compare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/media_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/parse_url.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/path_tools.cpp
)
target_include_directories(svarg_utilslib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(svarg_utilslib PUBLIC
    ${CMAKE_THREAD_LIBS_INIT}
    anyanylib
    kelcorolib
    zallib
    Boost::json
    Boost::pfr
    strswitchlib
    omolib
    metellib
)

if(svarg_BUILD_RUNTIME)
  add_subdirectory(include/svarg/backend/cxx/support)
endif()

if(svarg_BUILD_GENERATOR)
  file(GLOB_RECURSE svarg_SRCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
  # уже в svarg_utilslib
  list(FILTER svarg_SRCES EXCLUDE REGEX "/src/utils/(json_compare|media_types|parse_url|path_tools)\\.cpp$")

  add_library(svarglib STATIC ${svarg_SRCES})
  target_link_libraries(svarglib PUBLIC
      svarg_utilslib
      clinoklib
      Boost::process
      Boost::iostreams # mapped file
      ryml
  )
  if(svarg_ENABLE_NETWORK_REFS)
    target_link_libraries(svarglib PUBLIC hidilib)
    target_compile_definitions(svarglib PRIVATE SVARG_ENABLE_NETWORK_REFS)
  endif()

  add_executable(svarg main.cpp)
  add_executable(svarg-ir ir_main.cpp)
  add_executable(svarg-ir-cxx cxx_main.cpp)

  target_link_libraries(svarg PUBLIC svarglib)
  target_link_libraries(svarg-ir PUBLIC svarglib)
  target_link_libraries(svarg-ir-cxx PUBLIC svarglib)
endif()

foreach(tgt svarg_utilslib svarglib svarg svarg-ir svarg-ir-cxx)
  if(TARGET ${tgt})
    set_target_properties(${tgt} PROPERTIES
        LINKER_LANGUAGE CXX
        CXX_STANDARD 23
        CMAKE_CXX_STANDARD_REQUIRED ON
    )
  endif()
endforeach()

# генератор для svarg_generate_cxx, если он не собирается в этом дереве: отдельная сборка в Release (с
# компилятором проекта, при кросс-компиляции - хостовым), чтобы кодогенерация не шла отладочной сборкой
if(svarg_BUILD_RUNTIME AND NOT svarg_BUILD_GENERATOR)
  include(ExternalProject)
  set(svarg_generator_dir "${CMAKE_CURRENT_BINARY_DIR}/svarg_generator")
  set(svarg_generator_args -DCMAKE_BUILD_TYPE=Release -Dsvarg_GENERATOR_ONLY=ON)
  if(NOT CMAKE_CROSSCOMPILING)
    list(APPEND svarg_generator_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                                     -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
  endif()
  if(CPM_SOURCE_CACHE)
    list(APPEND svarg_generator_args -DCPM_SOURCE_CACHE=${CPM_SOURCE_CACHE})
  endif()
  # общие зависимости уже скачаны: не скачивать их второй раз
  foreach(pkg BOOST STRSWITCH KELCORO ANYANY LOGIC_GUARDS OMO METEL)
    if(${pkg}_SOURCE_DIR)
      list(APPEND svarg_generator_args -DCPM_${pkg}_SOURCE=${${pkg}_SOURCE_DIR})
    endif()
  endforeach()
  set(svarg_generator_exe "${svarg_generator_dir}/svarg${CMAKE_EXECUTABLE_SUFFIX}")
  get_property(svarg_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(svarg_multi_config)
    set(svarg_generator_exe "${svarg_generator_dir}/Release/svarg${CMAKE_EXECUTABLE_SUFFIX}")
  endif()
  ExternalProject_Add(svarg_generator
      SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
      BINARY_DIR "${svarg_generator_dir}"
      CMAKE_ARGS ${svarg_generator_args}
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target svarg
      INSTALL_COMMAND ""
      BUILD_BYPRODUCTS "${svarg_generator_exe}"
      # пересобирается при изменении исходников svarg (если ничего не изменилось - ничего не делает)
      BUILD_ALWAYS ON
  )
  set(SVARG_GENERATOR_EXECUTABLE "${svarg_generator_exe}" CACHE INTERNAL "svarg built by svarg_generator")
endif()

if(svarg_ENABLE_TESTING)
	include(CTest)
	add_subdirectory(test)
endif()

set(SVARG_SCRIPT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/svarg_generate.cmake)

# `svarg_generate_cxx` fn
include(cmake/svarg_generate.cmake)

if(svarg_ENABLE_EXAMPLES AND svarg_BUILD_RUNTIME)
    add_subdirectory(examples/cxx)
    add_subdirectory(examples/cxx_nrf)
    add_subdirectory(examples/cxx_stream_server)
    add_subdirectory(examples/cxx_stream_client)
endif()

# generates launch.json, places compile_commands.json correctly for clangd
if(PROJECT_IS_TOP_LEVEL AND NOT svarg_GENERATOR_ONLY)
  ccozy_on_configuration_end_vscode()
endif()
