#pragma once

#include <set>
#include <unordered_set>
#include <string_view>

#include <boost/json/value.hpp>
#include <boost/json/parse.hpp>

#include "svarg/errors.hpp"
#include "svarg/utils/yamljson.hpp"
#include "svarg/common_types.hpp"
#include "svarg/utils/safe_cast.hpp"
#include "svarg/concepts.hpp"

namespace svarg {

// если внутри типа есть static функция error_code parse_if_not_object(boost::json::value const&, T& out)
// то она вызывается, когда пришёл не объект чтобы распарсить агрегат (только для фронтенд типов)
template <typename T>
struct from_json_value {
  // error_code operator()(const boost::json::value&, T& out)
};

template <typename T>
[[nodiscard]] error_code try_from_json(const boost::json::value& j, T& out) {
  return from_json_value<std::remove_volatile_t<T>>{}(j, out);
}

template <typename T>
T from_json(const boost::json::value& j) {
  T value;
  try_from_json(j, value).rethrow_if_failed();
  return value;
}

template <typename T>
T from_json(std::string_view str) {
  return from_json<T>(boost::json::parse(str, {}, {.allow_comments = true}));
}

template <typename T>
T from_yaml(std::string_view str) {
  // yaml может быть пуст, json - нет. Так-то может быть null, "", {}, [], но выбран объект
  if (str.empty()) [[unlikely]]
    str = "{}";
  return from_json<T>(
      boost::json::parse(yaml2json(str), {}, boost::json::parse_options{.allow_comments = true}));
}

// специализации

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

template <std::integral T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    if (j.is_int64()) {
      safe_write(j.get_int64(), v);
      return {};
    }
    if (j.is_uint64()) {
      safe_write(j.get_uint64(), v);
      return {};
    }
    return svarg::parse_error(" expected int64 | uint64, got {}", e2str(j.kind()));
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

template <std::floating_point T>
struct from_json_value<T> {
  error_code operator()(const boost::json::value& v, T& out) {
    number nm;
    error_code ec = try_from_json(v, nm);
    // игнорируем возможные оверфлоу float -> double и тд
    if (!ec) [[likely]]
      out = static_cast<T>(nm.get_as_double());
    return ec;
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
struct from_json_value<arrayof<T>> {
  error_code operator()(const boost::json::value& v, arrayof<T>& out) {
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

template <typename T>
struct from_json_value<ordered_string_map<T>> {
  error_code operator()(const boost::json::value& v, ordered_string_map<T>& out) {
    // boost json гарантирует что порядок в объекте будет сохранён
    // https://www.boost.org/doc/libs/latest/libs/json/doc/html/dom/object.html
    // "The order of insertions is preserved, as long as there are no removals"
    const auto* obj = v.if_object();
    if (!obj)
      return parse_error(" object expected, got `{}`", e2str(v.kind()));
    out.clear();
    for (const auto& [_key, val] : *obj) {
      std::string_view key = _key;
      T elem{};
      error_code ec = try_from_json(val, elem);
      if (ec) {
        ec.reformat("/`{}`{}", key, old_message);
        return ec;
      }
      if (out.contains(key))
        return parse_error(" {} contains duplicated key `{}`", type_name<T>(), key);
      out.try_emplace(key, std::move(elem));
    }
    return {};
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

// любое JSON значение как есть
template <>
struct from_json_value<boost::json::value> {
  error_code operator()(const boost::json::value& v, boost::json::value& out) {
    out = v;
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

}  // namespace svarg
