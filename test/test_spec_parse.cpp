#include "svarg/openapi.hpp"
#include <moko3/moko3.hpp>

TEST("parse_spec") {
  svarg::svarg_options options;
  svarg::openapi_object o1 = svarg::parse_spec_file("./test_specs/TS29510_Nnrf_NFDiscovery.yaml", options);
  svarg::openapi_object o2 = svarg::parse_spec_file("./test_specs/TS29510_Nnrf_NFManagement.yaml", options);
  (void)o1, (void)o2;
  // пока просто должно распарситься
}

struct listener : moko3::gtest_listener {
  void on_test_case_end(const moko3::test_run_info& t) noexcept override {
    moko3::gtest_listener::on_test_case_end(t);
    if (t.failed)
      std::cout << t.failreason << std::endl;
  }
};
REGISTER_TEST_LISTENER(listener);
MOKO3_MAIN;
