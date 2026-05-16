#pragma once

#include <string_view>

namespace svarg {

// если внутри типа есть static функция parse_if_not_object(boost::json::value const&)
// то она вызывается, когда пришёл не объект чтобы распарсить агрегат
template <typename T>
struct parse {};

template <typename>
struct allow_additional_field {
  [[nodiscard]] static bool allow(std::string_view name) noexcept {
    return false;
  }
};

}  // namespace svarg
