#include <moko3/moko3.hpp>

// сгенерировано из generated_api/names.yaml с --simple-identifiers false (см. test/CMakeLists.txt)
#include "names/server.hpp"

TEST("unique identifiers and qualified type names") {
  using names::components::schemas::Holder;
  Holder h;
  h.label.n = "x";
  h.labels.emplace().emplace_back().n = "y";
  // "+1" и "-1" - разные поля
  h.Zx2b1 = 1;
  h.Z01 = 2;
  boost::json::value j = svarg::to_json(h);
  REQUIRE(j.at("+1") == 1 && j.at("-1") == 2 && j.at("label").at("n") == "x");
  Holder back;
  REQUIRE(!svarg::try_from_json(j, back).failed());
  REQUIRE(back.Zx2b1 == 1 && back.Z01 == 2 && back.labels && back.labels->at(0).n == "y");
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
