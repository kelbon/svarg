#include <moko3/moko3.hpp>

#include "svarg/backend/cxx/support/common_types.hpp"

// типы повторяют то, что генерирует бекенд для схем с ограничениями (minLength, minimum и т.д.)

using namespace svarg::cxx_support;
using svarg::arrayof;

struct name_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::fundamental;

  std::string svarg_implfield;

  // два ограничения: например после allOf<Base, {...}> требования обеих частей складываются
  static constexpr std::array<constraints_t, 2> svarg_constraints = {{
      constraints_t{.min_length = 2u, .max_length = 4u},
      constraints_t{.pattern = "^[a-z]+$"},
  }};
};

struct utf8_name_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::fundamental;

  std::string svarg_implfield;

  static constexpr std::array<constraints_t, 1> svarg_constraints = {{
      constraints_t{.max_length = 3u},
  }};
};

struct even_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::fundamental;

  integer svarg_implfield;

  static constexpr std::array<constraints_t, 1> svarg_constraints = {{
      constraints_t{
          .minimum = bound_t(int64_t(0)),
          .maximum = bound_t(int64_t(10)),
          .multiple_of = bound_t(int64_t(2)),
      },
  }};
};

struct ratio_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::fundamental;

  number svarg_implfield;

  static constexpr std::array<constraints_t, 1> svarg_constraints = {{
      constraints_t{
          .exclusive_minimum = bound_t(double(0.5)),
          .exclusive_maximum = bound_t(double(1.5)),
          .multiple_of = bound_t(double(0.1)),
      },
  }};
};

struct ids_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::array;

  arrayof<integer> svarg_implfield;

  static constexpr std::array<constraints_t, 1> svarg_constraints = {{
      constraints_t{.min_items = 1u, .max_items = 3u, .unique_items = true},
  }};
};

struct patch_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::object;

  std::optional<std::string> a;
  std::optional<std::string> b;

  using svarg_additional_properties_type = void;

  static constexpr std::array<field_info, 2> svarg_fields = {{{.name = "a"}, {.name = "b"}}};

  static constexpr std::array<constraints_t, 1> svarg_constraints = {{
      constraints_t{.min_properties = 1u},
  }};
};

// своё ограничение + схема-требование (например allOf с $ref)
struct short_name_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::fundamental;

  std::string svarg_implfield;

  using svarg_required_schemas = metel::typelist<name_t>;

  static constexpr std::array<constraints_t, 1> svarg_constraints = {{
      constraints_t{.max_length = 3u},
  }};
};

struct color_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::string_enum;

  enum struct svarg_values_e { red, green };
  using enum svarg_values_e;

  static constexpr std::array<std::string_view, 2> values = {"red", "green"};

  svarg_values_e svarg_implfield;
};

struct level_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::enum_;

  static constexpr std::array<integer, 2> values = {1, 3};

  integer svarg_implfield;
};

struct with_extra_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::object;

  std::optional<std::string> a;

  using svarg_additional_properties_type = integer;
  additional_properties_t<svarg_additional_properties_type> svarg_additional_properties;

  static constexpr std::array<field_info, 1> svarg_fields = {{{.name = "a"}}};
};

// additionalProperties: false
struct closed_t {
  static constexpr parse_tag_e svarg_parse_tag = parse_tag_e::object;

  std::optional<std::string> a;

  using svarg_additional_properties_type = void;
  static constexpr bool svarg_additional_properties_forbidden = true;

  static constexpr std::array<field_info, 1> svarg_fields = {{{.name = "a"}}};
};

template <typename T>
static bool parses(std::string_view json) {
  T v;
  return !svarg::try_from_json(boost::json::parse(json), v).failed();
}

TEST("string constraints") {
  REQUIRE(parses<name_t>(R"("ab")"));
  REQUIRE(parses<name_t>(R"("abcd")"));
  REQUIRE(!parses<name_t>(R"("a")"));      // minLength
  REQUIRE(!parses<name_t>(R"("abcde")"));  // maxLength
  REQUIRE(!parses<name_t>(R"("AB")"));     // pattern
  REQUIRE(!parses<name_t>(R"("ab1")"));    // pattern
}

TEST("required schema") {
  REQUIRE(parses<short_name_t>(R"("abc")"));
  REQUIRE(!parses<short_name_t>(R"("abcd")"));  // своё maxLength
  REQUIRE(!parses<short_name_t>(R"("a")"));     // minLength требования
  REQUIRE(!parses<short_name_t>(R"("AB")"));    // pattern требования
}

TEST("string length in unicode characters") {
  // 3 символа, 6 байт
  REQUIRE(parses<utf8_name_t>(R"("яяя")"));
  REQUIRE(!parses<utf8_name_t>(R"("яяяя")"));
}

TEST("integer constraints") {
  REQUIRE(parses<even_t>("0"));
  REQUIRE(parses<even_t>("4"));
  REQUIRE(parses<even_t>("10"));
  REQUIRE(!parses<even_t>("-2"));  // minimum
  REQUIRE(!parses<even_t>("12"));  // maximum
  REQUIRE(!parses<even_t>("3"));   // multipleOf
}

TEST("number constraints") {
  REQUIRE(parses<ratio_t>("1.0"));
  REQUIRE(parses<ratio_t>("0.7"));
  REQUIRE(parses<ratio_t>("1"));      // целое против дробных границ
  REQUIRE(!parses<ratio_t>("0.5"));   // exclusiveMinimum
  REQUIRE(!parses<ratio_t>("1.5"));   // exclusiveMaximum
  REQUIRE(!parses<ratio_t>("0.75"));  // multipleOf
}

TEST("array constraints") {
  REQUIRE(parses<ids_t>("[1, 2]"));
  REQUIRE(!parses<ids_t>("[]"));            // minItems
  REQUIRE(!parses<ids_t>("[1, 2, 3, 4]"));  // maxItems
  REQUIRE(!parses<ids_t>("[1, 2, 1]"));     // uniqueItems
}

TEST("object constraints") {
  REQUIRE(parses<patch_t>(R"({"a": "x"})"));
  REQUIRE(parses<patch_t>(R"({"a": "x", "b": "y"})"));
  REQUIRE(!parses<patch_t>("{}"));  // minProperties
}

TEST("additional properties") {
  with_extra_t v;
  REQUIRE(!svarg::try_from_json(boost::json::parse(R"({"a": "x", "n": 1})"), v).failed());
  REQUIRE(v.a == "x" && v.svarg_additional_properties.size() == 1 && v.svarg_additional_properties.at("n") == 1);
  REQUIRE(svarg::to_json(v) == boost::json::parse(R"({"a": "x", "n": 1})"));
  REQUIRE(!parses<with_extra_t>(R"({"a": "x", "n": "y"})"));  // неописанное свойство по своей схеме
  // additionalProperties не задано - неописанные свойства пропускаются
  patch_t p;
  REQUIRE(!svarg::try_from_json(boost::json::parse(R"({"a": "x", "c": 1})"), p).failed());
  REQUIRE(p.a == "x" && !p.b);
  // additionalProperties: false - отвергаются
  REQUIRE(parses<closed_t>(R"({"a": "x"})"));
  REQUIRE(!parses<closed_t>(R"({"a": "x", "c": 1})"));
}

TEST("parsed value is kept") {
  patch_t v;
  REQUIRE(!svarg::try_from_json(boost::json::parse(R"({"b": "y"})"), v).failed());
  REQUIRE(!v.a && v.b == "y");
}

// path/query/header/cookie: значение без JSON кодирования, затем проверка требований и ограничений

template <typename T>
static bool parses_nonbody(std::string_view str) {
  T v;
  return !parse_nonbody_from(str, v).failed();
}

TEST("nonbody string") {
  name_t v;
  REQUIRE(!parse_nonbody_from("abc", v).failed());
  REQUIRE(v.svarg_implfield == "abc");
  REQUIRE(!parses_nonbody<name_t>(R"("ab")"));  // кавычки - часть значения
  REQUIRE(!parses_nonbody<name_t>("a"));        // minLength
  REQUIRE(!parses_nonbody<name_t>("AB"));       // pattern
  REQUIRE(!parses_nonbody<short_name_t>("abcd"));
  REQUIRE(!parses_nonbody<short_name_t>("AB"));  // pattern требования
  REQUIRE(serialize_nonbody_into(v) == "abc");
  bool thrown = false;
  try {
    (void)serialize_nonbody_into(name_t{"A"});
  } catch (svarg::validate_error&) {
    thrown = true;
  }
  REQUIRE(thrown);
}

TEST("nonbody numbers") {
  REQUIRE(parses_nonbody<even_t>("4"));
  REQUIRE(!parses_nonbody<even_t>("3"));
  REQUIRE(!parses_nonbody<even_t>("4.0"));
  REQUIRE(parses_nonbody<ratio_t>("0.7"));
  REQUIRE(parses_nonbody<ratio_t>("1"));
  REQUIRE(!parses_nonbody<ratio_t>("0.5"));
  REQUIRE(!parses_nonbody<ratio_t>("x"));
  REQUIRE(serialize_nonbody_into(even_t{4}) == "4");
}

TEST("nonbody enums") {
  color_t c;
  REQUIRE(!parse_nonbody_from("green", c).failed());
  REQUIRE(c.svarg_implfield == color_t::green);
  REQUIRE(!parses_nonbody<color_t>("blue"));
  REQUIRE(serialize_nonbody_into(c) == "green");

  REQUIRE(parses_nonbody<level_t>("3"));
  REQUIRE(!parses_nonbody<level_t>("2"));
  REQUIRE(serialize_nonbody_into(level_t{3}) == "3");
}

TEST("nonbody optional") {
  std::optional<even_t> v;
  REQUIRE(!parse_nonbody_from("4", v).failed());
  REQUIRE(v && v->svarg_implfield == 4);
  REQUIRE(serialize_nonbody_into(v) == "4");
}

namespace test_types {

// свой тип со своим текстовым представлением
struct date {
  int y = 0, m = 0, d = 0;

  bool operator==(const date&) const = default;
};

}  // namespace test_types

template <>
struct svarg::cxx_support::text_value<test_types::date> {
  static std::string serialize(const test_types::date& v) {
    return std::format("{:04}-{:02}-{:02}", v.y, v.m, v.d);
  }
  static svarg::error_code parse(std::string_view s, test_types::date& v) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-' || !svarg::try_from_chars(s.substr(0, 4), v.y) ||
        !svarg::try_from_chars(s.substr(5, 2), v.m) || !svarg::try_from_chars(s.substr(8, 2), v.d))
      return svarg::parse_error("bad date `{}`", s);
    return {};
  }
};

TEST("nonbody user type") {
  using test_types::date;
  static_assert(is_plain_value<date> && is_plain_value<std::optional<date>>);
  date v;
  REQUIRE(!parse_nonbody_from("2026-11-04", v).failed());
  REQUIRE(v == date{2026, 11, 4});
  REQUIRE(serialize_nonbody_into(v) == "2026-11-04");
  REQUIRE(parse_nonbody_from("04.11.2026", v).failed());

  std::optional<date> o;
  REQUIRE(!parse_nonbody_from("2000-01-02", o).failed());
  REQUIRE(o == date{2000, 1, 2});
  // text/plain и параметры с `content:` тоже через text_value
  REQUIRE(encode_value<"text/plain">(*o) == "2000-01-02");
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
