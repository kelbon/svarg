
#include "moko3/moko3.hpp"
#include "svarg/ir/optimization_passes.hpp"

using namespace svarg;
using ir::ir_object;
using ir::ir_options;

auto pass_all = [](ir_object ir, ir_options opts) -> ir::pass_result {
  return {ir::optimization_pipeline(std::move(ir), opts), 0};
};

auto make_pipe(auto*... passes) {
  return [=](ir_object ir, ir_options opts) {
    auto onepass = [&](ir_object& x, size_t& count, auto* pass) {
      auto [irx, c] = pass(std::move(x), opts);
      x = std::move(irx);
      count += c;
    };
    size_t count = 0;
    (onepass(ir, count, passes), ...);
    return ir::pass_result{std::move(ir), count};
  };
}
static bool test_pass(std::string_view input_ir, std::string_view expected_ir, const ir_options& options,
                      auto pass) {
  ir_object in = from_yaml<ir_object>(input_ir);
  in = pass(std::move(in), options).ir;
  ir_object expected = from_yaml<ir_object>(expected_ir);
  return in == expected;
}

TEST("rm duplicates anyOf") {
  std::string_view input = R"(
"schemas":
  - "spec": "flatten_pass.yaml"
    "jsonptr": "/components/schemas/DedupAnyOf"
    "name": "DedupAnyOf"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@integer@"
          - "spec": "@string@"
          - "spec": "@string@"
)";
  std::string_view output = R"(
"schemas":
  - "spec": "flatten_pass.yaml"
    "jsonptr": "/components/schemas/DedupAnyOf"
    "name": "DedupAnyOf"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@integer@"
          - "spec": "@string@"
)";

  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_pass(input, output, opts, &ir::pass_combinators_flatten));
}

TEST("flatten nested anyOf") {
  std::string_view input = R"(
"schemas":
  - "spec": "flatten_pass.yaml"
    "jsonptr": "/components/schemas/DeepNestedAnyOf"
    "name": "DeepNestedAnyOf"
    "type":
      "anyOf":
        "schemas":
          - "spec": "flatten_pass.yaml"
            "jsonptr": "/components/schemas/DeepNestedAnyOf/anyOf/0"
  - "spec": "flatten_pass.yaml"
    "jsonptr": "/components/schemas/DeepNestedAnyOf/anyOf/0"
    "type":
      "anyOf":
        "schemas":
          - "spec": "flatten_pass.yaml"
            "jsonptr": "/components/schemas/DeepNestedAnyOf/anyOf/0/anyOf/0"
  - "spec": "flatten_pass.yaml"
    "jsonptr": "/components/schemas/DeepNestedAnyOf/anyOf/0/anyOf/0"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@string@"
)";
  std::string_view output = R"(
"schemas":
  - "spec": "flatten_pass.yaml"
    "jsonptr": "/components/schemas/DeepNestedAnyOf"
    "name": "DeepNestedAnyOf"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@string@"
)";

  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_pass(input, output, opts, &ir::pass_combinators_flatten));
  opts.allow_rm_unused = svarg::ir::remove_category_e::any;
  REQUIRE(test_pass(output, "", opts, &ir::pass_rm_unused_schemas));
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_pass(output, output, opts, make_pipe(&ir::pass_rm_trivial_schemas)));
}

TEST("double not") {
  std::string_view input = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/Abc"
    "name": "Abc"
    "type":
      "object":
        "fields": []
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/DoubleNot"
    "name": "DoubleNot"
    "type":
      "not":
        "type":
          "spec": "hmm.yaml"
          "jsonptr": "/components/schemas/DoubleNot/not"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/DoubleNot/not"
    "type":
      "not":
        "type":
          "spec": "@string@"
)";
  // верхний not заменяется на Abc, тот что посередине остаётся висеть бесполезный
  std::string_view output = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/Abc"
    "name": "Abc"
    "type":
      "object":
        "fields": []
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/DoubleNot/not"
    "type":
      "not":
        "type":
          "spec": "@string@"
)";

  std::string_view output_after_rm_unused = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/Abc"
    "name": "Abc"
    "type":
      "object":
        "fields": []
)";
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_pass(input, output, opts, &ir::pass_combinators_flatten));
  opts.allow_rm_unused = svarg::ir::remove_category_e::only_inline_schemas;
  REQUIRE(test_pass(output, output_after_rm_unused, opts, &ir::pass_rm_unused_schemas));
  opts.allow_rm_unused = svarg::ir::remove_category_e::any;
  REQUIRE(test_pass(output, "", opts, &ir::pass_rm_unused_schemas));
}

TEST("triple not") {
  std::string_view input = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/TripleNot"
    "name": "TripleNot"
    "type":
      "not":
        "type":
          "spec": "hmm.yaml"
          "jsonptr": "/components/schemas/TripleNot/not"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/TripleNot/not"
    "type":
      "not":
        "type":
          "spec": "hmm.yaml"
          "jsonptr": "/components/schemas/TripleNot/not/not"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/TripleNot/not/not"
    "type":
      "not":
        "type":
          "spec": "@string@"
  )";

  // остаётся not string
  std::string_view output = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/TripleNot/not/not"
    "type":
      "not":
        "type":
          "spec": "@string@"
)";
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_pass(input, output, opts, &ir::pass_combinators_flatten));
  opts.allow_rm_unused = svarg::ir::remove_category_e::any;
  REQUIRE(test_pass(output, "", opts, &ir::pass_rm_unused_schemas));
}

TEST("not any/never") {
  std::string_view input = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/Abc"
    "name": "Abc"
    "type":
      "array":
        "element":
          "name": "NotAny"
          "spec": "hmm.yaml"
          "jsonptr": "/components/schemas/NotAny"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/NotAny"
    "name": "NotAny"
    "type":
      "not":
        "type":
          "spec": "@any@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/NotNever"
    "name": "NotNever"
    "type":
      "not":
        "type":
          "spec": "@never@"
)";

  // NotNever/NotAny удаляются и заменяются в местах использования на any/never
  std::string_view output = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/Abc"
    "name": "Abc"
    "type":
      "array":
        "element":
          "spec": "@never@"
)";
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_pass(input, output, opts, &ir::pass_combinators_flatten));
}

TEST("single type in anyof/allof/oneof") {
  std::string_view input = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/SingleAllof"
    "name": "SingleAllof"
    "type":
      "allOf":
        "schemas":
          - "spec": "@string@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/SingleAnyof"
    "name": "SingleAnyof"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@string@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/SingleOneof"
    "name": "SingleOneof"
    "type":
      "oneOf":
        "schemas":
          - "spec": "@string@"

)";
  // преобразуются в просто типы, но не удаляются как фундаментальные, так как именованы
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::any;
  REQUIRE(test_pass(input, "{}", opts, &ir::pass_combinators_flatten));
}

TEST("multilevel oneof/anyof/allof") {
  /*
    schemas:
    FlattenAnyOf:
      anyOf:
        - type: string
        - anyOf:
            - type: integer
            - type: boolean
        - type: "null"

    # allOf<A, allOf<B, C>> -> allOf<A, B, C>
    FlattenAllOf:
      allOf:
        - type: string
        - allOf:
            - type: integer
            - type: boolean

    # oneOf<A, oneOf<B, C>> -> oneOf<A, B, C>
    FlattenOneOf:
      oneOf:
        - type: string
        - oneOf:
            - type: integer
            - type: boolean
  */
  std::string_view input = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAllOf"
    "name": "FlattenAllOf"
    "type":
      "allOf":
        "schemas":
          - "spec": "@string@"
          - "spec": "hmm.yaml"
            "jsonptr": "/components/schemas/FlattenAllOf/allOf/1"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAllOf/allOf/1"
    "type":
      "allOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAnyOf"
    "name": "FlattenAnyOf"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@null@"
          - "spec": "@string@"
          - "spec": "hmm.yaml"
            "jsonptr": "/components/schemas/FlattenAnyOf/anyOf/1"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAnyOf/anyOf/1"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenOneOf"
    "name": "FlattenOneOf"
    "type":
      "oneOf":
        "schemas":
          - "spec": "@string@"
          - "spec": "hmm.yaml"
            "jsonptr": "/components/schemas/FlattenOneOf/oneOf/1"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenOneOf/oneOf/1"
    "type":
      "oneOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"

)";
  std::string_view output = R"(
"schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAllOf"
    "name": "FlattenAllOf"
    "type":
      "allOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
          - "spec": "@string@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAllOf/allOf/1"
    "type":
      "allOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAnyOf"
    "name": "FlattenAnyOf"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
          - "spec": "@null@"
          - "spec": "@string@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAnyOf/anyOf/1"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenOneOf"
    "name": "FlattenOneOf"
    "type":
      "oneOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
          - "spec": "@string@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenOneOf/oneOf/1"
    "type":
      "oneOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
)";
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_pass(input, output, opts, &ir::pass_combinators_flatten));

  std::string_view output_rus = R"(
  "schemas":
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAllOf"
    "name": "FlattenAllOf"
    "type":
      "allOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
          - "spec": "@string@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenAnyOf"
    "name": "FlattenAnyOf"
    "type":
      "anyOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
          - "spec": "@null@"
          - "spec": "@string@"
  - "spec": "hmm.yaml"
    "jsonptr": "/components/schemas/FlattenOneOf"
    "name": "FlattenOneOf"
    "type":
      "oneOf":
        "schemas":
          - "spec": "@boolean@"
          - "spec": "@integer@"
          - "spec": "@string@"

  )";
  opts.allow_rm_unused = svarg::ir::remove_category_e::only_inline_schemas;
  REQUIRE(test_pass(input, output_rus, opts,
                    make_pipe(&ir::pass_combinators_flatten, &ir::pass_rm_unused_schemas)));
}

TEST("pass transform allofs") {
  std::string_view input = R"(
"schemas":
  - "spec": "testir"
    "jsonptr": "/components/schemas/AOF1"
    "name": "AOF1"
    "type":
      "allOf":
        "schemas":
          - "spec": "@string@"
          - "name": "Obj"
            "spec": "testir"
            "jsonptr": "/components/schemas/Obj"
  - "spec": "testir"
    "jsonptr": "/components/schemas/AOF2"
    "name": "AOF2"
    "type":
      "allOf":
        "schemas":
          - "spec": "@string@"
          - "name": "Obj"
            "spec": "testir"
            "jsonptr": "/components/schemas/Obj"
  - "spec": "testir"
    "jsonptr": "/components/schemas/Obj"
    "name": "Obj"
    "type":
      "object":
        "fields":
          - "type":
              "spec": "@string@"
            "name": "f1"
            "required": false

  )";
  std::string_view output = R"(
"schemas":
  - "spec": "testir"
    "jsonptr": "/components/schemas/AOF1"
    "name": "AOF1"
    "type":
      "fundamental": "never"
  - "spec": "testir"
    "jsonptr": "/components/schemas/AOF2"
    "name": "AOF2"
    "type":
      "fundamental": "never"
  - "spec": "testir"
    "jsonptr": "/components/schemas/Obj"
    "name": "Obj"
    "type":
      "object":
        "fields":
          - "type":
              "spec": "@string@"
            "name": "f1"
            "required": false
)";

  REQUIRE(test_pass(input, output, ir_options{}, &ir::pass_transform_allofs));
}

TEST("pass transform allofs 2") {
  std::string_view input = R"(
"schemas":
  - "spec": "testir"
    "jsonptr": "/components/schemas/BaseEventCollection"
    "name": "BaseEventCollection"
    "type":
      "object":
        "fields":
          - "type":
              "spec": "@integer@"
            "name": "collectionTimestamp"
            "required": true
          - "type":
              "spec": "testir"
              "jsonptr": "/components/schemas/BaseEventCollection/properties/records"
            "name": "records"
            "required": true
  - "spec": "testir"
    "jsonptr": "/components/schemas/BaseEventCollection/properties/records"
    "type":
      "array":
        "element":
          "spec": "@any@"
  - "spec": "testir"
    "jsonptr": "/components/schemas/QoEMetricsCollection"
    "name": "QoEMetricsCollection"
    "type":
      "allOf":
        "schemas":
          - "name": "BaseEventCollection"
            "spec": "testir"
            "jsonptr": "/components/schemas/BaseEventCollection"
          - "spec": "testir"
            "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1"
  - "spec": "testir"
    "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1"
    "type":
      "object":
        "fields":
          - "type":
              "spec": "testir"
              "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/abc"
            "name": "abc"
            "required": false
          - "type":
              "spec": "testir"
              "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/records"
            "name": "records"
            "required": true
  - "spec": "testir"
    "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/abc"
    "type":
      "object":
        "fields": []
  - "spec": "testir"
    "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/records"
    "type":
      "array":
        "element":
          "spec": "@string@"
)";
  std::string_view output = R"(
"schemas":
  - "spec": "testir"
    "jsonptr": "/components/schemas/BaseEventCollection"
    "name": "BaseEventCollection"
    "type":
      "object":
        "fields":
          - "type":
              "spec": "@integer@"
            "name": "collectionTimestamp"
            "required": true
          - "type":
              "spec": "testir"
              "jsonptr": "/components/schemas/BaseEventCollection/properties/records"
            "name": "records"
            "required": true
  - "spec": "testir"
    "jsonptr": "/components/schemas/BaseEventCollection/properties/records"
    "type":
      "array":
        "element":
          "spec": "@any@"
  - "spec": "testir"
    "jsonptr": "/components/schemas/QoEMetricsCollection"
    "name": "QoEMetricsCollection"
    "type":
      "object":
        "fields":
          - "type":
              "spec": "@integer@"
            "name": "collectionTimestamp"
            "required": true
          - "type":
              "spec": "testir"
              "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/records"
            "name": "records"
            "required": true
          - "type":
              "spec": "testir"
              "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/abc"
            "name": "abc"
            "required": false
  - "spec": "testir"
    "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1"
    "type":
      "object":
        "fields":
          - "type":
              "spec": "testir"
              "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/abc"
            "name": "abc"
            "required": false
          - "type":
              "spec": "testir"
              "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/records"
            "name": "records"
            "required": true
  - "spec": "testir"
    "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/abc"
    "type":
      "object":
        "fields": []
  - "spec": "testir"
    "jsonptr": "/components/schemas/QoEMetricsCollection/allOf/1/properties/records"
    "type":
      "array":
        "element":
          "spec": "@string@"
)";

  REQUIRE(test_pass(input, output, ir_options{}, &ir::pass_transform_allofs));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
