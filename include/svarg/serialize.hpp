#pragma once

#include <boost/pfr.hpp>
#include <boost/json.hpp>

#include "svarg/fundamental_types.hpp"
#include "svarg/openapi_object.hpp"
#include "svarg/utils/parse_json.hpp"
#include "svarg/utils/unescape_field_name.hpp"

namespace svarg {

template <typename T>
struct serialize {};

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
      out.push_back(serialize<T>{}(elem));
    return out;
  }
};

template <typename T>
struct serialize<string_map<T>> {
  boost::json::value operator()(string_map<T> const& map) {
    boost::json::object out;
    for (auto const& [key, val] : map)
      out.emplace(key, serialize<T>{}(val));
    return out;
  }
};

template <typename T>
struct serialize<optional<T>> {
  // возвращает null если пусто, иначе сериализует значение
  boost::json::value operator()(optional<T> const& opt) {
    if (!opt)
      return nullptr;
    return serialize<T>{}(*opt);
  }
};

template <typename T>
struct serialize<one_or_array<T>> {
  boost::json::value operator()(one_or_array<T> const& v) {
    if (v.values.size() == 1)
      return serialize<T>{}(v.values[0]);
    return serialize<array<T>>{}(v.values);
  }
};

template <typename T>
struct serialize<reference_or<T>> {
  boost::json::value operator()(reference_or<T> const& v) {
    return std::visit(
        [](auto const& x) -> boost::json::value {
          using U = std::decay_t<decltype(x)>;
          return serialize<U>{}(x);
        },
        v);
  }
};

namespace noexport {

template <aggregate T, size_t I>
void serialize_field(T const& in, boost::json::object& out) {
  using Field = std::decay_t<decltype(boost::pfr::get<I>(in))>;
  constexpr std::string_view raw_name = boost::pfr::get_name<I, T>();
  constexpr std::string_view name = unescape_field_name(raw_name);

  // пропускаем внутренние keyfield поля (они не часть JSON-схемы)
  if constexpr (raw_name.starts_with("_keyfield"))
    return;

  auto const& field = boost::pfr::get<I>(in);

  if constexpr (requires { field.has_value(); }) {
    if (!field.has_value())
      return;
    out.emplace(name, serialize<typename Field::value_type>{}(*field));
  } else {
    out.emplace(name, serialize<Field>{}(field));
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

template <>
struct serialize<responses_object> {
  boost::json::value operator()(responses_object const& in) {
    // собираем всё обратно в плоскую string_map
    boost::json::object out;
    if (in.default_.has_value())
      out.emplace("default", serialize<reference_or<response_object>>{}(*in.default_));
    for (auto const& [code, resp] : in._responses)
      out.emplace(code, serialize<reference_or<response_object>>{}(resp));
    return out;
  }
};

template <typename T>
boost::json::value serialize_as_json(const T& x) {
  return serialize<T>{}(x);
}

}  // namespace svarg
