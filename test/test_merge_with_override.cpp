#include "svarg/utils/merge_with_override.hpp"

#include <moko3/moko3.hpp>

using Map = std::unordered_map<std::string, int>;

TEST("merge_with_override: unique keys") {
  Map a = {{"a", 1}};
  Map b = {{"b", 2}};
  auto r = svarg::merge_with_override(a, b);
  REQUIRE(r.size() == 2);
  REQUIRE(r["a"] == 1);
  REQUIRE(r["b"] == 2);
}

TEST("merge_with_override: b overrides duplicate key") {
  Map a = {{"x", 1}, {"y", 2}};
  Map b = {{"x", 99}};
  auto r = svarg::merge_with_override(a, b);
  REQUIRE(r.size() == 2);
  REQUIRE(r["x"] == 99);
  REQUIRE(r["y"] == 2);
}

TEST("merge_with_override: all keys overlap, b wins") {
  Map a = {{"x", 1}, {"y", 2}};
  Map b = {{"x", 10}, {"y", 20}};
  auto r = svarg::merge_with_override(a, b);
  REQUIRE(r.size() == 2);
  REQUIRE(r["x"] == 10);
  REQUIRE(r["y"] == 20);
}

TEST("merge_with_override: empty a") {
  Map a = {};
  Map b = {{"x", 5}};
  auto r = svarg::merge_with_override(a, b);
  REQUIRE(r.size() == 1);
  REQUIRE(r["x"] == 5);
}

TEST("merge_with_override: empty b") {
  Map a = {{"x", 5}};
  Map b = {};
  auto r = svarg::merge_with_override(a, b);
  REQUIRE(r.size() == 1);
  REQUIRE(r["x"] == 5);
}

TEST("merge_with_override: both empty") {
  Map a = {}, b = {};
  auto r = svarg::merge_with_override(a, b);
  REQUIRE(r.empty());
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
