include(cmake/get_cpm.cmake)

# см. то же самое в hidi
if (WIN32)
  set(BOOST_CONTEXT_IMPLEMENTATION winfib CACHE STRING "Boost.Context implementation (fcontext, ucontext, winfib)")
endif()

set(BOOST_INCLUDE_LIBRARIES system asio pfr json iostreams process stacktrace)
CPMAddPackage(
  NAME BOOST
  VERSION 1.87.0
  URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
  OPTIONS "BOOST_ENABLE_CMAKE ON"
)
unset(BOOST_INCLUDE_LIBRARIES)

CPMAddPackage(
  NAME STRSWITCH
  GITHUB_REPOSITORY kelbon/strswitch
  GIT_TAG v1.1.1
)

CPMAddPackage(
  NAME RAPIDYAML
  GITHUB_REPOSITORY biojppm/rapidyaml
  GIT_TAG v0.15.2
  OPTIONS "RYML_WITH_TAB_TOKENS ON" "RYML_INSTALL OFF" "RYML_STANDALONE ON" "RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS ON"
)

# cmake tools
CPMAddPackage(
  NAME CCOZY
  GITHUB_REPOSITORY kelbon/ccozy
  GIT_TAG v0.8.2
)

include(${CCOZY_SOURCE_DIR}/ccozy_tools.cmake)

# C++20 coroutines (kelcorolib)
CPMAddPackage(
  NAME KELCORO
  GITHUB_REPOSITORY kelbon/kelcoro
  GIT_TAG v1.4.7
)

# move_only_function, function ref etc (anyanylib)
CPMAddPackage(
  NAME ANYANY
  GITHUB_REPOSITORY kelbon/AnyAny
  GIT_TAG v1.3.0
)

# on_scope_exit / on_scope_failure (zallib)
CPMAddPackage(
  NAME LOGIC_GUARDS
  GITHUB_REPOSITORY kelbon/logic_guards
  GIT_TAG        v1.0.0
)

# CLI interface, clinoklib
CPMAddPackage(
  NAME CLINOK
  GITHUB_REPOSITORY kelbon/clinok
  GIT_TAG v2.1.0
)

CPMAddPackage(
  NAME HIDI
  GIT_REPOSITORY https://github.com/kelbon/hidi
  GIT_TAG v0.13.3
  OPTIONS "HIDI_USE_CPM ON"
)

CPMAddPackage(
  NAME OMO
  GIT_REPOSITORY https://github.com/kelbon/omo
  GIT_TAG v0.0.7
  OPTIONS "omo_DOWNLOAD_BOOST OFF"
)

CPMAddPackage(
  NAME METEL
  GIT_REPOSITORY https://github.com/kelbon/metel
  GIT_TAG v0.0.9
)

if (svarg_ENABLE_TESTING)
  CPMAddPackage(
    NAME MOKO3
    GITHUB_REPOSITORY kelbon/moko3
    GIT_TAG v0.10.2
  )
endif()

find_package(Threads REQUIRED)
