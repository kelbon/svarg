#pragma once

#include "svarg/backend/cxx/support/types.hpp"
#include "svarg/errors.hpp"

#include <boost/json/value.hpp>

#include "svarg/parse.hpp"

namespace svarg {
#if 0

template <typename Optional>
struct optional_parser_base {
  using T = typename Optional::value_type;

  static void parse(Optional& v, const boost::json::value& j, error_code& r) {
    if (tok.kind() == sax_token::null) [[unlikely]]
      return {};
    return from_tokens_parser(v.emplace(), tok, r);
  }
};

template <typename T>
struct from_json_value<std::optional<T>> : optional_parser_base<std::optional<T>> {};

template <typename T>
struct from_json_value<metel::box<T>> : optional_parser_base<metel::box<T>> {};

template <an_array T>
struct from_json_value<T> {
  static void parse(T& v, const boost::json::value& j, error_code& r) {
    // требования проверяются выше, а больше кроме требований и значения в этом типе ничего нет
    return from_tokens_parser(get_special_field(v), tok, r);
  }
};

template <a_not_a T>
struct from_json_value<T> {
  static void parse(T& v, const boost::json::value& j, error_code& r) {
    // требования проверяются выше, а больше кроме требований и значения в этом типе ничего нет
    return from_tokens_parser(get_special_field(v), tok, r);
  }
};

template <a_string_enum T>
struct from_json_value<T> {
  static void parse(T& v, const boost::json::value& j, error_code&) {
    // по сути парсер для строки + встроенная валидация
    tok.expect(sax_token::string);
    std::string_view str = tok.get_str();
    auto it = std::ranges::find(T::values, str);
    if (it == T::values.end())
      throw validate_error("string {} cannot be value of enum {}", str, type_name<T>());
    // енам генерируется так, что порядок имён совпадает с порядком в массиве
    get_special_field(v) = T::values_e(std::distance(T::values.begin(), it));
    return {};
  }
};

template <an_enum T>
struct from_json_value<T> {
  static void parse(T& v, const boost::json::value& j, error_code&) {
    using underlying = std::decay_t<decltype(get_special_field(v))>;

    auto invalid_value = [](auto&& v) {
      return validate_error("value {} cannot represent enum {}", v, type_name<T>());
    };
    // рендер не создаёт енамов нетривиального типа
    // только bool, int64_t, double, null
    if constexpr (std::is_same_v<underlying, integer>) {
      integer i;
      if (tok.kind() == sax_token::int64)
        i = tok.get_int64();
      else if (tok.kind() == sax_token::uint64)
        i = tok.get_uint64();
      else  // throw
        tok.expect(sax_token::int64 | sax_token::uint64);
      if (!std::ranges::contains(T::values, i))
        throw invalid_value(i);
      get_special_field(v) = i;
    } else if constexpr (std::is_same_v<underlying, boolean>) {
      tok.expect(sax_token::bool_);
      boolean b = tok.get_bool();
      if (!std::ranges::contains(T::values, b))
        throw invalid_value(b);
      get_special_field(v) = b;
    } else if constexpr (std::is_same_v<underlying, number>) {
      number d;
      if (tok.kind() == sax_token::int64)
        d = tok.get_int64();
      else if (tok.kind() == sax_token::uint64)
        d = tok.get_uint64();
      else if (tok.kind() == sax_token::double_)
        d = tok.get_double();
      else  // throw
        tok.expect(sax_token::int64 | sax_token::uint64 | sax_token::double_);
      if (!std::ranges::contains(T::values, d))  // сравнивает на ==, плевать
        throw invalid_value(d);
      get_special_field(v) = d;
    } else if constexpr (std::is_same_v<underlying, null>) {
      tok.expect(sax_token::kind_e::null);
    } else {
      // C++23 позволяет так делать
      static_assert(false, "unsupported enum type");
    }
    return {};
  }
};

template <an_anyof T>
struct from_json_value<T> {
  static void parse(T& v, const boost::json::value& j, error_code& r) {
    return from_tokens_fallback(v, tok, r);
  }
};

template <a_oneof T>
struct from_json_value<T> {
  static void parse(T& v, const boost::json::value& j, error_code& r) {
    return from_tokens_fallback(v, tok, r);
  }
};

template <an_allof T>
struct from_json_value<T> {
  static void parse(T& v, const boost::json::value& j, error_code& r) {
    return from_tokens_fallback(v, tok, r);
  }
};

template <an_object T>
struct from_json_value<T> {
  static void parse(T& v, const boost::json::value& j, error_code& r) {
    // TODO здесь легко заменить фалбек
    return from_tokens_fallback(v, tok, r);
  }
};

template <a_body T>
struct from_json_value<T> {
  static void parse(T& v, const boost::json::value& j, error_code& r) {
    // TODO здесь легко заменить фалбек
    return from_tokens_fallback(v, tok, r);
  }
};

template <typename... Types>
struct from_json_value<anyof<Types...>> {
  static void parse(anyof<Types...>& v, const boost::json::value& j, error_code& r) {
    return from_tokens_fallback(v, tok, r);
  }
};

template <typename... Types>
struct from_json_value<oneof<Types...>> {
  static void parse(oneof<Types...>& v, const boost::json::value& j, error_code& r) {
    return from_tokens_fallback(v, tok, r);
  }
};

template <typename... Types>
struct from_json_value<allof<Types...>> {
  static void parse(allof<Types...>& v, const boost::json::value& j, error_code& r) {
    return from_tokens_fallback(v, tok, r);
  }
};

template <typename T>
struct from_json_value<not_a<T>> {
  static void parse(not_a<T>& v, const boost::json::value& j, error_code& r) {
    // TODO вполне заменяется, единственная проблема это заменить исключение на еррор код
    return from_tokens_fallback(v, tok, r);
  }
};
#endif
}  // namespace svarg
