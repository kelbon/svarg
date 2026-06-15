#pragma once

#include <iostream>
#include <sstream>

#include <map>

#include <boost/json.hpp>

namespace svarg {

void append_indent(std::string& result, std::string_view indent, int quantity) {
  result.reserve(indent.size() + quantity);
  for (int i = 0; i < quantity; i++) {
    result.append(indent);
  }
}

inline std::string serialize_value_debug(const boost::json::value& jv, std::string_view indent, int quantity);

inline std::string serialize_object_debug(const boost::json::object& obj, std::string_view indent,
                                          int quantity) {
  if (obj.size() == 0)
    return "{}";

  std::map<std::string_view, boost::json::value> sorted_keys;
  for (const auto& [key, v] : obj) {
    sorted_keys.emplace(key, v);
  }

  std::string result = "{\n";
  for (auto it = sorted_keys.begin(); it != sorted_keys.end(); it++) {
    append_indent(result, indent, quantity + 1);
    result += boost::json::serialize(boost::json::value(it->first)) + " : " +
              serialize_value_debug(it->second, indent, quantity + 1);
    if (std::next(it) != sorted_keys.end())
      result += ",\n";
  }
  result += "\n";
  append_indent(result, indent, quantity);
  result += "}";
  return result;
}

inline std::string serialize_array_debug(const boost::json::array& arr, std::string_view indent,
                                         int quantity) {
  if (arr.size() == 0)
    return "[]";

  std::string result = "[\n";
  for (auto it = arr.begin(); it != arr.end(); ++it) {
    append_indent(result, indent, quantity + 1);
    result += serialize_value_debug(*it, indent, quantity + 1);
    if (std::next(it) != arr.end())
      result += ",\n";
  }
  result += "\n";
  append_indent(result, indent, quantity);
  result += "]";
  return result;
}

inline std::string serialize_value_debug(const boost::json::value& jv, std::string_view indent,
                                         int quantity) {
  switch (jv.kind()) {
    case boost::json::kind::object:
      return serialize_object_debug(jv.as_object(), indent, quantity);
    case boost::json::kind::array:
      return serialize_array_debug(jv.as_array(), indent, quantity);
    case boost::json::kind::string:
    case boost::json::kind::uint64:
    case boost::json::kind::int64:
    case boost::json::kind::double_:
      return boost::json::serialize(jv);
    case boost::json::kind::bool_:
      return jv.as_bool() ? "true" : "false";
    case boost::json::kind::null:
      return "null";
  }

  return "";
}

inline void pretty_print(std::ostream& os, const boost::json::value& jv, std::string* indent = nullptr) {
  namespace json = boost::json;
  std::string indent_;
  if (!indent)
    indent = &indent_;
  switch (jv.kind()) {
    case json::kind::object: {
      os << "{\n";
      indent->append(4, ' ');
      auto const& obj = jv.get_object();
      if (!obj.empty()) {
        auto it = obj.begin();
        for (;;) {
          os << *indent << json::serialize(it->key()) << " : ";
          pretty_print(os, it->value(), indent);
          if (++it == obj.end())
            break;
          os << ",\n";
        }
      }
      os << "\n";
      indent->resize(indent->size() - 4);
      os << *indent << "}";
      break;
    }

    case json::kind::array: {
      os << "[\n";
      indent->append(4, ' ');
      auto const& arr = jv.get_array();
      if (!arr.empty()) {
        auto it = arr.begin();
        for (;;) {
          os << *indent;
          pretty_print(os, *it, indent);
          if (++it == arr.end())
            break;
          os << ",\n";
        }
      }
      os << "\n";
      indent->resize(indent->size() - 4);
      os << *indent << "]";
      break;
    }

    case json::kind::string: {
      os << json::serialize(jv.get_string());
      break;
    }

    case json::kind::uint64:
    case json::kind::int64:
    case json::kind::double_:
      os << jv;
      break;

    case json::kind::bool_:
      if (jv.get_bool())
        os << "true";
      else
        os << "false";
      break;

    case json::kind::null:
      os << "null";
      break;
  }

  if (indent->empty())
    os << "\n";
}

inline std::string debug_pretty_serialize(const boost::json::value& value, std::string_view indent = "  ") {
  return serialize_value_debug(value, indent, 0);
}

template <typename T>
std::string to_debug_pretty_json_str(const T& t) {
  return debug_pretty_serialize(serialize_as_json(t));
}

}  // namespace svarg
