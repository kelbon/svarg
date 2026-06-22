#pragma once

#include <string_view>

#include <boost/json/value.hpp>
#include <boost/json/parse.hpp>

#include "svarg/utils/yamljson.hpp"

namespace svarg {

// если внутри типа есть static функция parse_if_not_object(boost::json::value const&)
// то она вызывается, когда пришёл не объект чтобы распарсить агрегат
template <typename T>
struct parse {};

// TODO складывать такие поля в meta
template <typename>
struct allow_additional_field {
  [[nodiscard]] static bool allow(std::string_view name) noexcept {
    // https://spec.openapis.org/oas/v3.1.0#specification-extensions
    return name.starts_with("x-");
  }
};

template <typename T>
concept parsable_even_from_not_object = requires(boost::json::value const& v) {
  { T::parse_if_not_object(v) } -> std::same_as<T>;
};

template <typename T>
void from_json(boost::json::value const& j, T& out) {
  parse<T>{}(j, out);
}

template <typename T>
T from_json(boost::json::value const& j) {
  T value;
  from_json(j, value);
  return value;
}

template <typename T>
T from_json(std::string_view str) {
  return from_json<T>(boost::json::parse(str, {}, {.allow_comments = true}));
}

template <typename T>
T from_yaml(std::string_view str) {
  // yaml может быть пуст, json - нет. Так-то может быть null, "", {}, [], но выбран объект
  if (str.empty()) [[unlikely]]
    str = "{}";
  return from_json<T>(
      boost::json::parse(yaml2json(str), {}, boost::json::parse_options{.allow_comments = true}));
}

}  // namespace svarg
