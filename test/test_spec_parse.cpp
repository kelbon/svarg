#include "svarg/openapi.hpp"
#include <moko3/moko3.hpp>
#include "svarg/serialize.hpp"
#include "svarg/spec_loader.hpp"
#include "svarg/utils/json_pretty.hpp"
#include <fstream>

#include "svarg/ir/ir_object.hpp"

// TODO обновить yaml либу (коммит с исправлением пробелов)

TEST("spec loader") {
  svarg::spec_loader loader;

  loader.set_specs_dir(std::filesystem::current_path());
  loader.load_spec("./test_specs/TS29510_Nnrf_NFDiscovery.yaml");
  loader.load_spec("./test_specs/TS29571_CommonData.yaml");
  svarg::ir::ir_object o = loader.generate();
  for (auto&& x : o.types) {
    if (x.spec() == "test_specs/TS29571_CommonData.yaml")
      std::cout << x.jsonptr() << " NAME " << x.name.value_or("<unnamed>") << std::endl;
  }
}
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
  std::filesystem::path p = "./test_raw_links_specs/openapi/openapi.yaml";
  options.input_specfile_path = p;
  boost::json::value v;
  svarg::pass_as_json(p, [&](std::string_view str) { v = boost::json::parse(str); });
  v = svarg::resolve_raw_references(std::move(v), options);
  std::ofstream f("mdamda.json");
  svarg::pretty_print(f, v);
  svarg::openapi_object o = svarg::parse_spec(std::move(v), options);
  std::ofstream f2("mdamdaout.json");
  svarg::pretty_print(f2, svarg::serialize_as_json(o));
}

TEST("network links") {
  svarg::svarg_options options;
  options.resolve_url_refs = true;

  constexpr std::string_view api = R"(
openapi: "3.1.0"
info:
  title: Test API
  version: "1.0.0"
paths:
  /person:
    get:
      responses:
        "200":
          description: OK
          content:
            application/json:
              schema:
                $ref: "https://raw.githubusercontent.com/kelbon/TestYamlsRepo/main/schemas/common.yaml"
)";

  constexpr std::string_view expected_json = R"(
{
    "openapi" : "3.1.0",
    "info" : {
        "title" : "Test API",
        "version" : "1.0.0"
    },
    "servers" : [
        {
            "url" : "/"
        }
    ],
    "paths" : {
        "/person" : {
            "get" : {
                "responses" : {
                    "200" : {
                        "description" : "OK",
                        "content" : {
                            "application/json" : {
                                "schema" : {
                                    "type" : "object",
                                    "properties" : {
                                        "name" : {
                                            "type" : "string"
                                        },
                                        "address" : {
                                            "type" : "object",
                                            "properties" : {
                                                "street" : {
                                                    "type" : "string"
                                                },
                                                "city" : {
                                                    "type" : "string"
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
    )";
  svarg::openapi_object o = svarg::parse_yaml_spec(api, options);
  REQUIRE(boost::json::parse(expected_json) == svarg::serialize_as_json(o));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
