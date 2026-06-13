#pragma once

#include <string_view>
#include <boost/json/value.hpp>

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
T parse_from_json(boost::json::value const& j) {
  T value;
  parse<T>{}(j, value);
  return value;
}

template <typename T>
void parse_from_json(boost::json::value const& j, T& out) {
  parse<T>{}(j, out);
}

}  // namespace svarg
