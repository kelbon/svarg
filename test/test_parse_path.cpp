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

struct trace_visitor {
  std::vector<std::string> log;

  void substr(std::string_view s) {
    log.push_back("substr " + std::string(s));
  }
  void param(std::string_view name) {
    log.push_back("param " + std::string(name));
  }
  void param(std::string_view name, std::string_view sep) {
    log.push_back("param " + std::string(name) + ',' + std::string(sep));
  }
  void pattern_end() {
    log.push_back("pattern_end");
  }
  void error(std::string_view msg) {
    log.push_back("error " + std::string(msg));
  }
  bool is_done() const noexcept {
    return false;
  }
};

[[nodiscard]] bool test_path_template(std::string_view path, std::vector<std::string> expected_output) {
  trace_visitor v;
  svarg::visit_templated_path_parts(path, v);
  return v.log == expected_output;
}

TEST("templating with separators and trailing param") {
  REQUIRE(test_path_template("/abc/{x}-{y}", {
                                                 "substr /abc/",
                                                 "param x,-",
                                                 "param y",
                                                 "pattern_end",
                                             }));

  REQUIRE(test_path_template("/abc/{file}.json", {
                                                     "substr /abc/",
                                                     "param file,.json",
                                                     "pattern_end",
                                                 }));

  REQUIRE(test_path_template("/x/{a}-{b}.{c}", {
                                                   "substr /x/",
                                                   "param a,-",
                                                   "param b,.",
                                                   "param c",
                                                   "pattern_end",
                                               }));
}

TEST("param at very start of path, with and without trailing literal") {
  REQUIRE(test_path_template("{id}", {
                                         "param id",
                                         "pattern_end",
                                     }));

  REQUIRE(test_path_template("{id}/details", {
                                                 "param id,/details",
                                                 "pattern_end",
                                             }));
}

TEST("no parameters at all") {
  REQUIRE(test_path_template("/abc/def", {
                                             "substr /abc/def",
                                             "pattern_end",
                                         }));

  REQUIRE(test_path_template("", {"pattern_end"}));
}

TEST("errors: missing separator between two params") {
  REQUIRE(test_path_template("/a/{x}{y}", {
                                              "substr /a/",
                                              "error two parameters in a row without a separator",
                                          }));

  REQUIRE(test_path_template("{x}{y}", {
                                           "error two parameters in a row without a separator",
                                       }));

  // ошибка обязана прервать разбор — хвост после {y} не попадает в лог
  REQUIRE(test_path_template("/a/{x}{y}/tail", {
                                                   "substr /a/",
                                                   "error two parameters in a row without a separator",
                                               }));
}

TEST("errors: unterminated parameter") {
  REQUIRE(test_path_template("/a/{x", {
                                          "substr /a/",
                                          "error unterminated parameter: missing '}'",
                                      }));

  REQUIRE(test_path_template("{x", {
                                       "error unterminated parameter: missing '}'",
                                   }));
}

TEST("errors: empty parameter name") {
  REQUIRE(test_path_template("/a/{}", {
                                          "substr /a/",
                                          "error empty parameter name '{}'",
                                      }));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
