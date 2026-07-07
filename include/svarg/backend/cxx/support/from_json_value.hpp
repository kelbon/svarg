#pragma once

#include "svarg/backend/cxx/support/types.hpp"
#include "svarg/errors.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"

#include "svarg/from_json_value.hpp"

namespace svarg {

template <cxx_support::an_array T>
struct from_json_value<T> {
  static error_code operator()(T& v, const boost::json::value& j) {
    // TODO что с требованиями... Что-то типа
    // * проверка что нет рекурсивных требований
    // * для каждой схемы на один уровень выше
    //    - если нужно парсить - парсим
    //    - если нужно проверить конечный объект - проверяем его
    // TODO не забыть для объектов проверять что все обязательные поля на месте
    // требования проверяются выше, а больше кроме требований и значения в этом типе ничего нет
    return try_from_json(get_special_field(v), j);
  }
};
#if 0
template <cxx_support::a_not_a T>
struct from_json_value<T> {
  static error_code operator()(T& v, const boost::json::value& j) {
    // требования проверяются выше, а больше кроме требований и значения в этом типе ничего нет
    return from_tokens_parser(get_special_field(v), tok, r);
  }
};

template <cxx_support::a_string_enum T>
struct from_json_value<T> {
  static error_code operator()(T& v, const boost::json::value& j) {
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

template <cxx_support::an_enum T>
struct from_json_value<T> {
  static error_code operator()(T& v, const boost::json::value& j) {
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

template <cxx_support::an_anyof T>
struct from_json_value<T> {
  static error_code operator()(T& v, const boost::json::value& j) {
    TODO;
  }
};

template <cxx_support::a_oneof T>
struct from_json_value<T> {
  static error_code operator()(T& v, const boost::json::value& j) {
    TODO;
  }
};

template <cxx_support::an_allof T>
struct from_json_value<T> {
  static error_code operator()(T& v, const boost::json::value& j) {
    TODO;
  }
};

template <cxx_support::an_object T>
struct from_json_value<T> {
  static error_code operator()(T& v, const boost::json::value& j) {
    TODO;
  }
};

template <cxx_support::a_body T>
struct from_json_value<T> {
  static error_code operator()(T& v, const boost::json::value& j) {
    TODO;
  }
};
#endif

}  // namespace svarg
