#pragma once

#include <set>
#include <unordered_set>

#include <boost/json.hpp>

#include "svarg/common_types.hpp"

namespace svarg {

template <typename T>
struct to_json_value {
  // boost::json::value operator()(const T&) const;
};

template <typename E>
  requires(std::is_enum_v<E>)
struct to_json_value<E> {
  boost::json::value operator()(E v) const {
    return boost::json::string(e2str(v));
  }
};

template <typename T>
[[nodiscard]] boost::json::value to_json(const T& x) {
  return to_json_value<std::remove_cvref_t<T>>{}(x);
}

// специализации

template <>
struct to_json_value<bool> {
  boost::json::value operator()(bool v) const {
    return v;
  }
};

// int32_t и другие целые, float (format схемы)
template <std::integral T>
struct to_json_value<T> {
  boost::json::value operator()(T v) const {
    return v;
  }
};

template <std::floating_point T>
struct to_json_value<T> {
  boost::json::value operator()(T v) const {
    return double(v);
  }
};

template <>
struct to_json_value<uint64_t> {
  boost::json::value operator()(uint64_t v) const {
    return v;
  }
};

template <>
struct to_json_value<int64_t> {
  boost::json::value operator()(int64_t v) const {
    return v;
  }
};

template <>
struct to_json_value<string> {
  boost::json::value operator()(const string& v) const {
    return boost::json::string(v);
  }
};

template <>
struct to_json_value<number> {
  boost::json::value operator()(const number& v) const {
    return std::visit([](auto x) -> boost::json::value { return x; }, v.data);
  }
};

template <>
struct to_json_value<boost::json::value> {
  boost::json::value operator()(const boost::json::value& v) const {
    return v;
  }
};

template <>
struct to_json_value<Any> {
  boost::json::value operator()(const Any& v) const {
    return v.value;
  }
};

template <typename T>
struct to_json_value<arrayof<T>> {
  boost::json::value operator()(const arrayof<T>& arr) const {
    boost::json::array out;
    out.reserve(arr.size());
    for (const auto& elem : arr)
      out.push_back(to_json(elem));
    return out;
  }
};

template <typename... Types>
struct to_json_value<std::unordered_map<Types...>> {
  static boost::json::value operator()(const std::unordered_map<Types...>& map) {
    boost::json::object out;
    for (const auto& [key, val] : map)
      out.emplace(key, to_json(val));
    return out;
  }
};

template <typename... Types>
struct to_json_value<std::map<Types...>> {
  static boost::json::value operator()(const std::map<Types...>& map) {
    boost::json::object out;
    for (const auto& [key, val] : map)
      out.emplace(key, to_json(val));
    return out;
  }
};

template <typename T>
struct to_json_value<ordered_string_map<T>> {
  static boost::json::value operator()(const ordered_string_map<T>& map) {
    // boost json гарантирует что порядок в объекте будет сохранён
    boost::json::object out;
    for (const auto& [key, val] : map)
      out.emplace(key, to_json(val.value));
    return out;
  }
};

template <typename T>
struct to_json_value<optional<T>> {
  // возвращает null если пусто, иначе сериализует значение
  boost::json::value operator()(const optional<T>& opt) const {
    if (!opt)
      return nullptr;
    return to_json(*opt);
  }
};

template <typename T>
struct to_json_value<std::optional<T>> {
  // возвращает null если пусто, иначе сериализует значение
  boost::json::value operator()(const std::optional<T>& opt) const {
    if (!opt)
      return nullptr;
    return to_json(*opt);
  }
};

template <typename T>
struct to_json_value<one_or_array<T>> {
  boost::json::value operator()(const one_or_array<T>& v) const {
    if (v.values.size() == 1)
      return to_json(v.values[0]);
    return to_json(v.values);
  }
};

template <typename... Types>
struct to_json_value<std::unordered_set<Types...>> {
  boost::json::value operator()(const std::unordered_set<Types...>& r) const {
    boost::json::array out;
    out.reserve(r.size());
    for (const auto& elem : r)
      out.push_back(to_json(elem));
    return out;
  }
};

template <typename... Types>
struct to_json_value<std::set<Types...>> {
  boost::json::value operator()(const std::set<Types...>& r) const {
    boost::json::array out;
    out.reserve(r.size());
    for (const auto& elem : r)
      out.push_back(to_json(elem));
    return out;
  }
};

}  // namespace svarg
