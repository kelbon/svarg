#pragma once

#include <boost/pfr.hpp>
#include <boost/json.hpp>
#include <unordered_set>

#include "svarg/fundamental_types.hpp"
#include "svarg/openapi_object.hpp"
#include "svarg/utils/parse_json.hpp"
#include "svarg/utils/unescape_field_name.hpp"

namespace svarg {

template <typename T>
struct serialize {};

template <typename T>
boost::json::value serialize_as_json(const T& x) {
  return serialize<T>{}(x);
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
      out.push_back(serialize_as_json(elem));
    return out;
  }
};

template <typename T>
struct serialize<string_map<T>> {
  boost::json::value operator()(string_map<T> const& map) {
    boost::json::object out;
    for (auto const& [key, val] : map)
      out.emplace(key, serialize_as_json(val));
    return out;
  }
};

template <typename T>
struct serialize<optional<T>> {
  // возвращает null если пусто, иначе сериализует значение
  boost::json::value operator()(optional<T> const& opt) {
    if (!opt)
      return nullptr;
    return serialize_as_json(*opt);
  }
};

template <typename T>
struct serialize<one_or_array<T>> {
  boost::json::value operator()(one_or_array<T> const& v) {
    if (v.values.size() == 1)
      return serialize_as_json(v.values[0]);
    return serialize_as_json(v.values);
  }
};

template <typename T>
struct serialize<reference_or<T>> {
  boost::json::value operator()(reference_or<T> const& v) {
    return std::visit(
        [](auto const& x) -> boost::json::value {
          return serialize_as_json(x);  //
        },
        v);
  }
};

template <typename T>
struct discriminator_t {
  static constexpr auto value = []() {
    if constexpr (requires { T::discriminator_value; }) {
      return T::discriminator_value;
    } else {
      return type_name<T>();
    }
  }();
};

template <typename T>
inline constexpr auto discriminator = discriminator_t<T>::value;

template <typename... U>
struct serialize<std::variant<U...>> {
  boost::json::value operator()(std::variant<U...> const& v) {
    return std::visit(
        []<typename V>(V&& v) {
          boost::json::object r;
          r[discriminator<std::remove_cvref_t<V>>] = serialize_as_json(v);
          return r;
        },
        v);
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
    out.emplace(name, serialize_as_json(*field));
  } else {
    out.emplace(name, serialize_as_json(field));
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

template <typename T>
struct serialize<std::unordered_set<T>> {
  boost::json::value operator()(const std::unordered_set<T>& r) {
    boost::json::array a;

    a.reserve(r.size());

    for (const auto& t : r) {
      a.emplace_back(serialize_as_json(t));
    }

    return boost::json::value{a};
  }
};

template <>
struct serialize<responses_object> {
  boost::json::value operator()(responses_object const& in) {
    // собираем всё обратно в плоскую string_map
    boost::json::object out;
    if (in.default_.has_value())
      out.emplace("default", serialize_as_json(*in.default_));
    for (auto const& [code, resp] : in._responses)
      out.emplace(code, serialize_as_json(resp));
    return out;
  }
};

}  // namespace svarg
