#include "svarg/utils/path_tools.hpp"

#include <moko3/moko3.hpp>

#include <string>
#include <utility>
#include <vector>

TEST("validate path") {
  using svarg::validate_maybe_templated_path_parameters;
  REQUIRE(validate_maybe_templated_path_parameters("/orders"));
  REQUIRE(validate_maybe_templated_path_parameters("/"));
  REQUIRE(validate_maybe_templated_path_parameters("/orders/{id}"));
  REQUIRE(validate_maybe_templated_path_parameters("/{a}/x/{b}"));
  REQUIRE(validate_maybe_templated_path_parameters("/files/{name}.{ext}"));
  // каждый параметр ровно один раз
  REQUIRE(!validate_maybe_templated_path_parameters("/{a}/{a}"));
  REQUIRE(!validate_maybe_templated_path_parameters("/{a}/{b}/{a}"));
  REQUIRE(!validate_maybe_templated_path_parameters("/{}"));
  REQUIRE(!validate_maybe_templated_path_parameters("/{a"));
  REQUIRE(!validate_maybe_templated_path_parameters("/{a}{b}"));
}

TEST("count_in_str") {
  REQUIRE(svarg::count_in_str("", "a") == 0);
  REQUIRE(svarg::count_in_str("abc", "x") == 0);
  REQUIRE(svarg::count_in_str("/{a}/{a}", "{a}") == 2);
  REQUIRE(svarg::count_in_str("{a}{ab}", "{a}") == 1);
  // без перекрытий
  REQUIRE(svarg::count_in_str("aaa", "aa") == 1);
  REQUIRE(svarg::count_in_str("aaaa", "aa") == 2);
}

using pairs_t = std::vector<std::pair<std::string, std::string>>;

static auto collect_to(pairs_t& out) {
  return [&out](std::string_view k, std::string_view v) -> svarg::error_code {
    out.emplace_back(k, v);
    return {};
  };
}

TEST("visit_query") {
  pairs_t form, query, raw;
  std::string_view q = "a=1&b=x+y&&c&d=%41%2";
  REQUIRE(!svarg::visit_query<true>(q, collect_to(form)));
  REQUIRE(!svarg::visit_query<false>(q, collect_to(query)));
  REQUIRE(!svarg::visit_query_raw(q, collect_to(raw)));
  // пустые пары пропускаются, пара без '=' - пустое значение, неполный '%' остаётся как есть
  REQUIRE((form == pairs_t{{"a", "1"}, {"b", "x y"}, {"c", ""}, {"d", "A%2"}}));
  // '+' - пробел только в application/x-www-form-urlencoded
  REQUIRE((query == pairs_t{{"a", "1"}, {"b", "x+y"}, {"c", ""}, {"d", "A%2"}}));
  REQUIRE((raw == pairs_t{{"a", "1"}, {"b", "x+y"}, {"c", ""}, {"d", "%41%2"}}));

  // первая ошибка прерывает обход
  int calls = 0;
  svarg::error_code ec = svarg::visit_query<false>("a=1&b=2", [&](std::string_view, std::string_view) {
    ++calls;
    return svarg::error_code(svarg::parse_error("stop"));
  });
  REQUIRE(ec && calls == 1);
}

TEST("visit_cookies") {
  pairs_t got;
  REQUIRE(!svarg::visit_cookies(R"(a=1; b="q"; c=)", collect_to(got)));
  REQUIRE((got == pairs_t{{"a", "1"}, {"b", "q"}, {"c", ""}}));
  got.clear();
  REQUIRE(!svarg::visit_cookies(" a=1;", collect_to(got)));
  REQUIRE((got == pairs_t{{"a", "1"}}));

  auto fails = [](std::string_view header) {
    pairs_t ignored;
    return svarg::visit_cookies(header, collect_to(ignored)).failed();
  };
  REQUIRE(fails("a=1;; b=2"));  // пустая пара
  REQUIRE(fails("a"));          // нет '='
  REQUIRE(fails("=1"));         // пустое имя
  REQUIRE(fails("a=b c"));      // пробел в значении
  REQUIRE(fails("a=b,c"));
  REQUIRE(fails("a=b\\c"));
}

static std::strong_ordering inverse_ordering(std::strong_ordering r) {
  if (r == std::strong_ordering::less)
    return std::strong_ordering::greater;
  if (r == std::strong_ordering::greater)
    return std::strong_ordering::less;
  return std::strong_ordering::equal;
}

#define TEST_PATH_SPECIALIZED(A, B, R)                                        \
  REQUIRE(svarg::compare_paths_specialized(A, B) == std::strong_ordering::R); \
  REQUIRE(svarg::compare_paths_specialized(B, A) == inverse_ordering(std::strong_ordering::R));

TEST("compare_paths_specialized") {
  TEST_PATH_SPECIALIZED("/v1", "/orders", equivalent);
  TEST_PATH_SPECIALIZED("/orders", "/v1", equivalent);
  TEST_PATH_SPECIALIZED("/v1", "/v1", equivalent);
  TEST_PATH_SPECIALIZED("/v2/{str}.json", "/v2/{name}.yaml", equivalent);
  TEST_PATH_SPECIALIZED("/v2/{x}-{y}", "/v2/{name}", greater);
  TEST_PATH_SPECIALIZED("/v2/{x}-{y}.foo", "/v2/{name}", greater);
  TEST_PATH_SPECIALIZED("/v2/{x}-{y}", "/v2/{name}.foo", equivalent);
  TEST_PATH_SPECIALIZED("/v2/{x}-{y}", "/v2/{name}-/", less);
  TEST_PATH_SPECIALIZED("/v2", "/{version}", greater);
  TEST_PATH_SPECIALIZED("/{version}", "/v2", less);
  TEST_PATH_SPECIALIZED("/{id}", "/{version}", equivalent);
  TEST_PATH_SPECIALIZED("/{version}", "/{id}", equivalent);
  TEST_PATH_SPECIALIZED("/{id}", "/{id}", equivalent);
  TEST_PATH_SPECIALIZED("/v1/orders", "/v1/{id}", greater);
  TEST_PATH_SPECIALIZED("/v1/{id}", "/v1/orders", less);
  TEST_PATH_SPECIALIZED("/v1/{id}", "/{version}/orders", equivalent);
  TEST_PATH_SPECIALIZED("/{version}/orders", "/v1/{id}", equivalent);
  TEST_PATH_SPECIALIZED("/v1/orders", "/v2/products", equivalent);
  TEST_PATH_SPECIALIZED("/{version}/{id}", "/v1/orders", less);
  TEST_PATH_SPECIALIZED("/v1/orders", "/{version}/{id}", greater);
  TEST_PATH_SPECIALIZED("/", "/v1", equivalent);
  TEST_PATH_SPECIALIZED("/v1/orders/{id}", "/v1/{version}/{id}", greater);
}

TEST("match_path_pattern") {
  REQUIRE(svarg::match_path_pattern("/a/{x}/b", "/a/1/b"));
  REQUIRE(!svarg::match_path_pattern("/a/{x}/b", "/a/1/b/c"));  // лишний хвост
  REQUIRE(!svarg::match_path_pattern("/a/{x}/b", "/a/1/bc"));
  REQUIRE(
      svarg::match_path_pattern("/files/{name}.{ext}", "/files/dir/a.txt"));  // '/' в значении принимается
  REQUIRE(!svarg::match_path_pattern("/a", "/ab"));
  REQUIRE(svarg::match_path_pattern("/files/{name}.{ext}", "/files/a.tar.gz"));
  REQUIRE(!svarg::match_path_pattern("/files/{name}.{ext}", "/files/a"));
  // последний параметр забирает остаток пути вместе с '/'
  REQUIRE(svarg::match_path_pattern("/repos/{owner}/contents/{path}", "/repos/me/contents/a/b/c"));
  REQUIRE(!svarg::match_path_pattern("/repos/{owner}/contents/{path}", "/repos/me/other/a"));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
