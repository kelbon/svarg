#pragma once

#include <set>

#include <boost/pfr.hpp>
#include <boost/json.hpp>
#include <unordered_set>

#include "svarg/common_types.hpp"
#include "svarg/concepts.hpp"
#include "svarg/utils/unescape_field_name.hpp"
#include "svarg/utils/json_pretty.hpp"
#include "svarg/utils/yamljson.hpp"

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

[[nodiscard]] inline std::string to_json_string(const boost::json::value& v, bool pretty = true) {
  if (pretty)
    return pretty_print(v);
  return boost::json::serialize(v);
}
[[nodiscard]] inline std::string to_yaml_string(const boost::json::value& v) {
  return json2yaml(to_json_string(v, /*pretty=*/false));
}

[[nodiscard]] std::string to_json_string(const auto& x, bool pretty = true) {
  if (pretty)
    return pretty_print(to_json(x));
  return boost::json::serialize(to_json(x));
}

[[nodiscard]] std::string to_yaml_string(const auto& x) {
  return json2yaml(to_json_string(x, /*pretty=*/false));
}

// специализации

template <>
struct to_json_value<bool> {
  boost::json::value operator()(bool v) const {
    return v;
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
  boost::json::value operator()(const std::unordered_map<Types...>& map) const {
    boost::json::object out;
    for (const auto& [key, val] : map)
      out.emplace(key, to_json(val));
    return out;
  }
};

template <typename... Types>
struct to_json_value<std::map<Types...>> {
  boost::json::value operator()(const std::map<Types...>& map) const {
    boost::json::object out;
    for (const auto& [key, val] : map)
      out.emplace(key, to_json(val));
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

namespace noexport {

template <typename T, size_t I>
void serialize_field(const T& in, boost::json::object& out) {
  constexpr std::string_view raw_name = boost::pfr::get_name<I, T>();
  constexpr std::string_view name = unescape_field_name(raw_name);

  // пропускаем внутренние keyfield поля (они не часть JSON-схемы)
  if constexpr (is_keyfield(raw_name))
    return;

  const auto& field = boost::pfr::get<I>(in);

  if constexpr (requires { field.has_value(); }) {
    if (!field.has_value())
      return;
    out.emplace(name, to_json(*field));
  } else {
    out.emplace(name, to_json(field));
  }
}

template <front_or_ir_type T, size_t... Is>
void serialize_all_fields(const T& in, boost::json::object& out, std::index_sequence<Is...>) {
  (serialize_field<T, Is>(in, out), ...);
}

}  // namespace noexport

template <front_or_ir_type T>
struct to_json_value<T> {
  static_assert(std::is_same_v<T, std::decay_t<T>>);

  boost::json::value operator()(const T& in) const {
    // костыль для schema_object
    if constexpr (requires { in._keyfield_boolean_schema; }) {
      if (in._keyfield_boolean_schema.has_value())
        return *in._keyfield_boolean_schema;
    }

    boost::json::object out;
    noexport::serialize_all_fields(in, out, std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
    return out;
  }
};

}  // namespace svarg
