#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/json/value.hpp>
#include <metel/box.hpp>

#include "svarg/errors.hpp"
#include "svarg/backend/cxx/support/to_json_value.hpp"
#include "svarg/backend/cxx/support/stream_parsing.hpp"

namespace svarg::cxx_support {

template <typename T>
struct to_tokens;  // не определён: тип обходится через to_json

template <typename T>
concept has_to_tokens = requires { sizeof(to_tokens<std::remove_cvref_t<T>>); };

namespace noexport {

template <typename W>
void json_value_to_tokens(const boost::json::value& v, W& w) {
  switch (v.kind()) {
    case boost::json::kind::null:
      return w.on_null();
    case boost::json::kind::bool_:
      return w.on_bool(v.get_bool());
    case boost::json::kind::int64:
      return w.on_int64(v.get_int64());
    case boost::json::kind::uint64:
      return w.on_uint64(v.get_uint64());
    case boost::json::kind::double_:
      return w.on_double(v.get_double());
    case boost::json::kind::string:
      return w.on_string(std::string_view(v.get_string().subview()));
    case boost::json::kind::array:
      w.on_array_begin();
      for (const boost::json::value& x : v.get_array())
        json_value_to_tokens(x, w);
      return w.on_array_end();
    case boost::json::kind::object:
      w.on_object_begin();
      for (const boost::json::key_value_pair& kv : v.get_object()) {
        w.on_key(std::string_view(kv.key()));
        json_value_to_tokens(kv.value(), w);
      }
      return w.on_object_end();
  }
}

// строку можно записать в JSON как есть, без экранирования
constexpr bool json_safe(std::string_view s) {
  return std::ranges::none_of(
      s, [](char c) { return c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20; });
}

// on_escaped_key/on_escaped_string необязательны
void write_escaped_key(auto& w, std::string_view k) {
  if constexpr (requires { w.on_escaped_key(k); })
    w.on_escaped_key(k);
  else
    w.on_key(k);
}

void write_escaped_string(auto& w, std::string_view s) {
  if constexpr (requires { w.on_escaped_string(s); })
    w.on_escaped_string(s);
  else
    w.on_string(s);
}

}  // namespace noexport

// события `v` для писателя `w` (serialize_tokens_consumer)
template <typename T, serialize_tokens_consumer W>
void write_tokens(const T& v, W& w) {
  if constexpr (has_to_tokens<T>)
    to_tokens<std::remove_cvref_t<T>>{}(v, w);
  else
    noexport::json_value_to_tokens(to_json(v), w);
}

namespace noexport {

template <typename T, size_t I>
void write_field_key(auto& w) {
  constexpr std::string_view name = get_field_name<I, T>();
  if constexpr (json_safe(name))
    noexport::write_escaped_key(w, name);
  else
    w.on_key(name);
}

// свойства объекта без begin/end. Discriminated - oneOf, дискриминатор которого уже записан (одноимённое
// свойство пропускается), иначе void
template <typename Discriminated, typename T, typename W>
void object_fields_to_tokens(const T& v, W& w) {
  auto field_to_tokens = [&]<size_t I>(std::index_sequence<I>) {
    constexpr std::string_view name = get_field_name<I, T>();
    if constexpr (!std::is_void_v<Discriminated>) {
      if constexpr (name == Discriminated::svarg_discriminator_field)
        return;
    }
    // одноимённое дополнительное свойство заменяет поле, как в to_json
    if constexpr (has_additional_properties<T>) {
      if (get_additional_properties(v).contains(name))
        return;
    }
    constexpr field_info info = get_field_info<I, T>();
    const auto& field = get_field<I>(v);
    if (is_field_present<I>(v)) {
      write_field_key<T, I>(w);
      // необязательное поле в optional/box: присутствует - значит не пусто
      if constexpr (!info.required && requires {
                      field.has_value();
                      *field;
                    })
        write_tokens(*field, w);
      else
        write_tokens(field, w);
    } else if constexpr (info.null_when_absent) {
      write_field_key<T, I>(w);
      w.on_null();
    }
  };
  [&]<size_t... I>(std::index_sequence<I...>) {
    (field_to_tokens(std::index_sequence<I>{}), ...);
  }(std::make_index_sequence<fields_count<T>()>{});
  if constexpr (has_additional_properties<T>) {
    for (const auto& [name, value] : get_additional_properties(v)) {
      if constexpr (!std::is_void_v<Discriminated>) {
        if (name == Discriminated::svarg_discriminator_field)
          continue;
      }
      w.on_key(name);
      write_tokens(value, w);
    }
  }
}

}  // namespace noexport

// ПРОСТЫЕ ТИПЫ

template <>
struct to_tokens<bool> {
  static void operator()(bool v, auto& w) {
    w.on_bool(v);
  }
};

template <std::integral T>
  requires(!std::is_same_v<T, bool>)
struct to_tokens<T> {
  static void operator()(T v, auto& w) {
    if constexpr (std::is_signed_v<T>)
      w.on_int64(v);
    else
      w.on_uint64(v);
  }
};

template <std::floating_point T>
struct to_tokens<T> {
  static void operator()(T v, auto& w) {
    w.on_double(double(v));
  }
};

template <>
struct to_tokens<std::string> {
  static void operator()(std::string_view v, auto& w) {
    w.on_string(v);
  }
};

template <>
struct to_tokens<std::string_view> {
  static void operator()(std::string_view v, auto& w) {
    w.on_string(v);
  }
};

template <>
struct to_tokens<null> {
  static void operator()(const null&, auto& w) {
    w.on_null();
  }
};

template <>
struct to_tokens<never> {
  [[noreturn]] static void operator()(const never&, auto&) {
    throw serialize_error("value of `never` schema cannot be serialized");
  }
};

template <>
struct to_tokens<number> {
  static void operator()(const number& v, auto& w) {
    std::visit([&](auto x) { write_tokens(x, w); }, v.data);
  }
};

template <>
struct to_tokens<boost::json::value> {
  static void operator()(const boost::json::value& v, auto& w) {
    noexport::json_value_to_tokens(v, w);
  }
};

// КОНТЕЙНЕРЫ

template <typename T, typename A>
struct to_tokens<std::vector<T, A>> {
  static void operator()(const std::vector<T, A>& v, auto& w) {
    w.on_array_begin();
    for (const auto& x : v)
      write_tokens<T>(x, w);  // T явно: у vector<bool> элемент - прокси
    w.on_array_end();
  }
};

template <typename T>
struct to_tokens<std::optional<T>> {
  static void operator()(const std::optional<T>& v, auto& w) {
    if (v)
      write_tokens(*v, w);
    else
      w.on_null();
  }
};

template <typename T>
struct to_tokens<metel::box<T>> {
  static void operator()(const metel::box<T>& v, auto& w) {
    if (v)
      write_tokens(*v, w);
    else
      w.on_null();
  }
};

namespace noexport {

template <typename M>
void map_to_tokens(const M& m, auto& w) {
  w.on_object_begin();
  for (const auto& [k, v] : m) {
    w.on_key(k);
    write_tokens(v, w);
  }
  w.on_object_end();
}

}  // namespace noexport

template <typename T, typename C, typename A>
struct to_tokens<std::map<std::string, T, C, A>> {
  static void operator()(const std::map<std::string, T, C, A>& m, auto& w) {
    noexport::map_to_tokens(m, w);
  }
};

template <typename T, typename H, typename E, typename A>
struct to_tokens<std::unordered_map<std::string, T, H, E, A>> {
  static void operator()(const std::unordered_map<std::string, T, H, E, A>& m, auto& w) {
    noexport::map_to_tokens(m, w);
  }
};

// КОМБИНАЦИИ СХЕМ

template <typename... Types>
struct to_tokens<oneof<Types...>> {
  static void operator()(const oneof<Types...>& v, auto& w) {
    v.visit(
        metel::matcher{[](metel::nothing_t) { throw serialize_error("empty oneOf cannot be serialized"); },
                       [&](const auto& x) { write_tokens(x, w); }});
  }
};

// одна часть обходится как есть, несколько сливаются через to_json
template <typename... Types>
struct to_tokens<anyof<Types...>> {
  static void operator()(const anyof<Types...>& v, auto& w) {
    size_t count = 0;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((count += bool(v.template get<Is>())), ...);
    }(std::index_sequence_for<Types...>{});
    if (count != 1)
      return noexport::json_value_to_tokens(to_json(v), w);
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      auto part_to_tokens = [&]<size_t I>(std::index_sequence<I>) {
        if (const auto& part = v.template get<I>())
          write_tokens(*part, w);
      };
      (part_to_tokens(std::index_sequence<Is>{}), ...);
    }(std::index_sequence_for<Types...>{});
  }
};

template <typename... Types>
struct to_tokens<allof<Types...>> {
  static void operator()(const allof<Types...>& v, auto& w) {
    noexport::json_value_to_tokens(to_json(v), w);
  }
};

// СГЕНЕРИРОВАННЫЕ ТИПЫ

// схема-обёртка над единственным значением (fundamental, array, enum, anyOf, allOf, тело)
template <typename T>
  requires(an_fundamental<T> || an_array<T> || an_enum<T> || an_anyof<T> || an_allof<T> || a_body<T>)
struct to_tokens<T> {
  static void operator()(const T& v, auto& w) {
    write_tokens(get_special_field(v), w);
  }
};

template <a_string_enum T>
struct to_tokens<T> {
  static constexpr bool safe = std::ranges::all_of(T::values, noexport::json_safe);

  static void operator()(const T& v, auto& w) {
    std::string_view s = T::values[std::to_underlying(get_special_field(v))];
    if constexpr (safe)
      noexport::write_escaped_string(w, s);
    else
      w.on_string(s);
  }
};

template <an_open_string_enum T>
struct to_tokens<T> {
  static void operator()(const T& v, auto& w) {
    w.on_string(v.value);
  }
};

template <an_object T>
struct to_tokens<T> {
  static void operator()(const T& v, auto& w) {
    static_assert(valid_generated_object<T>());
    w.on_object_begin();
    noexport::object_fields_to_tokens<void>(v, w);
    w.on_object_end();
  }
};

// дискриминатор - первое свойство объекта варианта
template <a_oneof T>
struct to_tokens<T> {
  static void operator()(const T& v, auto& w) {
    if constexpr (!has_discriminator<T>) {
      write_tokens(get_special_field(v), w);
    } else {
      get_special_field(v).visit(
          metel::matcher{[](metel::nothing_t) { throw serialize_error("cannot serialize empty oneOf"); },
                         [&]<typename X>(const X& x) {
                           if constexpr (an_object<X>) {
                             std::string_view discriminator = T::template svarg_discriminator_for<X>();
                             if (discriminator.empty())
                               return write_tokens(x, w);
                             w.on_object_begin();
                             w.on_key(T::svarg_discriminator_field);
                             w.on_string(discriminator);
                             noexport::object_fields_to_tokens<T>(x, w);
                             w.on_object_end();
                           } else {
                             // объект ли это, известно только по JSON
                             noexport::json_value_to_tokens(to_json(v), w);
                           }
                         }});
    }
  }
};

}  // namespace svarg::cxx_support
