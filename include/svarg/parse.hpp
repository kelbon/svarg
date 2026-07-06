#pragma once

#include <string_view>

#include <boost/json/value.hpp>
#include <boost/json/parse.hpp>

#include "svarg/errors.hpp"
#include "svarg/utils/yamljson.hpp"

namespace svarg {

// если внутри типа есть static функция error_code parse_if_not_object(boost::json::value const&, T& out)
// то она вызывается, когда пришёл не объект чтобы распарсить агрегат
template <typename T>
struct from_json_value {
  // error_code parse(const boost::json::value&, T& out)
};

// TODO складывать такие поля в meta
template <typename>
struct allow_additional_field {
  [[nodiscard]] static bool allow(std::string_view name) noexcept {
    // https://spec.openapis.org/oas/v3.1.0#specification-extensions
    return name.starts_with("x-");
  }
};

template <typename T>
concept parsable_even_from_not_object = requires(const boost::json::value& v, T& out) {
  { T::parse_if_not_object(v, out) } -> std::same_as<error_code>;
};

template <typename T>
[[nodiscard]] error_code try_from_json(const boost::json::value& j, T& out) {
  return from_json_value<T>{}(j, out);
}

template <typename T>
void from_json(const boost::json::value& j, T& out) {
  error_code ec = try_from_json(j, out);
  if (ec)
    ec.rethrow_if_failed();
}

template <typename T>
T from_json(const boost::json::value& j) {
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
