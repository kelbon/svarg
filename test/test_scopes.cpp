#include "svarg/utils/scopes.hpp"

#include <moko3/moko3.hpp>

#include <vector>
#include <string>

using segments = std::vector<std::string>;

static segments collect(const svarg::scopes_t& s) {
  segments result;
  for (auto seg : s.segments())
    result.emplace_back(seg);
  return result;
}

// --- scopes_t: add/remove/view ---

TEST("scopes: complex") {
  svarg::scopes_t s;
  REQUIRE(s.view() == "");
  REQUIRE(collect(s) == segments{});
  s.add_key("foo");
  REQUIRE(s.view() == "/foo");
  REQUIRE(collect(s) == segments{"foo"});
  s.add_key("bar");
  s.add_key("baz");
  REQUIRE(s.view() == "/foo/bar/baz");
  REQUIRE(collect(s) == segments{"foo", "bar", "baz"});
  s.remove_scope();
  REQUIRE(collect(s) == segments{"foo", "bar"});
  REQUIRE(s.view() == "/foo/bar");
  s.remove_scope();
  REQUIRE(collect(s) == segments{"foo"});
  REQUIRE(s.view() == "/foo");
  s.remove_scope();
  REQUIRE(collect(s) == segments{});
  REQUIRE(s.view() == "");
}
// TODO тесты из json pointer RFC на escaped

TEST("multiscope") {
  svarg::scopes_t s = "/foo/bar/baz"_jp;
  REQUIRE(s.view() == "/foo/bar/baz");
  REQUIRE(collect(s) == segments{"foo", "bar", "baz"});
  s.remove_scope();
  REQUIRE(collect(s) == segments{"foo", "bar"});
  REQUIRE(s.view() == "/foo/bar");
  s.remove_scope();
  REQUIRE(collect(s) == segments{"foo"});
  REQUIRE(s.view() == "/foo");
  s.remove_scope();
  REQUIRE(collect(s) == segments{});
  REQUIRE(s.view() == "");
}

TEST("scopes: add after remove") {
  svarg::scopes_t s;
  s.add_scopes("foo");
  s.add_scopes("bar");
  s.remove_scope();
  s.add_scopes("qux");
  REQUIRE(s.view() == "/foo/qux");
}

TEST("scopes: add remove interleaved") {
  svarg::scopes_t s;
  s.add_scopes("a");
  s.add_scopes("b");
  s.remove_scope();
  s.add_scopes("c");
  s.add_scopes("d");
  s.remove_scope();
  REQUIRE(s.view() == "/a/c");
}

// --- iterator ---

TEST("scopes: iterate single segment") {
  svarg::scopes_t s;
  s.add_scopes("foo");
  REQUIRE(collect(s) == segments{"foo"});
}

TEST("scopes: iterator begin equals end on empty") {
  svarg::scopes_t s;
  REQUIRE(s.segments().begin() == s.segments().end());
  s.add_scopes("x");
  REQUIRE(s.segments().begin() != s.segments().end());
}

TEST("scopes: iterator post-increment") {
  svarg::scopes_t s;
  s.add_scopes("a");
  s.add_scopes("b");
  auto it = s.segments().begin();
  REQUIRE(*it++ == "a");
  REQUIRE(*it++ == "b");
  REQUIRE(it == s.segments().end());
}

TEST("scopes: iterator dereference is idempotent") {
  svarg::scopes_t s;
  s.add_scopes("foo");
  s.add_scopes("bar");
  auto it = s.segments().begin();
  REQUIRE(*it == "foo");
  REQUIRE(*it == "foo");
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
