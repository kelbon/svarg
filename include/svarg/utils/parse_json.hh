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
struct parse<bool> {
  void operator()(boost::json::value const& v, bool& out) {
    if (auto* b = v.if_bool())
      out = *b;
    else
      throw parse_error(" bool expected, got `{}`", e2str(v.kind()));
  }
};

template <>
struct parse<uint64_t> {
  void operator()(boost::json::value const& v, uint64_t& out) {
    if (auto* x = v.if_uint64())
      out = *x;
    else if (auto* x = v.if_int64()) {
      if (*x < 0)
        throw parse_error(" expected >= 0, got {}", *x);
      out = *x;
    } else
      throw parse_error(" uint64_t|int64_t expected, got `{}`", e2str(v.kind()));
  }
};

template <>
struct parse<int64_t> {
  void operator()(boost::json::value const& v, int64_t& out) {
    if (auto* x = v.if_uint64()) {
      if (!std::in_range<int64_t>(*x))
        throw parse_error(" value `{}` is not in range int64_t", *x);
      out = *x;
    } else if (auto* x = v.if_int64()) {
      out = *x;
    } else
      throw parse_error(" uint64_t|int64_t expected, got `{}`", e2str(v.kind()));
  }
};

template <>
struct parse<string> {
  void operator()(boost::json::value const& v, string& out) {
    if (auto* s = v.if_string())
      out = s->subview();
    else
      throw parse_error(" string expected, got `{}`", e2str(v.kind()));
  }
};

template <>
struct parse<number> {
  void operator()(boost::json::value const& v, number& out) {
    if (v.is_uint64())
      out = v.get_uint64();
    else if (v.is_int64())
      out = v.get_int64();
    else if (v.is_double())
      out = v.get_double();
    else
      throw parse_error(" expected number, got `{}`", e2str(v.kind()));
  }
};

template <typename T>
struct parse<one_or_array<T>> {
  void operator()(boost::json::value const& v, one_or_array<T>& out) {
    if (v.is_array())
      return from_json(v, out.values);
    T x;
    from_json(v, x);
    out.values.push_back(std::move(x));
  }
};

template <typename T>
struct parse<optional<T>> {
  void operator()(boost::json::value const& v, optional<T>& out) {
    if (v.is_null()) {
      out.reset();
      return;
    }
    out = from_json<T>(v);
  }
};

template <typename T>
struct parse<std::optional<T>> {
  void operator()(boost::json::value const& v, std::optional<T>& out) {
    if (v.is_null()) {
      out.reset();
      return;
    }
    out = from_json<T>(v);
  }
};

template <typename T>
struct parse<array<T>> {
  void operator()(boost::json::value const& v, array<T>& out) {
    auto const* arr = v.if_array();
    if (!arr)
      throw parse_error(" array expected, got `{}`", e2str(v.kind()));
    out.clear();
    out.reserve(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
      // не вставляем последний лишний элемент, если его не получится распарсить
      T elem{};
      try {
        from_json((*arr)[i], elem);
      } catch (parse_error const& e) {
        throw parse_error("[{}]{}", i, e.what());
      }
      out.push_back(std::move(elem));
    }
  }
};

template <typename K, typename V, typename... Types>
struct parse<std::unordered_map<K, V, Types...>> {
  void operator()(boost::json::value const& v, std::unordered_map<K, V, Types...>& out) {
    auto const* obj = v.if_object();
    if (!obj)
      throw parse_error(" object expected, got `{}`", e2str(v.kind()));
    out.clear();
    for (auto const& [key, val] : *obj) {
      V elem{};
      try {
        from_json(val, elem);
      } catch (parse_error const& e) {
        throw parse_error("/`{}`{}", std::string_view(key), e.what());
      }
      if (out.contains(key))
        throw parse_error(" {} contains duplicated key `{}`", type_name<V>(), std::string_view(key));
      out.emplace(key, std::move(elem));
    }
  }
};

template <typename K, typename V, typename... Types>
struct parse<std::map<K, V, Types...>> {
  void operator()(boost::json::value const& v, std::map<K, V, Types...>& out) {
    auto const* obj = v.if_object();
    if (!obj)
      throw parse_error(" object expected, got `{}`", e2str(v.kind()));
    out.clear();
    for (auto const& [key, val] : *obj) {
      V elem{};
      try {
        from_json(val, elem);
      } catch (parse_error const& e) {
        throw parse_error("/`{}`{}", std::string_view(key), e.what());
      }
      if (out.contains(key))
        throw parse_error(" {} contains duplicated key `{}`", type_name<V>(), std::string_view(key));
      out.emplace(key, std::move(elem));
    }
  }
};

template <>
struct parse<Any> {
  void operator()(boost::json::value const& v, Any& out) {
    out.value = v;
  }
};

template <>
struct parse<parameter_location> {
  void operator()(boost::json::value const& v, parameter_location& out) {
    if (auto const* s = v.if_string())
      efromstr(s->subview(), out);
    else
      throw parse_error(" string expected for parameter_location, got `{}`", e2str(v.kind()));
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
void parse_field(boost::json::object const& obj, T& out) {
  using Field = std::decay_t<decltype(boost::pfr::get<I>(out))>;
  constexpr std::string_view name = unescape_field_name(boost::pfr::get_name<I, T>());
  auto it = obj.find(name);
  if (it == obj.end()) {
    if (is_optional_field<Field>::value)
      return;
    else
      throw parse_error(" field `{}` in type `{}` is REQUIRED but not present", name, type_name<T>());
  }
  try {
    from_json(it->value(), boost::pfr::get<I>(out));
  } catch (parse_error& e) {
    throw parse_error("/`{}`{}", name, e.what());
  }
}

template <aggregate T, size_t... Is>
void parse_all_fields(boost::json::object const& obj, T& out, std::index_sequence<Is...>) {
  (parse_field<T, Is>(obj, out), ...);
}

}  // namespace noexport

template <aggregate T>
struct parse<T> {
  static_assert(std::is_same_v<T, std::decay_t<T>>);

  void operator()(boost::json::value const& v, T& out) {
    auto const* obj = v.if_object();
    if (!obj) {
      if constexpr (parsable_even_from_not_object<T>) {
        out = T::parse_if_not_object(v);
        return;
      }
      throw parse_error(" object expected, got `{}`", e2str(v.kind()));
    }
    for (auto&& [key, _] : *obj) {
      std::string_view k(key);
      if (!noexport::has_field<T>(k) && !allow_additional_field<T>{}.allow(k))
        throw parse_error(" not allowed field `{}` in type `{}`", k, type_name<T>());
    }
    noexport::parse_all_fields(*obj, out, std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
    postparse<T>{}(out);
    validate<T>{}(std::as_const(out));
  }
};

template <typename E>
  requires(std::is_enum_v<E>)
struct parse<E> {
  void operator()(const boost::json::value& v, E& out) {
    efromstr(std::string_view(v.as_string()), out);
  }
};

template <typename T, typename... Types>
struct parse<std::unordered_set<T, Types...>> {
  void operator()(boost::json::value const& v, std::unordered_set<T, Types...>& out) {
    auto const* arr = v.if_array();
    if (!arr)
      throw parse_error(" array expected, got `{}`", e2str(v.kind()));
    out.clear();
    out.reserve(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
      // не вставляем последний лишний элемент, если его не получится распарсить
      T elem{};
      try {
        from_json((*arr)[i], elem);
      } catch (parse_error const& e) {
        throw parse_error("[{}]{}", i, e.what());
      }
      out.insert(std::move(elem));
    }
  }
};

template <typename T, typename... Types>
struct parse<std::set<T, Types...>> {
  void operator()(boost::json::value const& v, std::set<T, Types...>& out) {
    auto const* arr = v.if_array();
    if (!arr)
      throw parse_error(" array expected, got `{}`", e2str(v.kind()));
    out.clear();
    for (size_t i = 0; i < arr->size(); ++i) {
      // не вставляем последний лишний элемент, если его не получится распарсить
      T elem{};
      try {
        from_json((*arr)[i], elem);
      } catch (parse_error const& e) {
        throw parse_error("[{}]{}", i, e.what());
      }
      out.insert(std::move(elem));
    }
  }
};

}  // namespace svarg
