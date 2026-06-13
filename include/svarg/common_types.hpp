#pragma once

#include <strswitch/strswitch.hpp>

#include <boost/json/value.hpp>

#include "svarg/utils/number.hpp"
#include "svarg/utils/box.hpp"
#include "svarg/utils/strcmp.hpp"

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

  bool operator==(const Any&) const = default;
};

template <typename T>
using optional = box<T>;
using std::string;
template <typename T>
using array = std::vector<T>;
template <typename T>
using string_map = std::unordered_map<string, T, str_hash, str_equal_to>;

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
