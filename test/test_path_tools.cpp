#include "svarg/utils/path_tools.hpp"

#include <moko3/moko3.hpp>

#if 0
[[nodiscard]] constexpr size_t count_substrs(std::string_view path) {
  struct vtor_t {
    size_t count = 0;

    constexpr void substr(std::string_view s) {
      ++count;
    }
    constexpr void pattern_end() {
    }
    constexpr void param(std::string_view name) {
    }
    constexpr void param(std::string_view name, std::string_view sep) {
      substr(sep);
    }
    static consteval bool is_done() {
      return false;
    }
    constexpr void error(std::string_view msg) {
      throw svarg::validate_error("{}", msg);
    }
  };
  vtor_t vtor;
  svarg::visit_templated_path_parts(path, vtor);
  return vtor.count;
}
// неплохо приближение, гораздо быстрее, просто. Но не работает в некоторых ситуациях
// регексы лучший
std::strong_ordering match_patterns2(std::string_view a, std::string_view b) {
  return count_substrs(a) <=> count_substrs(b);
}
#endif

TEST("validate path") {
  REQUIRE(svarg::validate_maybe_templated_path_parameters("/orders"));
  // TODO
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
  REQUIRE(svarg::match_path_pattern("/files/{name}.{ext}", "/files/dir/a.txt"));  // '/' в значении принимается
  REQUIRE(!svarg::match_path_pattern("/a", "/ab"));
  REQUIRE(svarg::match_path_pattern("/files/{name}.{ext}", "/files/a.tar.gz"));
  REQUIRE(!svarg::match_path_pattern("/files/{name}.{ext}", "/files/a"));
  // последний параметр забирает остаток пути вместе с '/'
  REQUIRE(svarg::match_path_pattern("/repos/{owner}/contents/{path}", "/repos/me/contents/a/b/c"));
  REQUIRE(!svarg::match_path_pattern("/repos/{owner}/contents/{path}", "/repos/me/other/a"));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
