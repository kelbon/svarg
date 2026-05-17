#include "svarg/openapi.hpp"
#include <moko3/moko3.hpp>
#include "svarg/serialize.hpp"
#include "svarg/utils/json_pretty.hpp"
#include <fstream>

TEST("parse_spec") {
  svarg::svarg_options options;
  svarg::openapi_object o1 = svarg::parse_spec_file("./test_specs/TS29510_Nnrf_NFDiscovery.yaml", options);
  svarg::openapi_object o2 = svarg::parse_spec_file("./test_specs/TS29510_Nnrf_NFManagement.yaml", options);
  (void)o1, (void)o2;

  // svarg::pretty_print(std::cout, serialize_as_json(o1));
  // пока просто должно распарситься
}

TEST("raw_refs") {
  svarg::svarg_options options;
  options.ref_max_depth = 0;
  // TODO svarg::openapi_object o = svarg::parse_spec_file("./test_raw_links_specs/openapi/openapi.yaml",
  // options);
  // std::filesystem::path p = "./test_raw_links_specs/openapi/components/schemas/ExampleObject.yaml";
  std::filesystem::path p = "./test_raw_links_specs/openapi/openapi.yaml";
  boost::json::value v;
  options.input_specfile_path = p;
  svarg::pass_as_json(p, [&](std::string_view str) { v = boost::json::parse(str); });
  v = svarg::resolve_raw_references(std::move(v), options);
  std::fstream out(std::format("_openapi_{}.yaml", options.ref_max_depth));
  svarg::pretty_print(out, v);
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
