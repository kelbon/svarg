#pragma once

#include <strswitch/strswitch.hpp>

#include <map>

#include "svarg/utils/number.hpp"
#include "svarg/utils/box.hpp"
#include "svarg/utils/strcmp.hpp"
#include "svarg/utils/json_compare.hpp"

namespace std::filesystem {}

namespace svarg {

namespace fs = std::filesystem;

enum struct parameter_location : uint8_t {
  path,
  query,
  header,
  cookie,
  // TODO querystring (openapi 3.2)?
};

std::string_view e2str(parameter_location l) noexcept;
inline std::string_view e2str(boost::json::kind k) {
  return boost::json::to_string(k);
}

void efromstr(std::string_view s, parameter_location& out);

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
using optional = box<T>;
using std::string;
template <typename T>
using array = std::vector<T>;

template <typename T>
using string_map = std::unordered_map<string, T, str_hash, str_equal_to>;
template <typename T>
using string_map_ci = std::unordered_map<string, T, str_hash_ci, str_equal_to_ci>;

template <typename T>
using ordered_string_map = std::map<string, T, str_less>;
template <typename T>
using ordered_string_map_ci = std::map<string, T, str_less_ci>;

// тег для парсинга
template <typename T>
struct one_or_array {
  array<T> values;

  [[nodiscard]] bool empty() const noexcept {
    return values.empty();
  }
};

}  // namespace svarg

#include "svarg/utils/parse_json.hh"
