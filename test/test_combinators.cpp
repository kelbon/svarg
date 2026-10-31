#include <moko3/moko3.hpp>

#include "svarg/backend/cxx/support/common_types.hpp"

using namespace svarg::cxx_support;

TEST("oneOf: exactly one schema") {
  using T = oneof<integer, std::string>;
  T v;
  REQUIRE(!svarg::try_from_json(boost::json::parse("5"), v).failed());
  REQUIRE(v.get_if<integer>() && *v.get_if<integer>() == 5);
  REQUIRE(!svarg::try_from_json(boost::json::parse(R"("a")"), v).failed());
  REQUIRE(v.get_if<std::string>() && *v.get_if<std::string>() == "a");
  REQUIRE(svarg::try_from_json(boost::json::parse("true"), v).failed());

  using U = oneof<integer, number>;  // 5 подходит под обе
  U u;
  REQUIRE(svarg::try_from_json(boost::json::parse("5"), u).failed());
}

TEST("anyOf: every matching schema") {
  using T = anyof<integer, number, std::string>;
  T v;
  REQUIRE(!svarg::try_from_json(boost::json::parse("5"), v).failed());
  REQUIRE(v.get<0>() && v.get<1>() && !v.get<2>());
  REQUIRE(svarg::to_json(v) == boost::json::parse("5"));
  REQUIRE(svarg::try_from_json(boost::json::parse("true"), v).failed());
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
