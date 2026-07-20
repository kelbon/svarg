#include "svarg/utils/path_tools.hpp"

#include <moko3/moko3.hpp>

// TODO эксперимент
#include <regex>

std::string make_regex(std::string_view pattern) {
  std::string out;
  out.reserve(pattern.size() * 2);

  // Символы, имеющие специальное значение в ECMAScript regex.
  constexpr std::string_view special = R"(\.^$|()[]*+?{})";

  for (size_t i = 0; i < pattern.size();) {
    if (pattern[i] == '{') {
      size_t j = pattern.find('}', i + 1);
      if (j != std::string_view::npos) {
        out += ".*";
        i = j + 1;
        continue;
      }
    }
    if (special.find(pattern[i]) != std::string_view::npos)
      out += '\\';

    out += pattern[i++];
  }

  return out;
}

std::strong_ordering match_patterns(std::string_view a, std::string_view b) {
  const std::regex ra(make_regex(a));
  const std::regex rb(make_regex(b));
  // подставляем первый во второй
  bool _1 = std::regex_match(a.begin(), a.end(), rb);
  // подставляем второй в первый
  bool _2 = std::regex_match(b.begin(), b.end(), ra);
  return _1 <=> _2;
}

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

    constexpr void error(std::string_view msg) {
      throw svarg::validate_error("{}", msg);
    }
  };
  vtor_t vtor;
  svarg::visit_templated_path_parts(path, vtor);
  return vtor.count;
}
// неплохо приближение, гораздо быстрее, просто. Но не лучший вариант, регексы лучший
std::strong_ordering match_patterns2(std::string_view a, std::string_view b) {
  return count_substrs(a) <=> count_substrs(b);
}

// TODO end

TEST("validate path") {
  REQUIRE(svarg::validate_maybe_templated_path("/orders"));
  // TODO
}

static std::strong_ordering inverse_ordering(std::strong_ordering r) {
  if (r == std::strong_ordering::less)
    return std::strong_ordering::greater;
  if (r == std::strong_ordering::greater)
    return std::strong_ordering::less;
  return std::strong_ordering::equal;
}
// #define TEST_PATH_SPECIALIZED(A, B, R)                                        \
//   REQUIRE(svarg::compare_paths_specialized(A, B) == std::strong_ordering::R); \
//   REQUIRE(svarg::compare_paths_specialized(B, A) == inverse_ordering(std::strong_ordering::R));

#define TEST_PATH_SPECIALIZED(A, B, R)                      \
  REQUIRE(match_patterns(A, B) == std::strong_ordering::R); \
  REQUIRE(match_patterns(B, A) == inverse_ordering(std::strong_ordering::R));

TEST("compare_paths_specialized") {
  // TODO? задача сводится к тому что есть два регекса и в них разрешено только *
  // дальше нужно определить у кого меньше количество возможных вариантов
  // что если попробовать подставить выдуманную строку в оба шаблона и сматчить?
  // всмысле по регексу буквально сматчить заменив все шаблонные сегменты на *
  // и подставить один шаблон в другой и наоборот
  // примеры:
  // (это имеет смысл делать только для шаблонизированных)
  // /v1* VS /orders* => не матчится ни первый ни второй,
  // /*v1 VS /*orders => опять же не сматчится ни один ни второй (ничья)
  // /v2/{str}.json VS  /v2/{name}.yaml опять же .json vs .yaml не даст никому выиграть
  // "/v2/{x}-{y}" VS "/v2/{name}"  => /v2/*-* VS /v2/* матчим первый во второй подставляя вместо * пустоту
  // /v2/- в /v2/* - влезает
  // матчим наоборот /v2/ в /v2/*-* - не влезает
  // значит первый более специален...
  // /v2/{x}-{y}.foo", "/v2/{name} матчим 1-2 /v2/-.foo в /v2/* - влезло
  // матчим 2-1 /v2/ в /v2/*-*.foo - не влезает, первый более специален
  //
  // /v2/{x}-{y} VS /v2/{name}-/
  // 1-2 /v2/- в /v2/*-/ не влезает
  // 2-1 /v2/-/ в /v2/*-* - влезает. Значит победил второй
  TEST_PATH_SPECIALIZED("/v1", "/orders", equivalent);
  TEST_PATH_SPECIALIZED("/orders", "/v1", equivalent);
  TEST_PATH_SPECIALIZED("/v1", "/v1", equivalent);
  TEST_PATH_SPECIALIZED("/v2/{str}.json", "/v2/{name}.yaml", equivalent);
  TEST_PATH_SPECIALIZED("/v2/{x}-{y}", "/v2/{name}", greater);

  TEST_PATH_SPECIALIZED("/v2/{x}-{y}.foo", "/v2/{name}", greater);
  // ??? TEST_PATH_SPECIALIZED("/v2/{x}-{y}", "/v2/{name}.foo", greater);
  TEST_PATH_SPECIALIZED("/v2/{x}-{y}", "/v2/{name}-/", less);
  // TODO {x}-{y} vs {x}+{y}
  // TODO всё же сортировку делать и по строкам для тех у кого equal
  // чтобы от порядка в ямле никогда не зависел выбор сервера
  // (существуют например {x}-{y} vs {x}+{y}, которые непонятно кто более специализирован
  // выбор зависит от порядка)
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

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;

// TODO как порядок из ямла достать ёпрст... На этапе буст джсона уже всё ломается же
