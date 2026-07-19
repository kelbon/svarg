#include "svarg/utils/path_tools.hpp"

#include <moko3/moko3.hpp>

TEST("validate path") {
  REQUIRE(svarg::validate_maybe_templated_path("/orders"));
  // TODO
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
