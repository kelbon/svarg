#pragma once

#include "svarg/utils/box.hpp"
#include "svarg/utils/number.hpp"
#include "svarg/errors.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

#include <boost/json/value.hpp>

#include <strswitch/strswitch.hpp>

namespace svarg {

// data types

// integer, string, number

// formats
// int32, int64, float, double, password, ... и тд может быть определено пользователем (data ещё, но видимо
// убрали в новой версии)

struct Any {
  boost::json::value value;
};

template <typename T>
using optional = box<T>;
using std::string;
template <typename T>
using array = std::vector<T>;
template <typename T>
using string_map = std::unordered_map<string, T>;

struct reference_object {
  string ref_;  // MUST be URI, в json как $ref
  optional<string> summary;
  optional<string> description;
};

// тег для парсинга
template <typename T>
struct one_or_array {
  array<T> values;

  [[nodiscard]] bool empty() const noexcept {
    return values.empty();
  }
};

struct path_item_object;
// Note: T НЕ ДОЛЖНО БЫТЬ path_item_object. Это только на уровне спеки написано ref | path item
// на деле же у них одинаковые поля и это формальность
template <typename T>
  requires(!std::is_same_v<path_item_object, T>)
using reference_or = std::variant<reference_object, T>;

enum struct parameter_location : uint8_t {
  path,
  query,
  header,
  cookie,
};

inline std::string_view e2str(parameter_location l) noexcept {
  switch (l) {
    case parameter_location::path:
      return "path";
    case parameter_location::query:
      return "query";
    case parameter_location::header:
      return "header";
    case parameter_location::cookie:
      return "cookie";
  }
  std::unreachable();
}

inline void efromstr(std::string_view s, parameter_location& out) {
  using enum parameter_location;
  auto loc = ss::string_switch<parameter_location>(s)
                 .case_("query", query)
                 .case_("header", header)
                 .case_("path", path)
                 .case_("cookie", cookie)
                 .or_default(parameter_location(-1));
  if (loc == parameter_location(-1))
    throw parse_error("unknown parameter location `{}`", s);
  out = loc;
}

}  // namespace svarg
