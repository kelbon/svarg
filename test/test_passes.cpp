#include <print>

#include "moko3/moko3.hpp"
#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"
#include "svarg/ir/optimization_passes.hpp"
#include "svarg/frontend/openapi/generate_ir.hpp"
#include "svarg/utils/yamljson.hpp"

using namespace svarg;
using ir::ir_object;
using ir::ir_options;

// входы и ожидаемые результаты - файлы IR (в формате вывода svarg-ir) или спеки, читаются прямо из исходников
static const fs::path passes_dir = fs::path(__FILE__).parent_path() / "passes";
// true - записать результаты в ожидаемые файлы вместо сравнения (только руками, затем смотреть git diff)
static constexpr bool update_expected = false;

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

// IR может быть написан руками, поэтому проверяется
static ir_object load_ir(const fs::path& file) {
  ir_object ir;
  pass_as_json(passes_dir / file,
               [&](std::string_view json) { ir = from_json<ir_object>(boost::json::parse(json)); });
  ir.validate();
  return ir;
}

// сравнение через json: ir_object::operator== сравнивает схемы только по spec/jsonptr/name
static bool check_result(const ir_object& result, const fs::path& expected_file) {
  if (update_expected) {
    write_into(passes_dir / expected_file, to_yaml_string(result));
    return true;
  }
  if (to_json(result) == to_json(load_ir(expected_file)))
    return true;
  std::println(stderr, "result differs from {}:\n{}", expected_file.generic_string(), to_yaml_string(result));
  return false;
}

// вход - IR из файла
static bool test_ir(const fs::path& input_file, const fs::path& expected_file, const ir_options& options,
                    auto pass) {
  return check_result(pass(load_ir(input_file), options).ir, expected_file);
}

// вход - IR, который фронтенд OpenAPI делает из спеки (с тем, что он делает всегда)
static bool test_spec(const fs::path& spec_file, const fs::path& expected_file,
                      const openapi_options& options, auto pass) {
  ir_object ir = openapi2ir(passes_dir, {spec_file}, options);
  return check_result(pass(std::move(ir), options.ir_opts).ir, expected_file);
}

TEST("rm duplicates anyOf") {
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_ir("rm_duplicates_anyof/input.yaml", "rm_duplicates_anyof/output.yaml", opts,
                  &ir::pass_combinators_flatten));
}

TEST("flatten nested anyOf") {
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_ir("flatten_nested_anyof/input.yaml", "flatten_nested_anyof/output.yaml", opts,
                  &ir::pass_combinators_flatten));
  opts.allow_rm_unused = svarg::ir::remove_category_e::any;
  REQUIRE(test_ir("flatten_nested_anyof/output.yaml", "empty.yaml", opts, &ir::pass_rm_unused_schemas));
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_ir("flatten_nested_anyof/output.yaml", "flatten_nested_anyof/output.yaml", opts,
                  make_pipe(&ir::pass_rm_trivial_schemas)));
}

TEST("double not") {
  // именованная DoubleNot при noone не удаляется, а становится string; средний not остаётся бесполезным
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_ir("double_not/input.yaml", "double_not/output.yaml", opts, &ir::pass_combinators_flatten));
  opts.allow_rm_unused = svarg::ir::remove_category_e::only_inline_schemas;
  REQUIRE(test_ir("double_not/output.yaml", "double_not/output_after_rm_unused.yaml", opts,
                  &ir::pass_rm_unused_schemas));
  opts.allow_rm_unused = svarg::ir::remove_category_e::any;
  REQUIRE(test_ir("double_not/output.yaml", "empty.yaml", opts, &ir::pass_rm_unused_schemas));
}

TEST("triple not") {
  // TripleNot становится not string
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_ir("triple_not/input.yaml", "triple_not/output.yaml", opts, &ir::pass_combinators_flatten));
  opts.allow_rm_unused = svarg::ir::remove_category_e::any;
  REQUIRE(test_ir("triple_not/output.yaml", "empty.yaml", opts, &ir::pass_rm_unused_schemas));
}

TEST("not any/never") {
  // именованные NotNever/NotAny при noone не удаляются, а становятся any/never
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(
      test_ir("not_any_never/input.yaml", "not_any_never/output.yaml", opts, &ir::pass_combinators_flatten));
}

TEST("single type in anyof/allof/oneof") {
  // преобразуются в просто типы, но не удаляются как фундаментальные, так как именованы
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::any;
  REQUIRE(test_ir("single_type_combinators/input.yaml", "empty.yaml", opts, &ir::pass_combinators_flatten));
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
  ir_options opts;
  opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
  REQUIRE(test_ir("multilevel_combinators/input.yaml", "multilevel_combinators/output.yaml", opts,
                  &ir::pass_combinators_flatten));

  opts.allow_rm_unused = svarg::ir::remove_category_e::only_inline_schemas;
  REQUIRE(test_ir("multilevel_combinators/input.yaml", "multilevel_combinators/output_rus.yaml", opts,
                  make_pipe(&ir::pass_combinators_flatten, &ir::pass_rm_unused_schemas)));
}

TEST("pass transform allofs") {
  REQUIRE(test_ir("transform_allofs/input.yaml", "transform_allofs/output.yaml", ir_options{},
                  &ir::pass_transform_allofs));
}

TEST("pass transform allofs 2") {
  REQUIRE(test_ir("transform_allofs_2/input.yaml", "transform_allofs_2/output.yaml", ir_options{},
                  &ir::pass_transform_allofs));
}

// allOf из строк с ограничениями: тип сводится к строке, ограничения всех частей сохраняются
TEST("allof extract requirements") {
  ir_options opts;
  REQUIRE(test_ir("allof_extract_requirements/input.yaml", "allof_extract_requirements/expected.yaml", opts,
                  &ir::pass_allof_extract_requirements));
}

TEST("pass transform allofs keeps constraints") {
  REQUIRE(test_ir("transform_allofs_keeps_constraints/input.yaml",
                  "transform_allofs_keeps_constraints/output.yaml", ir_options{},
                  &ir::pass_transform_allofs));
}

TEST("pass transform allofs keeps format") {
  REQUIRE(test_ir("transform_allofs_keeps_format/input.yaml", "transform_allofs_keeps_format/output.yaml",
                  ir_options{}, &ir::pass_transform_allofs));
}

TEST("pass transform allofs drops conflicting formats") {
  REQUIRE(test_ir("transform_allofs_conflicting_formats/input.yaml",
                  "transform_allofs_conflicting_formats/output.yaml", ir_options{}, &ir::pass_transform_allofs));
}

// type: string + allOf: [{pattern: ...}, {pattern: ...}]: требование типа any заменяется своими требованиями
TEST("rm useless requirements flattens any requirement") {
  ir_options opts;
  REQUIRE(test_ir("rm_useless_requirements_any/input.yaml", "rm_useless_requirements_any/output.yaml", opts,
                  make_pipe(&ir::pass_rm_useless_requirements, &ir::pass_rm_unused_schemas)));
}

// {not: X} фронтенд делает схемой any с единственным требованием not X, rur заменяет её на not X
TEST("rm useless requirements inlines single requirement of any") {
  openapi_options opts;
  opts.ir_opts.enable_optimizations = false;
  REQUIRE(test_spec("inline_single_requirement/spec.yaml", "inline_single_requirement/output.yaml", opts,
                    &ir::pass_rm_useless_requirements));
}

// поле, которое только перечислено в required, при слиянии не меняет тип поля базы
TEST("pass transform allofs required only field") {
  openapi_options opts;
  opts.ir_opts.enable_optimizations = false;
  REQUIRE(test_spec("transform_allofs_required_only/spec.yaml", "transform_allofs_required_only/output.yaml",
                    opts, &ir::pass_transform_allofs));
}

TEST("merge equal schemas") {
  openapi_options opts;
  opts.ir_opts.enable_optimizations = false;
  REQUIRE(test_spec("merge_equal_schemas/spec.yaml", "merge_equal_schemas/output.yaml", opts,
                    &ir::pass_merge_equal_schemas));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
