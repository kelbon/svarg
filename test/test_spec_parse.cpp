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

TEST("yaml with document marker") {
  // так начинается, например, спека GitHub
  REQUIRE(boost::json::parse(svarg::yaml2json("---\na: 1\n")) == boost::json::parse(R"({"a": 1})"));
  bool thrown = false;
  try {
    (void)svarg::yaml2json("---\na: 1\n---\nb: 2\n");
  } catch (std::exception&) {
    thrown = true;
  }
  REQUIRE(thrown);
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

TEST("discriminator with inline schemas is ignored") {
  // как POST /repos/{owner}/{repo}/check-runs в спеке GitHub
  svarg::write_into("inline_discriminator.yaml", R"(
openapi: 3.0.3
info: {title: d, version: "1"}
paths: {}
components:
  schemas:
    Run:
      type: object
      discriminator: {propertyName: status}
      oneOf:
        - properties: {status: {enum: [completed]}}
          required: [status, conclusion]
        - properties: {status: {enum: [queued, in_progress]}}
)");
  svarg::openapi_options opts;
  opts.ir_opts.enable_optimizations = false;
  svarg::ir::ir_object o =
      svarg::openapi2ir(std::filesystem::current_path(), {"./inline_discriminator.yaml"}, opts);
  bool found = false;
  for (const svarg::ir::schema& s : *o.schemas) {
    if (const auto* oneof = s.type.if_oneof()) {
      found = true;
      REQUIRE(!oneof->discriminator);
    }
  }
  REQUIRE(found);
}

TEST("several types with object and array") {
  // OpenAPI 3.1, как custom-property-value в спеке GitHub; null - это nullable, items может не быть
  svarg::write_into("several_types.yaml", R"(
openapi: 3.1.0
info: {title: t, version: "1"}
paths: {}
components:
  schemas:
    Multi:
      type: [string, array, object, "null"]
      properties:
        a: {type: integer}
      required: [a]
    NoItems:
      type: array
)");
  svarg::openapi_options opts;
  opts.ir_opts.enable_optimizations = false;
  svarg::ir::ir_object o = svarg::openapi2ir(std::filesystem::current_path(), {"./several_types.yaml"}, opts);
  auto get = [&](std::string_view ptr) -> const svarg::ir::schema& {
    for (const svarg::ir::schema& s : *o.schemas)
      if (s.jsonptr.escaped() == ptr)
        return s;
    throw std::runtime_error(std::string(ptr));
  };
  const auto* anyof = get("/components/schemas/Multi").type.if_anyof();
  REQUIRE(anyof && anyof->schemas.size() == 3);
  const auto* arr = get("/components/schemas/Multi/@array").type.if_array();
  REQUIRE(arr && arr->element.is_fundamental(svarg::ir::fundamental_e::any));
  const auto* obj = get("/components/schemas/Multi/@object").type.if_object();
  REQUIRE(obj && obj->fields.size() == 1 && obj->find_field("a") && obj->find_field("a")->required);
  const auto* no_items = get("/components/schemas/NoItems").type.if_array();
  REQUIRE(no_items && no_items->element.is_fundamental(svarg::ir::fundamental_e::any));
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
