#pragma once

// этот файл - часть common_types.hpp (здесь #include common_types.hpp для подсветки)

#include <boost/pfr.hpp>
#include <unordered_set>
#include <set>

#include "svarg/common_types.hpp"
#include "svarg/errors.hpp"
#include "svarg/utils/type_name.hpp"
#include "svarg/utils/unescape_field_name.hpp"
#include "svarg/validate.hpp"
#include "svarg/parse.hpp"

namespace svarg {

template <>
struct from_json_value<bool> {
  error_code operator()(const boost::json::value& v, bool& out) {
    if (auto* b = v.if_bool())
      out = *b;
    else
      return parse_error(" bool expected, got `{}`", e2str(v.kind()));
    return {};
  }
};

template <>
struct from_json_value<uint64_t> {
  error_code operator()(const boost::json::value& v, uint64_t& out) {
    if (auto* x = v.if_uint64())
      out = *x;
    else if (auto* x = v.if_int64()) {
      if (*x < 0)
        return parse_error(" expected >= 0, got {}", *x);
      out = *x;
    } else
      return parse_error(" uint64_t|int64_t expected, got `{}`", e2str(v.kind()));
    return {};
  }
};

template <>
struct from_json_value<int64_t> {
  error_code operator()(const boost::json::value& v, int64_t& out) {
    if (auto* x = v.if_uint64()) {
      if (!std::in_range<int64_t>(*x))
        return parse_error(" value `{}` is not in range int64_t", *x);
      out = *x;
    } else if (auto* x = v.if_int64()) {
      out = *x;
    } else
      return parse_error(" uint64_t|int64_t expected, got `{}`", e2str(v.kind()));
    return {};
  }
};

template <>
struct from_json_value<string> {
  error_code operator()(const boost::json::value& v, string& out) {
    if (auto* s = v.if_string())
      out = s->subview();
    else
      return parse_error(" string expected, got `{}`", e2str(v.kind()));
    return {};
  }
};

template <>
struct from_json_value<number> {
  error_code operator()(const boost::json::value& v, number& out) {
    if (v.is_uint64())
      out = v.get_uint64();
    else if (v.is_int64())
      out = v.get_int64();
    else if (v.is_double())
      out = v.get_double();
    else
      return parse_error(" expected number, got `{}`", e2str(v.kind()));
    return {};
  }
};

template <>
struct from_json_value<double> {
  error_code operator()(const boost::json::value& v, double& out) {
    number nm;
    error_code ec = try_from_json(v, nm);
    if (!ec) [[likely]]
      out = nm.get_as_double();
    return ec;
  }
};

template <typename T>
struct from_json_value<one_or_array<T>> {
  error_code operator()(const boost::json::value& v, one_or_array<T>& out) {
    error_code ec;
    if (v.is_array())
      ec = try_from_json(v, out.values);
    else {
      T x;
      ec = try_from_json(v, x);
      if (!ec) [[likely]]
        out.values.push_back(std::move(x));
    }
    return ec;
  }
};

template <typename T>
struct from_json_value<metel::box<T>> {
  error_code operator()(const boost::json::value& v, metel::box<T>& out) {
    if (v.is_null()) {
      out.reset();
      return {};
    }
    return try_from_json(v, out.emplace());
  }
};

template <typename T>
struct from_json_value<std::optional<T>> {
  error_code operator()(const boost::json::value& v, std::optional<T>& out) {
    if (v.is_null()) {
      out.reset();
      return {};
    }
    return try_from_json(v, out.emplace());
  }
};

template <typename T>
struct from_json_value<array<T>> {
  error_code operator()(const boost::json::value& v, array<T>& out) {
    const auto* arr = v.if_array();
    if (!arr)
      return parse_error(" array expected, got `{}`", e2str(v.kind()));
    out.clear();
    out.reserve(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
      // не вставляем последний лишний элемент, если его не получится распарсить
      T elem{};
      error_code ec = try_from_json((*arr)[i], elem);
      if (ec) {
        ec.reformat("[{}]{}", i, old_message);
        return ec;
      }
      out.push_back(std::move(elem));
    }
    return {};
  }
};

template <typename K, typename V, typename... Types>
struct from_json_value<std::unordered_map<K, V, Types...>> {
  error_code operator()(const boost::json::value& v, std::unordered_map<K, V, Types...>& out) {
    const auto* obj = v.if_object();
    if (!obj)
      return parse_error(" object expected, got `{}`", e2str(v.kind()));
    out.clear();
    for (const auto& [key, val] : *obj) {
      V elem{};
      error_code ec = try_from_json(val, elem);
      if (ec) {
        ec.reformat("/`{}`{}", std::string_view(key), old_message);
        return ec;
      }
      if (out.contains(key))
        return parse_error(" {} contains duplicated key `{}`", type_name<V>(), std::string_view(key));
      out.emplace(key, std::move(elem));
    }
    return {};
  }
};

template <typename K, typename V, typename... Types>
struct from_json_value<std::map<K, V, Types...>> {
  error_code operator()(const boost::json::value& v, std::map<K, V, Types...>& out) {
    const auto* obj = v.if_object();
    if (!obj)
      return parse_error(" object expected, got `{}`", e2str(v.kind()));
    out.clear();
    for (const auto& [key, val] : *obj) {
      V elem{};
      error_code ec = try_from_json(val, elem);
      if (ec) {
        ec.reformat("/`{}`{}", std::string_view(key), old_message);
        return ec;
      }
      if (out.contains(key))
        return parse_error(" {} contains duplicated key `{}`", type_name<V>(), std::string_view(key));
      out.emplace(key, std::move(elem));
    }
    return {};
  }
};

template <>
struct from_json_value<Any> {
  error_code operator()(const boost::json::value& v, Any& out) {
    out.value = v;
    return {};
  }
};

template <>
struct from_json_value<parameter_location> {
  error_code operator()(const boost::json::value& v, parameter_location& out) {
    if (const auto* s = v.if_string()) {
      if (efromstr(std::string_view(*s), out))
        return {};
      return bad_enum_value<parameter_location>(std::string_view(*s));
    }
    return parse_error(" string expected for parameter_location, got `{}`", e2str(v.kind()));
  }
};

template <typename T>
concept aggregate = std::is_aggregate_v<std::decay_t<T>>;

namespace noexport {

template <typename T>
struct is_optional_field : std::false_type {};
template <typename T>
struct is_optional_field<optional<T>> : std::true_type {};
template <typename T>
struct is_optional_field<std::optional<T>> : std::true_type {};

template <typename T>
constexpr bool has_field(std::string_view name) noexcept {
  return [&]<size_t... Is>(std::index_sequence<Is...>) {
    return ((unescape_field_name(boost::pfr::get_name<Is, T>()) == name) || ...);
  }(std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
}

template <aggregate T, size_t I>
bool parse_field(const boost::json::object& obj, T& out, error_code& ec) {
  using Field = std::decay_t<decltype(boost::pfr::get<I>(out))>;
  constexpr std::string_view name = unescape_field_name(boost::pfr::get_name<I, T>());
  auto it = obj.find(name);
  if (it == obj.end()) {
    if (is_optional_field<Field>::value)
      return true;
    else {
      ec = parse_error(" field `{}` in type `{}` is REQUIRED but not present", name, type_name<T>());
      return false;
    }
  }
  ec = try_from_json(it->value(), boost::pfr::get<I>(out));
  if (ec) {
    ec.reformat("/`{}`{}", name, old_message);
    return false;
  }
  return true;
}

template <aggregate T, size_t... Is>
error_code parse_all_fields(const boost::json::object& obj, T& out, std::index_sequence<Is...>) {
  error_code ec;
  (parse_field<T, Is>(obj, out, ec) && ...);
  return ec;
}

}  // namespace noexport

template <aggregate T>
struct from_json_value<T> {
  static_assert(std::is_same_v<T, std::decay_t<T>>);

  error_code operator()(const boost::json::value& v, T& out) {
    const auto* obj = v.if_object();
    if (!obj) {
      if constexpr (parsable_even_from_not_object<T>)
        return T::parse_if_not_object(v, out);
      else
        return parse_error(" object expected, got `{}`", e2str(v.kind()));
    }
    for (auto&& [key, _] : *obj) {
      std::string_view k(key);
      if (!noexport::has_field<T>(k) && !allow_additional_field<T>{}.allow(k))
        return parse_error(" not allowed field `{}` in type `{}`", k, type_name<T>());
    }
    error_code ec =
        noexport::parse_all_fields(*obj, out, std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
    if (ec)
      return ec;
    ec = postparse<T>{}(out);
    if (ec)
      return ec;
    return validate<T>{}(std::as_const(out));
  }
};

template <typename E>
  requires(std::is_enum_v<E>)
struct from_json_value<E> {
  error_code operator()(const boost::json::value& v, E& out) {
    if (efromstr(std::string_view(v.as_string()), out))
      return {};
    return bad_enum_value<E>(std::string_view(v.as_string()));
  }
};

template <typename T, typename... Types>
struct from_json_value<std::unordered_set<T, Types...>> {
  error_code operator()(const boost::json::value& v, std::unordered_set<T, Types...>& out) {
    const auto* arr = v.if_array();
    if (!arr)
      return parse_error(" array expected, got `{}`", e2str(v.kind()));
    out.clear();
    out.reserve(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
      // не вставляем последний лишний элемент, если его не получится распарсить
      T elem{};
      error_code ec = try_from_json((*arr)[i], elem);
      if (ec) {
        ec.reformat("[{}]{}", i, old_message);
        return ec;
      }
      out.insert(std::move(elem));
    }
    return {};
  }
};

template <typename T, typename... Types>
struct from_json_value<std::set<T, Types...>> {
  error_code operator()(const boost::json::value& v, std::set<T, Types...>& out) {
    const auto* arr = v.if_array();
    if (!arr)
      return parse_error(" array expected, got `{}`", e2str(v.kind()));
    out.clear();
    for (size_t i = 0; i < arr->size(); ++i) {
      // не вставляем последний лишний элемент, если его не получится распарсить
      T elem{};
      error_code ec = try_from_json((*arr)[i], elem);
      if (ec) {
        ec.reformat("[{}]{}", i, old_message);
        return ec;
      }
      out.insert(std::move(elem));
    }
    return {};
  }
};

}  // namespace svarg
