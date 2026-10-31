#include "svarg/backend/cxx/namespace_strings.hpp"

#include <moko3/moko3.hpp>

static bool Case(std::string_view a, std::string_view b, std::string_view expected) {
  return svarg::cxx::namespace_relative(a, b) == expected;
}
#define NR_CASE(a, b, expected) REQUIRE(Case(a, b, expected))

namespace d::e {
constexpr int t1 = 1;
}

namespace b::c {
constexpr int t1 = 0;
static_assert(::d::e::t1 == 1);
}  // namespace b::c

namespace a::b {
constexpr int t1 = 2;
}
namespace b {
constexpr int t1 = 3;
}

namespace a::b::c {
constexpr int t1 = 4;
static_assert(::b::c::t1 == 0);
static_assert(::a::b::t1 == 2);
static_assert(::b::t1 == 3);
}  // namespace a::b::c

namespace a {
static_assert(b::c::t1 == 4);
}

TEST("namespace_relative") {
  NR_CASE("a::b::c", "b::c", "::b::c");
  NR_CASE("b::c", "d::e", "::d::e");
  NR_CASE("a::b::c", "a::b", "::a::b");
  // префикс только по целым сегментам
  NR_CASE("x::anyOf", "x::anyOf_::_1", "::x::anyOf_::_1");
  NR_CASE("a::b::c", "b", "::b");
  NR_CASE("a", "a::b::c", "b::c");
  NR_CASE("a::b::c", "a::b::c", "");
  NR_CASE("", "", "");
  NR_CASE("a::b::c", "", "::");
  NR_CASE("", "a::b::c", "a::b::c");
  NR_CASE("a", "b", "::b");
  NR_CASE("a::b", "a::b::c", "c");
  NR_CASE("a::b", "a::c", "::a::c");
  NR_CASE("a::b::c::d", "a::b::e", "::a::b::e");
  NR_CASE("a::b", "b::c", "::b::c");
  NR_CASE("x::a::b", "a::b::c", "::a::b::c");
  NR_CASE("", "a", "a");
  NR_CASE("a", "", "::");
  NR_CASE("a", "a", "");
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
