#include "svarg/backend/cxx/render.hpp"

#include <moko3/moko3.hpp>

#include <stdexcept>

TEST("add-format option") {
  using svarg::cxx::parse_format_option;
  using result_t = std::pair<std::string, std::string>;
  REQUIRE(parse_format_option("string+date=my::date") == result_t("string+date", "my::date"));
  REQUIRE(parse_format_option("string+date-time=std::chrono::sys_seconds") ==
          result_t("string+date-time", "std::chrono::sys_seconds"));
  REQUIRE(parse_format_option("integer+int32=int") == result_t("integer+int32", "int"));
  REQUIRE(parse_format_option("object+point=my::point") == result_t("object+point", "my::point"));
  // неверный синтаксис и тип, к которому формат неприменим, - ошибка
  for (std::string_view bad :
       {"string", "string+date", "date=x", "string+=x", "string+date=", "string=x+y", "array+x=y"}) {
    bool thrown = false;
    try {
      (void)parse_format_option(bad);
    } catch (std::invalid_argument&) {
      thrown = true;
    }
    REQUIRE(thrown);
  }
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
