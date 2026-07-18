#pragma once

#include <strswitch/strswitch.hpp>

#include <map>

#include "svarg/utils/number.hpp"
#include <metel/box.hpp>

#include "svarg/utils/strcmp.hpp"
#include "svarg/utils/json_compare.hpp"

namespace std::filesystem {}

namespace svarg {

namespace fs = std::filesystem;

inline std::string_view e2str(boost::json::kind k) {
  return boost::json::to_string(k);
}

struct Any {
  boost::json::value value;

  bool operator==(const Any& r) const {
    return value == r.value;
  }
  std::strong_ordering operator<=>(const Any& r) const {
    return json_compare(value, r.value);
  }
};

template <typename T>
using optional = metel::box<T>;
using std::string;
template <typename T>
using arrayof = std::vector<T>;

template <typename T>
using string_map = std::map<string, T, str_less>;
template <typename T>
using string_map_ci = std::map<string, T, str_less_ci>;

template <typename T>
using unordered_string_map = std::map<string, T, str_hash, str_equal_to>;
template <typename T>
using unordered_string_map_ci = std::unordered_map<string, T, str_hash_ci, str_equal_to_ci>;

// тег для парсинга
template <typename T>
struct one_or_array {
  arrayof<T> values;

  [[nodiscard]] bool empty() const noexcept {
    return values.empty();
  }
};

enum struct status_range_e {
  _1XX = 1,
  _2XX,
  _3XX,
  _4XX,
  _5XX,
};

constexpr std::string_view e2str(status_range_e e) {
  switch (e) {
    case status_range_e::_1XX:
      return "1XX";
    case status_range_e::_2XX:
      return "2XX";
    case status_range_e::_3XX:
      return "3XX";
    case status_range_e::_4XX:
      return "4XX";
    case status_range_e::_5XX:
      return "5XX";
  }
  metel::unreachable();
}

[[nodiscard]] constexpr bool efromstr(std::string_view str, status_range_e& out) {
  using enum status_range_e;
  std::optional e = ss::string_switch<status_range_e>(str)
                        .case_("1XX", _1XX)
                        .case_("2XX", _2XX)
                        .case_("3XX", _3XX)
                        .case_("4XX", _4XX)
                        .case_("5XX", _5XX)
                        .or_nullopt();
  if (!e)
    return false;
  out = *e;
  return true;
}

template <status_range_e R>
[[nodiscard]] constexpr bool match_status_range(int status) noexcept {
  using enum status_range_e;
  if constexpr (R == _1XX)
    return status >= 100 && status < 200;
  else if constexpr (R == _2XX)
    return status >= 200 && status < 300;
  else if constexpr (R == _3XX)
    return status >= 300 && status < 400;
  else if constexpr (R == _4XX)
    return status >= 400 && status < 500;
  else if constexpr (R == _5XX)
    return status >= 500 && status < 600;
  else
    static_assert(false, "invalid status range value");
}

}  // namespace svarg
