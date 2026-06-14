#include "svarg/utils/path_tools.hpp"

#include <moko3/moko3.hpp>

using svarg::is_equal_paths_template_insensitve;

TEST("is_equal_paths_template_insensitve") {
  REQUIRE(is_equal_paths_template_insensitve("/users", "/users"));
  REQUIRE(is_equal_paths_template_insensitve("/users/{id}", "/users/{name}"));
  REQUIRE(is_equal_paths_template_insensitve("/users/{id}/orders/{oid}", "/users/{name}/orders/{x}"));
  REQUIRE(is_equal_paths_template_insensitve("/users/{id}", "/users/{name}"));
  REQUIRE(is_equal_paths_template_insensitve("", ""));
  REQUIRE(is_equal_paths_template_insensitve("/", "/"));
  REQUIRE(!is_equal_paths_template_insensitve("/users/{id}", "/items/{id}"));
  REQUIRE(!is_equal_paths_template_insensitve("/users/profile", "/users/settings"));
  REQUIRE(!is_equal_paths_template_insensitve("/users/{id}", "/users/123"));
  REQUIRE(!is_equal_paths_template_insensitve("/users/123", "/users/{id}"));
  REQUIRE(!is_equal_paths_template_insensitve("/users/{id}/orders", "/users/{id}"));
  REQUIRE(!is_equal_paths_template_insensitve("/users/{id}", "/users/{id}/orders"));
  REQUIRE(!is_equal_paths_template_insensitve("", "/users"));
  REQUIRE(!is_equal_paths_template_insensitve("/users", ""));
  // в конце пустой сегмент
  REQUIRE(!is_equal_paths_template_insensitve("/users/{id}/", "/users/{id}"));
  // невалидные пути
  REQUIRE(!is_equal_paths_template_insensitve("//users/{id}", "/users/{id}"));
  REQUIRE(is_equal_paths_template_insensitve("//users/{id}", "//users/{name}"));
  REQUIRE(!is_equal_paths_template_insensitve("/users//{id}", "/users/{id}"));
  REQUIRE(is_equal_paths_template_insensitve("/users//{id}", "/users//{name}"));
  REQUIRE(!is_equal_paths_template_insensitve("/users/{id}//", "/users/{id}"));
}

[[nodiscard]] bool test_visit_path_segments(std::string_view path,
                                            std::initializer_list<std::string_view> expected) {
  std::vector<std::string_view> segments;
  svarg::visit_path_segments(path, [&](std::string_view s) { segments.push_back(s); });
  return std::ranges::equal(segments, expected);
}

TEST("visit_path_segments") {
  REQUIRE(test_visit_path_segments("", {}));
  REQUIRE(test_visit_path_segments("/", {""}));
  REQUIRE(test_visit_path_segments("//", {"", ""}));
  REQUIRE(test_visit_path_segments("/users/id", {"users", "id"}));
  REQUIRE(test_visit_path_segments("/users/{id}", {"users", "{id}"}));
  REQUIRE(test_visit_path_segments("/{users}/{id}", {"{users}", "{id}"}));
  REQUIRE(test_visit_path_segments("/users/{id}/", {"users", "{id}", ""}));
  REQUIRE(test_visit_path_segments("/users/{id}//", {"users", "{id}", "", ""}));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
