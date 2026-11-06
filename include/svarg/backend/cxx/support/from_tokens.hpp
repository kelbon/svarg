#pragma once

#include "svarg/backend/cxx/support/stream_parsing.hpp"
#include "svarg/errors.hpp"
#include "svarg/utils/safe_cast.hpp"
#include "svarg/backend/cxx/support/validate_stream.hpp"
#include "svarg/backend/cxx/support/types.hpp"

#include <metel/box.hpp>

#include <bitset>
#include <format>

namespace svarg::cxx_support {

template <>
struct from_tokens<boolean> {
  static tokens_consumer operator()(bool& v, sax_token& tok, dd::with_stack_resource) {
    tok.expect(sax_token::bool_);
    v = tok.get_bool();
    return {};
  }
};

template <>
struct from_tokens<std::string> {
  static tokens_consumer operator()(std::string& v, sax_token& tok, dd::with_stack_resource) {
    tok.expect(sax_token::string);
    v = tok.get_str();
    return {};
  }
};

template <typename T>
  requires(std::integral<T>)
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    using enum sax_token::kind_e;
    switch (tok.kind()) {
      case int64:
        safe_write(tok.get_int64(), v);
        return {};
      case uint64:
        safe_write(tok.get_uint64(), v);
        return {};
      default:
        throw svarg::parse_error("expected int64/uint64");
    }
  }
};

// целые тоже подходят, как в DOM
template <typename T>
  requires(std::floating_point<T>)
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    tok.expect(sax_token::double_ | sax_token::int64 | sax_token::uint64);
    if (tok.kind() == sax_token::int64)
      v = static_cast<T>(tok.get_int64());
    else if (tok.kind() == sax_token::uint64)
      v = static_cast<T>(tok.get_uint64());
    else
      v = static_cast<T>(tok.get_double());
    return {};
  }
};

template <typename T>
struct from_tokens<arrayof<T>> {
  static tokens_consumer operator()(arrayof<T>& v, sax_token& tok, dd::with_stack_resource r) {
    tok.expect(sax_token::array_begin);
    for (;;) {
      co_yield {};
      if (tok.kind() == sax_token::array_end)
        break;
      co_yield dd::elements_of(from_tokens_subobject(v.emplace_back(), tok, r));
    }
  }
};

// значение внутри проверяется по своим требованиям, как и в DOM
template <typename T>
struct from_tokens<std::optional<T>> {
  static tokens_consumer operator()(std::optional<T>& v, sax_token& tok, dd::with_stack_resource r) {
    if (tok.kind() == sax_token::null) [[unlikely]] {
      v.reset();
      return {};
    }
    return from_tokens_subobject(v.emplace(), tok, r);
  }
};

template <typename T>
struct from_tokens<metel::box<T>> {
  static tokens_consumer operator()(metel::box<T>& v, sax_token& tok, dd::with_stack_resource r) {
    if (tok.kind() == sax_token::null) [[unlikely]] {
      v.reset();
      return {};
    }
    return from_tokens_subobject(v.emplace(), tok, r);
  }
};

template <an_fundamental T>
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    // требования проверяются выше, а больше кроме требований и значения в этом типе ничего нет
    return from_tokens_parser(get_special_field(v), tok, r);
  }
};

template <an_array T>
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    // требования проверяются выше, а больше кроме требований и значения в этом типе ничего нет
    return from_tokens_parser(get_special_field(v), tok, r);
  }
};

template <a_not_a T>
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    // требования проверяются выше, а больше кроме требований и значения в этом типе ничего нет
    return from_tokens_parser(get_special_field(v), tok, r);
  }
};

template <>
struct from_tokens<number> {
  static tokens_consumer operator()(number& v, sax_token& tok, dd::with_stack_resource) {
    tok.expect(sax_token::int64 | sax_token::uint64 | sax_token::double_);
    if (tok.kind() == sax_token::uint64)
      v = tok.get_uint64();
    else if (tok.kind() == sax_token::int64)
      v = tok.get_int64();
    else
      v = tok.get_double();
    return {};
  }
};

template <>
struct from_tokens<null> {
  static tokens_consumer operator()(null&, sax_token& tok, dd::with_stack_resource) {
    if (tok.kind() != sax_token::null)
      throw svarg::parse_error("expected `null`, got `{}`", tok);
    return {};
  }
};

template <>
struct from_tokens<never> {
  static tokens_consumer operator()(never&, sax_token& tok, dd::with_stack_resource) {
    throw validate_error("no value matches `never` schema, got `{}`", tok);
  }
};

// значение не должно подходить под T: T разбирается параллельно с пропуском значения
template <typename T>
struct from_tokens<not_a<T>> {
  static tokens_consumer operator()(not_a<T>&, sax_token& tok, dd::with_stack_resource r) {
    T value;
    noexport::parallel_parsers<1> p;
    tokens_consumer skip = noexport::skip_json_value(tok, r);
    p.start(0, value, tok);
    co_yield dd::elements_of(noexport::feed_in_parallel(skip, p.parsers, p.errors));
    if (p.parsed(0))
      throw svarg::parse_error("when parsing not_a<{}>. Unexpected success of parsing", type_name<T>());
  }
};

// значение должно подходить ровно под одну схему: все разбираются параллельно
template <typename... Types>
struct from_tokens<oneof<Types...>> {
  static tokens_consumer operator()(oneof<Types...>& v, sax_token& tok, dd::with_stack_resource r) {
    std::tuple<Types...> values;
    noexport::parallel_parsers<sizeof...(Types)> p;
    tokens_consumer skip = noexport::skip_json_value(tok, r);
    [&]<size_t... I>(std::index_sequence<I...>) {
      (p.start(I, std::get<I>(values), tok), ...);
    }(std::index_sequence_for<Types...>{});
    co_yield dd::elements_of(noexport::feed_in_parallel(skip, p.parsers, p.errors));
    size_t success = 0;
    [&]<size_t... I>(std::index_sequence<I...>) {
      auto take = [&]<size_t J>(std::index_sequence<J>) {
        if (p.parsed(J) && ++success == 1)
          v.template emplace<std::tuple_element_t<J, std::tuple<Types...>>>(std::move(std::get<J>(values)));
      };
      (take(std::index_sequence<I>{}), ...);
    }(std::index_sequence_for<Types...>{});
    if (success == 0)
      throw svarg::parse_error("oneOf failed to parse any schema in list");
    if (success > 1) {
      v = nullptr;
      throw svarg::parse_error("oneOf parse failed, {} schemas parsed", success);
    }
  }
};

// заполняются все подошедшие части
template <typename... Types>
struct from_tokens<anyof<Types...>> {
  static tokens_consumer operator()(anyof<Types...>& v, sax_token& tok, dd::with_stack_resource r) {
    noexport::parallel_parsers<sizeof...(Types)> p;
    tokens_consumer skip = noexport::skip_json_value(tok, r);
    [&]<size_t... I>(std::index_sequence<I...>) {
      (p.start(I, v.template get<I>().emplace(), tok), ...);
    }(std::index_sequence_for<Types...>{});
    co_yield dd::elements_of(noexport::feed_in_parallel(skip, p.parsers, p.errors));
    size_t success = 0;
    [&]<size_t... I>(std::index_sequence<I...>) {
      auto keep = [&]<size_t J>(std::index_sequence<J>) {
        if (p.parsed(J))
          ++success;
        else
          v.template get<J>().reset();  // не подошедшая часть не должна остаться частично заполненной
      };
      (keep(std::index_sequence<I>{}), ...);
    }(std::index_sequence_for<Types...>{});
    if (success == 0)
      throw svarg::parse_error("anyOf failed to parse any schema in list");
  }
};

// значение должно подходить под все схемы, ошибка - первая по порядку
template <typename... Types>
struct from_tokens<allof<Types...>> {
  static tokens_consumer operator()(allof<Types...>& v, sax_token& tok, dd::with_stack_resource r) {
    noexport::parallel_parsers<sizeof...(Types)> p;
    tokens_consumer skip = noexport::skip_json_value(tok, r);
    [&]<size_t... I>(std::index_sequence<I...>) {
      (p.start(I, v.template get<Types>(), tok), ...);
    }(std::index_sequence_for<Types...>{});
    co_yield dd::elements_of(noexport::feed_in_parallel(skip, p.parsers, p.errors));
    p.rethrow_first_error();
  }
};

namespace noexport {

template <typename>
struct alternatives_of;

template <typename... Types>
struct alternatives_of<oneof<Types...>> {
  using tuple = std::tuple<Types...>;
};

template <typename... Types>
struct alternatives_of<anyof<Types...>> {
  using tuple = std::tuple<Types...>;
};

template <typename X, typename Tuple>
constexpr size_t index_in = [] {
  return []<size_t... I>(std::index_sequence<I...>) {
    size_t i = 0;
    ((std::is_same_v<X, std::tuple_element_t<I, Tuple>> ? (void)(i = I) : (void)0), ...);
    return i;
  }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}();

// пропускает объект, запоминая значение свойства верхнего уровня `field`: строку, или nullopt если свойства
// нет либо оно не строка. Повторяющийся ключ - последний выигрывает, как в DOM
inline tokens_consumer watch_property(sax_token& tok, std::string_view field, std::optional<std::string>& value,
                                      dd::with_stack_resource r) {
  for (;;) {
    co_yield {};
    if (tok.kind() == sax_token::object_end)
      break;
    bool is_field = tok.get_key() == field;
    co_yield {};
    if (is_field)
      value = tok.kind() == sax_token::string ? std::optional<std::string>(tok.get_str()) : std::nullopt;
    co_yield dd::elements_of(skip_json_value(tok, r));
  }
}

// anyOf/oneOf с дискриминатором, значение - объект: вариант выбирается по свойству-дискриминатору, и важен
// только его разбор (остальные не проверяются, как в DOM). Где в объекте дискриминатор, заранее неизвестно,
// поэтому разбираются все варианты сразу
template <typename T>
tokens_consumer from_tokens_discriminated(T& v, sax_token& tok, dd::with_stack_resource r) {
  using field_t = std::remove_cvref_t<decltype(get_special_field(v))>;
  using tuple = typename alternatives_of<field_t>::tuple;
  tuple values;
  parallel_parsers<std::tuple_size_v<tuple>> p;
  std::optional<std::string> discriminator;
  tokens_consumer watch = watch_property(tok, T::svarg_discriminator_field, discriminator, r);
  [&]<size_t... I>(std::index_sequence<I...>) {
    (p.start(I, std::get<I>(values), tok), ...);
  }(std::make_index_sequence<std::tuple_size_v<tuple>>{});
  co_yield dd::elements_of(feed_in_parallel(watch, p.parsers, p.errors));
  if (!discriminator) {
    throw svarg::parse_error("discriminator property `{}` of `{}` is missing or is not a string",
                             std::string_view(T::svarg_discriminator_field), type_name<T>());
  }
  T::svarg_discriminate(*discriminator, [&]<typename X>() {
    if constexpr (std::is_void_v<X>) {
      throw svarg::parse_error("unknown discriminator value `{}` of `{}`", *discriminator, type_name<T>());
    } else {
      constexpr size_t i = index_in<X, tuple>;
      if (!p.parsed(i))
        std::rethrow_exception(p.errors[i]);
      auto& field = get_special_field(v);
      if constexpr (tagged_as<T>(parse_tag_e::anyof))
        field.template get<X>().emplace(std::move(std::get<i>(values)));
      else
        field.template emplace<X>(std::move(std::get<i>(values)));
    }
  });
}

}  // namespace noexport

// дискриминатор - свойство объекта, остальные значения разбираются как без него
template <typename T>
  requires(an_anyof<T> || a_oneof<T>)
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    if constexpr (has_discriminator<T>) {
      if (tok.kind() == sax_token::object_begin)
        return noexport::from_tokens_discriminated(v, tok, r);
    }
    return from_tokens_parser(get_special_field(v), tok, r);
  }
};

template <an_allof T>
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    return from_tokens_parser(get_special_field(v), tok, r);
  }
};

template <a_string_enum T>
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource) {
    // по сути парсер для строки + встроенная валидация
    tok.expect(sax_token::string);
    std::string_view str = tok.get_str();
    auto it = std::ranges::find(T::values, str);
    if (it == T::values.end())
      throw validate_error("string {} cannot be value of enum {}", str, type_name<T>());
    // енам генерируется так, что порядок имён совпадает с порядком в массиве
    get_special_field(v) = typename T::svarg_values_e(std::distance(T::values.begin(), it));
    return {};
  }
};

template <an_open_string_enum T>
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource) {
    tok.expect(sax_token::string);
    v.value = tok.get_str();
    return {};
  }
};

template <an_enum T>
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource) {
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

// как aggregate_from_json: требования самого объекта проверяются выше (from_tokens_subobject), поле - по
// своим. Повторяющийся ключ - последний выигрывает, как в DOM
template <an_object T>
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    static_assert(valid_generated_object<T>());
    tok.expect(sax_token::object_begin);
    std::bitset unparsed = get_required_bits_mask<T>();
    for (;;) {
      co_yield {};
      if (tok.kind() == sax_token::object_end)
        break;
      // ключ жив только до следующего токена
      std::optional<size_t> i;
      if constexpr (fields_count<T>() > 0)
        i = get_field_index<T>(tok.get_key());
      std::string name;
      if (!i) {
        if constexpr (forbids_additional_properties<T>)
          throw svarg::parse_error("field `{}` unexpected in type `{}`", tok.get_key(), type_name<T>());
        if constexpr (has_additional_properties<T>)
          name = tok.get_key();
      }
      co_yield {};  // первый токен значения
      if constexpr (fields_count<T>() > 0) {
        if (i) {
          tokens_consumer field = visit_field_index<fields_count<T>()>(
              [&]<size_t I>() {
                if constexpr (get_field_info<I, T>().required)
                  unparsed.set(I, false);
                auto& f = get_field<I>(v);
                f = std::remove_cvref_t<decltype(f)>{};  // повторный ключ не дописывает в старое значение
                return from_tokens_subobject(f, tok, r);
              },
              *i);
          co_yield dd::elements_of(std::move(field));
          continue;
        }
      }
      if constexpr (has_additional_properties<T>) {
        additional_properties_schema_t<T> property;
        co_yield dd::elements_of(from_tokens_subobject(property, tok, r));
        get_additional_properties(v).insert_or_assign(std::move(name), std::move(property));
      } else {
        co_yield dd::elements_of(omo::collect_json_value_tokens(tok, [](sax_token&) {}, r));
      }
    }
    if (unparsed.any()) {
      throw svarg::parse_error("not all required fields of type `{}` parsed, unparsed: {}", type_name<T>(),
                               unparsed.to_string());
    }
  }
};

template <a_body T>
struct from_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    return from_tokens_subobject(get_special_field(v), tok, r);
  }
};

}  // namespace svarg::cxx_support
