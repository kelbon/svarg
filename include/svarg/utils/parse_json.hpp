#pragma once

#include <format>

#include <boost/pfr.hpp>

#include "svarg/fundamental_types.hpp"
#include "svarg/utils/type_name.hpp"
#include "svarg/utils/unescape_field_name.hpp"
#include "svarg/validate.hpp"
#include "svarg/parse.hpp"

namespace svarg {

inline std::string_view e2str(boost::json::kind k) {
  return boost::json::to_string(k);
}

template <>
struct parse<bool> {
  void operator()(boost::json::value const& v, bool& out) {
    if (auto* b = v.if_bool())
      out = *b;
    else
      throw parse_error("bool expected, got `{}`", e2str(v.kind()));
  }
};

template <>
struct parse<uint64_t> {
  void operator()(boost::json::value const& v, uint64_t& out) {
    if (auto* x = v.if_uint64())
      out = *x;
    else if (auto* x = v.if_int64()) {
      if (*x < 0)
        throw parse_error("expected >= 0, got {}", *x);
      out = *x;
    } else
      throw parse_error("uint64_t|int64_t expected, got `{}`", e2str(v.kind()));
  }
};

template <>
struct parse<int64_t> {
  void operator()(boost::json::value const& v, int64_t& out) {
    if (auto* x = v.if_uint64()) {
      if (!std::in_range<int64_t>(*x))
        throw parse_error("value `{}` is not in range int64_t", *x);
      out = *x;
    } else if (auto* x = v.if_int64()) {
      out = *x;
    } else
      throw parse_error("uint64_t|int64_t expected, got `{}`", e2str(v.kind()));
  }
};

template <>
struct parse<string> {
  void operator()(boost::json::value const& v, string& out) {
    if (auto* s = v.if_string())
      out = s->subview();
    else
      throw parse_error("string expected, got `{}`", e2str(v.kind()));
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
      throw parse_error("expected number, got `{}`", e2str(v.kind()));
  }
};

template <typename T>
struct parse<one_or_array<T>> {
  void operator()(boost::json::value const& v, one_or_array<T>& out) {
    if (v.is_array())
      return parse<decltype(out.values)>{}(v, out.values);
    T x;
    parse<T>{}(v, x);
    out.values.push_back(std::move(x));
  }
};

template <typename T>
struct parse<box<T>> {
  void operator()(boost::json::value const& v, box<T>& out) {
    if (v.is_null()) {
      out.reset();
      return;
    }
    // если не получилось распарсить не трогаем out
    T tmp{};
    parse<T>{}(v, tmp);
    out = std::move(tmp);
  }
};

template <typename T>
struct parse<array<T>> {
  void operator()(boost::json::value const& v, array<T>& out) {
    auto const* arr = v.if_array();
    if (!arr)
      throw parse_error("array expected, got `{}`", e2str(v.kind()));
    out.clear();
    out.reserve(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
      // не вставляем последний лишний элемент, если его не получится распарсить
      T elem{};
      try {
        parse<T>{}((*arr)[i], elem);
      } catch (parse_error const& e) {
        throw parse_error("while parsing array[{}]: {}", i, e.what());
      }
      out.push_back(std::move(elem));
    }
  }
};

// TODO ещё нужно провалидировать там какие-то паттерны, вроде в servers и в path
// пути должны не быть типа x/{name} x/{id}, т.е. уникальны без учета этих имён
template <typename T>
struct parse<string_map<T>> {
  void operator()(boost::json::value const& v, string_map<T>& out) {
    // TODO? проверять что ключ один раз встречается каждый?
    auto const* obj = v.if_object();
    if (!obj)
      throw parse_error("object expected, got `{}`", e2str(v.kind()));
    out.clear();
    for (auto const& [key, val] : *obj) {
      // не вставляем полураспаршенный элемент
      T elem{};
      try {
        parse<T>{}(val, elem);
      } catch (parse_error const& e) {
        throw parse_error("while parsing string map key `{}`: {}", std::string_view(key), e.what());
      }
      if (out.contains(key))
        throw parse_error("duplicated key `{}` while parsing string map of `{}`", std::string_view(key),
                          type_name<T>());
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
      throw parse_error("string expected for parameter_location, got `{}`", e2str(v.kind()));
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
      throw parse_error("field `{}` in type `{}` is REQUIRED but not present", name, type_name<T>());
  }
  parse<Field>{}(it->value(), boost::pfr::get<I>(out));
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
      if constexpr (requires {
                      { T::parse_if_not_object(v) } -> std::same_as<T>;
                    }) {
        out = T::parse_if_not_object(v);
        return;
      }
      throw parse_error("object expected, got `{}`", e2str(v.kind()));
    }
    for (auto&& [key, _] : *obj) {
      std::string_view k(key);
      if (!noexport::has_field<T>(k) && !allow_additional_field<T>{}.allow(k))
        throw parse_error("not allowed field `{}` in type `{}`", k, type_name<T>());
    }
    noexport::parse_all_fields(*obj, out, std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
    validate<T>{}(out);
  }
};

template <typename T>
struct parse<reference_or<T>> {
  void operator()(boost::json::value const& v, reference_or<T>& out) {
    // path_item_object содержит в себе опциональную ссылку, его так парсить было бы неверно и бессмысленно
    static_assert(!std::is_same_v<T, path_item_object>);
    auto const* obj = v.if_object();
    if (!obj)
      throw parse_error("object expected, got `{}`", e2str(v.kind()));
    if (obj->contains("$ref")) {
      reference_object ref{};
      parse<reference_object>{}(v, ref);
      out = std::move(ref);
    } else {
      T val{};
      parse<T>{}(v, val);
      out = std::move(val);
    }
  }
};

}  // namespace svarg
