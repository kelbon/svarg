#pragma once

#include <string>
#include <string_view>

#include <boost/json.hpp>

#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"
#include "svarg/utils/json_pretty.hpp"
#include "svarg/utils/yamljson.hpp"

// текст JSON/YAML для генератора (спецификации, IR). yaml2json/json2yaml - в svarglib, не в рантайме

namespace svarg {

[[nodiscard]] inline std::string to_json_text(const boost::json::value& v, bool pretty = true) {
  if (pretty)
    return pretty_print(v);
  return boost::json::serialize(v);
}

[[nodiscard]] std::string to_json_text(const auto& x, bool pretty = true) {
  return to_json_text(to_json(x), pretty);
}

[[nodiscard]] inline std::string to_yaml_string(const boost::json::value& v) {
  return json2yaml(to_json_text(v, /*pretty=*/false));
}

[[nodiscard]] std::string to_yaml_string(const auto& x) {
  return to_yaml_string(to_json(x));
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
