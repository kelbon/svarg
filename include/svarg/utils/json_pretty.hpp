#pragma once

#include <format>
#include <string>

#include <boost/json.hpp>

namespace svarg {

namespace noexport {

inline void pretty_print_into(std::string& out, const boost::json::value& jv, size_t indent) {
  namespace json = boost::json;
  auto newline = [&](size_t n) {
    out += '\n';
    out.append(n, ' ');
  };
  switch (jv.kind()) {
    case json::kind::object: {
      const json::object& obj = jv.get_object();
      out += '{';
      for (bool first = true; auto& [key, value] : obj) {
        if (!first)
          out += ',';
        first = false;
        newline(indent + 4);
        out += json::serialize(key);
        out += " : ";
        pretty_print_into(out, value, indent + 4);
      }
      newline(indent);
      out += '}';
      break;
    }
    case json::kind::array: {
      const json::array& arr = jv.get_array();
      out += '[';
      for (bool first = true; const json::value& v : arr) {
        if (!first)
          out += ',';
        first = false;
        newline(indent + 4);
        pretty_print_into(out, v, indent + 4);
      }
      newline(indent);
      out += ']';
      break;
    }
    default:
      // строки, числа, bool и null - как в компактном JSON
      out += json::serialize(jv);
      break;
  }
}

}  // namespace noexport

// JSON с отступами в 4 пробела, с переводом строки в конце
[[nodiscard]] inline std::string pretty_print(const boost::json::value& j) {
  std::string out;
  noexport::pretty_print_into(out, j, 0);
  out += '\n';
  return out;
}

}  // namespace svarg
