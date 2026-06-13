#pragma once

#include <sstream>

#include <boost/pfr.hpp>
#include <boost/json.hpp>

#include "svarg/common_types.hpp"
#include "svarg/utils/unescape_field_name.hpp"
#include "svarg/utils/json_pretty.hpp"

namespace svarg {

template <typename T>
struct serialize {};

template <typename T>
boost::json::value to_json(const T& x) {
  return serialize<T>{}(x);
}

std::string to_json_string(const auto& x, bool pretty = true) {
  if (pretty) {
    std::stringstream ss;
    pretty_print(ss, to_json(x));
    return std::move(ss).str();
  }
  return boost::json::serialize(to_json(x));
}

template <>
struct serialize<bool> {
  boost::json::value operator()(bool v) {
    return v;
  }
};

template <>
struct serialize<uint64_t> {
  boost::json::value operator()(uint64_t v) {
    return v;
  }
};

template <>
struct serialize<int64_t> {
  boost::json::value operator()(int64_t v) {
    return v;
  }
};

template <>
struct serialize<string> {
  boost::json::value operator()(string const& v) {
    return boost::json::string(v);
  }
};

template <>
struct serialize<number> {
  boost::json::value operator()(number const& v) {
    return std::visit([](auto x) -> boost::json::value { return x; }, v.data);
  }
};

template <>
struct serialize<Any> {
  boost::json::value operator()(Any const& v) {
    return v.value;
  }
};

template <>
struct serialize<parameter_location> {
  boost::json::value operator()(parameter_location v) {
    return boost::json::string(e2str(v));
  }
};

template <typename T>
struct serialize<array<T>> {
  boost::json::value operator()(array<T> const& arr) {
    boost::json::array out;
    out.reserve(arr.size());
    for (auto const& elem : arr)
      out.push_back(to_json(elem));
    return out;
  }
};

template <typename T>
struct serialize<string_map<T>> {
  boost::json::value operator()(string_map<T> const& map) {
    boost::json::object out;
    for (auto const& [key, val] : map)
      out.emplace(key, to_json(val));
    return out;
  }
};

template <typename T>
struct serialize<optional<T>> {
  // возвращает null если пусто, иначе сериализует значение
  boost::json::value operator()(optional<T> const& opt) {
    if (!opt)
      return nullptr;
    return to_json(*opt);
  }
};

template <typename T>
struct serialize<one_or_array<T>> {
  boost::json::value operator()(one_or_array<T> const& v) {
    if (v.values.size() == 1)
      return to_json(v.values[0]);
    return to_json(v.values);
  }
};

namespace noexport {

template <aggregate T, size_t I>
void serialize_field(T const& in, boost::json::object& out) {
  constexpr std::string_view raw_name = boost::pfr::get_name<I, T>();
  constexpr std::string_view name = unescape_field_name(raw_name);

  // пропускаем внутренние keyfield поля (они не часть JSON-схемы)
  if constexpr (raw_name.starts_with("_keyfield"))
    return;

  auto const& field = boost::pfr::get<I>(in);

  if constexpr (requires { field.has_value(); }) {
    if (!field.has_value())
      return;
    out.emplace(name, to_json(*field));
  } else {
    out.emplace(name, to_json(field));
  }
}

template <aggregate T, size_t... Is>
void serialize_all_fields(T const& in, boost::json::object& out, std::index_sequence<Is...>) {
  (serialize_field<T, Is>(in, out), ...);
}

}  // namespace noexport

template <aggregate T>
struct serialize<T> {
  static_assert(std::is_same_v<T, std::decay_t<T>>);

  boost::json::value operator()(T const& in) {
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
