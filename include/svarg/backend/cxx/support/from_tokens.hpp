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
