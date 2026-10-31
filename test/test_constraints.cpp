#include <moko3/moko3.hpp>

#include "svarg/backend/cxx/support/common_types.hpp"

// типы повторяют то, что генерирует бекенд для схем с ограничениями (minLength, minimum и т.д.)

using namespace svarg::cxx_support;
using svarg::arrayof;

struct name_t {
  static constexpr parse_tag_e parse_tag = parse_tag_e::fundamental;

  std::string _dATA;

  // два ограничения: например после allOf<Base, {...}> требования обеих частей складываются
  static constexpr std::array<constraints_t, 2> constraints = {{
      constraints_t{.min_length = 2u, .max_length = 4u},
      constraints_t{.pattern = "^[a-z]+$"},
  }};
};

struct utf8_name_t {
  static constexpr parse_tag_e parse_tag = parse_tag_e::fundamental;

  std::string _dATA;

  static constexpr std::array<constraints_t, 1> constraints = {{
      constraints_t{.max_length = 3u},
  }};
};

struct even_t {
  static constexpr parse_tag_e parse_tag = parse_tag_e::fundamental;

  integer _dATA;

  static constexpr std::array<constraints_t, 1> constraints = {{
      constraints_t{
          .minimum = bound_t(int64_t(0)),
          .maximum = bound_t(int64_t(10)),
          .multiple_of = bound_t(int64_t(2)),
      },
  }};
};

struct ratio_t {
  static constexpr parse_tag_e parse_tag = parse_tag_e::fundamental;

  number _dATA;

  static constexpr std::array<constraints_t, 1> constraints = {{
      constraints_t{
          .exclusive_minimum = bound_t(double(0.5)),
          .exclusive_maximum = bound_t(double(1.5)),
          .multiple_of = bound_t(double(0.1)),
      },
  }};
};

struct ids_t {
  static constexpr parse_tag_e parse_tag = parse_tag_e::array;

  arrayof<integer> _dATA;

  static constexpr std::array<constraints_t, 1> constraints = {{
      constraints_t{.min_items = 1u, .max_items = 3u, .unique_items = true},
  }};
};

struct patch_t {
  static constexpr parse_tag_e parse_tag = parse_tag_e::object;

  std::optional<std::string> a;
  std::optional<std::string> b;

  using additional_properties_type = void;

  [[nodiscard]] static constexpr bool is_required(std::string_view) noexcept {
    return false;
  }

  static constexpr std::array<std::string_view, 2> field_names = {"a", "b"};

  static constexpr std::array<constraints_t, 1> constraints = {{
      constraints_t{.min_properties = 1u},
  }};
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

TEST("parsed value is kept") {
  patch_t v;
  REQUIRE(!svarg::try_from_json(boost::json::parse(R"({"b": "y"})"), v).failed());
  REQUIRE(!v.a && v.b == "y");
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
