#pragma once

#include "concepts.hpp"

namespace svarg::cxx_support {

/*

template <typename T>
concept an_array = tagged_as<T>(parse_tag_e::array);

template <typename T>
concept an_anyof = tagged_as<T>(parse_tag_e::anyof);

template <typename T>
concept a_oneof = tagged_as<T>(parse_tag_e::oneof);

template <typename T>
concept an_allof = tagged_as<T>(parse_tag_e::allof);

template <typename T>
concept a_not_a = tagged_as<T>(parse_tag_e::not_a);

template <typename T>
concept an_object = tagged_as<T>(parse_tag_e::object);

template <typename T>
concept an_enum = tagged_as<T>(parse_tag_e::enum_);

template <typename T>
concept a_string_enum = tagged_as<T>(parse_tag_e::string_enum);

template <typename T>
concept a_body = tagged_as<T>(parse_tag_e::body);

+ алиасы которые просто есть

+ arrayof
+ box
+ not
+ anyof
+ allof
+ oneof
+ string
...


*/

template <>
struct from_tokens<bool> {
  static tokens_consumer parse(bool& v, sax_token& tok, dd::with_stack_resource) {
    tok.expect(sax_token::bool_);
    v = tok.bool_m;
    return {};
  }
};

template <>
struct from_tokens<std::string> {
  static tokens_consumer parse(std::string& v, sax_token& tok, dd::with_stack_resource r) {
    tok.expect(sax_token::string);
    v = tok.str_m;
    return {};
  }
};

template <typename T>
  requires(std::integral<T>)
struct from_tokens<T> {
  static tokens_consumer parse(T& v, sax_token& tok, dd::with_stack_resource r) {
    using enum sax_token::kind_e;
    switch (tok.got) {
      case int64:
        safe_write(v, tok.int_m);
        return {};
      case uint64:
        safe_write(v, tok.uint_m);
        return {};
      default:
        throw parse_error(std::format("expected int64/uint64, got {}", tok));
    }
  }
};

template <typename T>
  requires(std::floating_point<T> || std::same_as<api::Double, T>)
struct from_tokens<T> {
  static tokens_consumer parse(T& v, sax_token& tok, dd::with_stack_resource r) {
    tok.expect(sax_token::double_);
    v = tok.double_m;
    return {};
  }
};

template <typename T>
struct from_tokens<std::vector<T>> {
  static tokens_consumer parse(std::vector<T>& v, sax_token& tok, dd::with_stack_resource r) {
    tok.expect(sax_token::array_begin);
    for (;;) {
      co_yield {};
      if (tok.got == sax_token::array_end)
        break;
      co_yield dd::elements_of(from_tokens<T>::parse(v.emplace_back(), tok, r));
    }
  }
};

template <typename Optional>
struct basic_optional_parser {
  using T = typename Optional::value_type;

  TGBM_GCC_WORKAROUND static tokens_consumer parse(Optional& v, sax_token& tok, dd::with_stack_resource r) {
    if (tok.got == sax_token::null) [[unlikely]]
      return {};
    return from_tokens<T>::parse(v.emplace(), tok, r);
  }
};

template <typename T>
struct from_tokens<std::optional<T>> : basic_optional_parser<std::optional<T>> {};

template <typename T>
struct from_tokens<api::optional<T>> : basic_optional_parser<api::optional<T>> {};

template <typename T>
struct from_tokens<box<T>> : basic_optional_parser<box<T>> {};

template <oneof_field_api_type T>
struct from_tokens<T> {
  static constexpr auto N = pfr_extension::tuple_size_v<T> - 1;

  static_assert(std::same_as<pfr_extension::tuple_element_t<N, T>, decltype(T::data)>);

  static tokens_consumer get_generator_field(T& t_, std::string_view key, sax_token& tok,
                                             std::bitset<N>& parsed_, dd::with_stack_resource r) {
    return pfr_extension::visit_struct_field<T, tokens_consumer, N>(
        key,
        [&]<size_t I>() {
          using Field = pfr_extension::tuple_element_t<I, T>;
          auto& field = pfr_extension::get<I>(t_);
          if (parsed_.test(I)) {
            throw parse_error(
                std::format("field `{}` already parsed", pfr_extension::element_name_v<I, std::decay_t<T>>));
          }
          parsed_.set(I);
          return from_tokens<Field>::parse(field, tok, r);
        },
        // unknown field (discriminated 'data' or unknown field which must be ignored)
        [&]() {
          return oneof_field_utils::emplace_field<T, tokens_consumer>(
              t_.data, key,
              [&]<typename Field>(Field& field) { return from_tokens<Field>::parse(field, tok, r); },
              []() -> tokens_consumer { throw parse_error("`data` already not null"); },
              [&]() -> tokens_consumer { return sax_ignore_value(tok); });
        });
  }

  static tokens_consumer parse(T& v, sax_token& tok, dd::with_stack_resource r) {
    using enum sax_token::kind_e;
    std::bitset<N> parsed_;

    tok.expect(object_begin);

    for (;;) {
      co_yield {};
      if (tok.got == object_end)
        break;
      tok.expect(key);
      std::string_view key = tok.str_m;
      co_yield {};
      co_yield dd::elements_of(get_generator_field(v, key, tok, parsed_, r));
    }
    if (!parsed_.all())
      throw parse_error("not all required fields are parsed");
    assert(tok.got == object_end);
  }
};

}  // namespace svarg::cxx_support
