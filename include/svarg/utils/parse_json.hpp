#pragma once

#include <format>

#include <boost/pfr.hpp>

#include "svarg/fundamental_types.hpp"
#include "svarg/utils/type_name.hpp"
#include "svarg/utils/unescape_field_name.hpp"
#include "svarg/validate.hpp"

namespace svarg {

inline std::string_view e2str(boost::json::kind k) {
  switch (k) {
    case boost::json::kind::null:
      return "null";
    case boost::json::kind::bool_:
      return "bool";
    case boost::json::kind::int64:
      return "int64";
    case boost::json::kind::uint64:
      return "uint64";
    case boost::json::kind::double_:
      return "double";
    case boost::json::kind::string:
      return "string";
    case boost::json::kind::array:
      return "array";
    case boost::json::kind::object:
      return "object";
  }
  std::unreachable();
}

template <typename T>
struct parse {};

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
struct parse<string> {
  void operator()(boost::json::value const& v, string& out) {
    if (auto* s = v.if_string())
      out = s->subview();
    else
      throw parse_error("string expected, got `{}`", e2str(v.kind()));
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
        throw parse_error("while parsing string map key `{}`: {}", key, e.what());
      }
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

template <aggregate T, size_t I>
void parse_field(boost::json::object const& obj, T& out) {
  using Field = std::decay_t<decltype(boost::pfr::get<I>(out))>;
  constexpr std::string_view name = unescape_field_name(boost::pfr::get_name<I, T>());
  auto it = obj.find(name);
  if (it == obj.end()) {
    if (is_optional_field<std::decay_t<T>>::value)
      return;
    else
      throw parse_error("field `{}` in type `{}` is REQUIRED but not present", name, type_name<T>());
  }
  parse<Field>{}(it->value(), boost::pfr::get<I>(out));
}

template <aggregate T, std::size_t... Is>
void parse_all_fields(boost::json::object const& obj, T& out, std::index_sequence<Is...>) {
  (parse_field<T, Is>(obj, out), ...);
}

}  // namespace noexport

template <aggregate T>
struct parse<T> {
  static_assert(std::is_same_v<T, std::decay_t<T>>);

  void operator()(boost::json::value const& v, T& out) {
    auto const* obj = v.if_object();
    if (!obj)
      throw parse_error("object expected, got `{}`", e2str(v.kind()));
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
