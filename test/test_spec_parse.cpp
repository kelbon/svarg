#include "svarg/frontend/openapi/openapi.hpp"
#include <moko3/moko3.hpp>
#include "svarg/to_json_value.hpp"
#include "svarg/frontend/openapi/generate_ir.hpp"
#include "svarg/utils/json_pretty.hpp"
#include <fstream>

#include "svarg/ir/ir_object.hpp"

TEST("json is an yaml") {
  std::string json = R"({"name":"Alice","age":30,"tags":["dev","cpp"]})";
  std::string out = svarg::yaml2json(json);
  REQUIRE(boost::json::parse(json) == boost::json::parse(out));
}

TEST("generate ir from openapi") {
  svarg::ir::ir_object o = svarg::openapi2ir(
      std::filesystem::current_path(),
      {"./test_specs/TS29510_Nnrf_NFDiscovery.yaml", "./test_specs/TS29571_CommonData.yaml"},
      {.ir_opts = {.allow_rm_unused = svarg::ir::remove_category_e::noone}});

  REQUIRE(o.schemas && o.schemas->size() == 2124);
  REQUIRE(o.operations && o.operations->size() == 6);
  REQUIRE(o.webhooks && o.webhooks->size() == 1);
}

TEST("generate ir with allofs") {
  // сложный случай с allOf когда должно сработать объединение в один объект
  svarg::ir::ir_object o =
      svarg::openapi2ir(std::filesystem::current_path(), {"./test_specs/TS26512_EventExposure.yaml"},
                        {.ir_opts = {.allow_rm_unused = svarg::ir::remove_category_e::noone}});

  REQUIRE(o.schemas && o.schemas->size() == 188);
  REQUIRE(!o.operations);
  REQUIRE(!o.webhooks);
}

TEST("operations with the same method and path shape") {
  // /a/{x} и /a/{y} - один путь (path-templating), вторая операция не должна молча пропасть
  svarg::write_into("conflicting_paths.yaml", R"(
openapi: 3.1.0
info: {title: c, version: "1"}
servers: [{url: /}]
paths:
  /a/{x}:
    get:
      operationId: one
      parameters: [{name: x, in: path, required: true, schema: {type: string}}]
      responses: {"200": {description: ok}}
  /a/{y}:
    get:
      operationId: two
      parameters: [{name: y, in: path, required: true, schema: {type: string}}]
      responses: {"200": {description: ok}}
)");
  std::string error;
  try {
    (void)svarg::openapi2ir(std::filesystem::current_path(), {"./conflicting_paths.yaml"}, {});
  } catch (std::exception& e) {
    error = e.what();
  }
  REQUIRE(error.contains("conflicts with"));
}

TEST("callbacks with the same method") {
  // у калбеков нет пути, они различаются по id
  svarg::write_into("same_method_callbacks.yaml", R"(
openapi: 3.1.0
info: {title: cb, version: "1"}
servers: [{url: /}]
paths:
  /sub:
    post:
      operationId: sub
      responses: {"200": {description: ok}}
      callbacks:
        onA:
          '{$request.body#/a}':
            post:
              operationId: cbA
              responses: {"200": {description: ok}}
        onB:
          '{$request.body#/b}':
            post:
              responses: {"200": {description: ok}}
)");
  svarg::ir::ir_object o =
      svarg::openapi2ir(std::filesystem::current_path(), {"./same_method_callbacks.yaml"}, {});
  REQUIRE(o.webhooks && o.webhooks->size() == 2);
}

TEST("parse_spec") {
  svarg::openapi_options options;
  svarg::openapi_object o1 = svarg::parse_spec_file("./test_specs/TS29510_Nnrf_NFDiscovery.yaml", options);
  svarg::openapi_object o2 = svarg::parse_spec_file("./test_specs/TS29510_Nnrf_NFManagement.yaml", options);
  (void)o1, (void)o2;

  // пока просто должно распарситься
}

TEST("raw_refs") {
  svarg::openapi_options options;
  std::filesystem::path p = "./test_raw_links_specs/openapi/openapi.yaml";
  boost::json::value v;
  svarg::pass_as_json(p, [&](std::string_view str) { v = boost::json::parse(str); });
  v = svarg::resolve_raw_references(std::move(v), p, options);
  svarg::write_into("mdamda.json", svarg::to_json_string(v));
  svarg::openapi_object o = svarg::parse_spec(std::move(v), options);
  svarg::write_into("mdamdaout.json", svarg::to_json_string(o));
}

// часто отваливается всилу то гитхаба то ещё чего
#define SVARG_CI
// на CI нельзя идти в сеть так напрямую
#ifndef SVARG_CI
TEST("network links") {
  svarg::openapi_options options;
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
  svarg::openapi_object o = svarg::parse_yaml_spec(api, /*specfile=*/{}, options);
  REQUIRE(boost::json::parse(expected_json) == svarg::to_json(o));
}
#endif

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
// TODO тесты на парсинг разных типов. В том числе рекурсивных, с всякими anyof и тд
// TODO подключить позже измерялку, чтобы понять где теряется больше всего времени

// TODO тест который поможет понять что всё правильно везде лежит (json ptr / file)
// TODO override ссылками всякими заниматься (и на них тоже тест)
