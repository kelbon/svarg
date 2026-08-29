#include "svarg/frontend/openapi/openapi_object.hpp"
#include "svarg/frontend/openapi/reference.hpp"
#include "svarg/utils/json_pointer.hpp"

#include <moko3/moko3.hpp>

#include <vector>
#include <string>

using segments = std::vector<std::string>;

static segments collect(const svarg::json_pointer& s) {
  segments result;
  for (auto seg : s.segments())
    result.emplace_back(seg);
  return result;
}

// --- json_pointer: add/remove/view ---

TEST("json_pointer: basic") {
  svarg::json_pointer s;
  REQUIRE(s.view() == "");
  REQUIRE(collect(s) == segments{});
  s.add_key("foo");
  REQUIRE(s.view() == "/foo");
  REQUIRE(collect(s) == segments{"foo"});
  s.add_key("bar");
  s.add_key("baz");
  REQUIRE(s.view() == "/foo/bar/baz");
  REQUIRE(collect(s) == segments{"foo", "bar", "baz"});
  s.rm_last_segment();
  REQUIRE(collect(s) == segments{"foo", "bar"});
  REQUIRE(s.view() == "/foo/bar");
  s.rm_last_segment();
  REQUIRE(collect(s) == segments{"foo"});
  REQUIRE(s.view() == "/foo");
  s.rm_last_segment();
  REQUIRE(collect(s) == segments{});
  REQUIRE(s.view() == "");
}
// TODO тесты из json pointer RFC на escaped

TEST("json_pointer: multiscope") {
  svarg::json_pointer s = "/foo/bar/baz"_jp;
  REQUIRE(s.view() == "/foo/bar/baz");
  REQUIRE(collect(s) == segments{"foo", "bar", "baz"});
  s.rm_last_segment();
  REQUIRE(collect(s) == segments{"foo", "bar"});
  REQUIRE(s.view() == "/foo/bar");
  s.rm_last_segment();
  REQUIRE(collect(s) == segments{"foo"});
  REQUIRE(s.view() == "/foo");
  s.rm_last_segment();
  REQUIRE(collect(s) == segments{});
  REQUIRE(s.view() == "");
}

TEST("json_pointer: add after remove") {
  svarg::json_pointer s;
  s.add_json_ptr("foo");
  s.add_json_ptr("bar");
  s.rm_last_segment();
  s.add_json_ptr("qux");
  REQUIRE(s.view() == "/foo/qux");
}

TEST("json_pointer: add remove interleaved") {
  svarg::json_pointer s;
  s.add_json_ptr("a");
  s.add_json_ptr("b");
  s.rm_last_segment();
  s.add_json_ptr("c");
  s.add_json_ptr("d");
  s.rm_last_segment();
  REQUIRE(s.view() == "/a/c");
}

// --- iterator ---

TEST("json_pointer: iterate single segment") {
  svarg::json_pointer s;
  s.add_json_ptr("foo");
  REQUIRE(collect(s) == segments{"foo"});
}

TEST("json_pointer: iterator begin equals end on empty") {
  svarg::json_pointer s;
  REQUIRE(s.segments().begin() == s.segments().end());
  s.add_json_ptr("x");
  REQUIRE(s.segments().begin() != s.segments().end());
}

TEST("json_pointer: iterator post-increment") {
  svarg::json_pointer s;
  s.add_json_ptr("a");
  s.add_json_ptr("b");
  auto it = s.segments().begin();
  REQUIRE(*it++ == "a");
  REQUIRE(*it++ == "b");
  REQUIRE(it == s.segments().end());
}

TEST("json_pointer: iterator dereference is idempotent") {
  svarg::json_pointer s;
  s.add_json_ptr("foo");
  s.add_json_ptr("bar");
  auto it = s.segments().begin();
  REQUIRE(*it == "foo");
  REQUIRE(*it == "foo");
}

TEST("json encode/decode") {
  std::string_view input = R"(
  {
    "$ref": "/components/abc",
    "description": "my description",
    "x-some-field": 0,
    "something2": "nothing"
  })";
  svarg::reference_object expected;
  expected.ref_ = "/components/abc";
  expected.description = "my description";
  expected._keyfield_override_fields = {
      {"x-some-field", svarg::Any(0)},
      {"something2", svarg::Any("nothing")},
  };
  auto ref = svarg::from_json<svarg::reference_object>(input);

  REQUIRE(ref == expected);
  boost::json::value json = svarg::to_json(ref);
  auto ref2 = svarg::from_json<svarg::reference_object>(json);
  REQUIRE(ref2 == expected);
  REQUIRE(ref2 == ref);
}

template <typename T>
void test_json_encode_decode(std::string_view input, T expected) {
  auto ref = svarg::from_json<T>(input);
  REQUIRE(to_json(ref) == to_json(expected));
}

TEST("json encode/decode schema and meta") {
  {
    svarg::schema_object expected_schema;
    expected_schema._keyfield_boolean_schema = true;
    test_json_encode_decode("true", expected_schema);
    expected_schema._keyfield_boolean_schema = false;
    test_json_encode_decode("false", expected_schema);
  }
  {
    svarg::schema_object expected_schema;
    expected_schema.type.emplace().values.push_back("sometype");
    test_json_encode_decode("{ \"type\": \"sometype\" }", expected_schema);
    expected_schema.SVARG_META_KEYFIELD["x-key"] = svarg::Any("string1");
    test_json_encode_decode("{ \"type\": \"sometype\", \"x-key\": \"string1\" }", expected_schema);
  }
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
