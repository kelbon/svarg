#pragma once

#include "svarg/backend/cxx/support/stream_parsing.hpp"
#include "svarg/backend/cxx/support/validate_base.hpp"

namespace svarg::cxx_support {

template <typename T>
tokens_consumer from_tokens_subobject(T& v, sax_token& tok, dd::with_stack_resource r);

// pre: в 'saved' должны быть сохранены все токены одного json value
template <typename Schema, typename T>
[[nodiscard]] error_code validate_schema(std::span<const sax_token_value> saved, const T& result,
                                         std::span<unsigned char> buf) {
  static_assert(!has_recursive_requirements<Schema>());
  if constexpr (noexport::requirement_needs_tokens_reuse<Schema>) {
    Schema schema;
    omo::stream_parser_tokens parser(
        [&](sax_token& token, dd::with_stack_resource r) { return from_tokens_subobject(schema, token, r); },
        buf);

    size_t eaten;
    try {
      eaten = std::distance(saved.begin(), parser.feed(saved, /*end=*/true));
    } catch (parse_error& e) {
      return std::move(e);
    } catch (validate_error& e) {
      return std::move(e);
    }
    if (eaten != saved.size())
      return parse_error("not all tokens consumed by validator");
    (void)schema;  // главное что не вылетело исключение
    return {};
  } else {
    return noexport::validate_parsed_object<Schema>(result);
  }
}

// `saved` может быть пустым, если ни одна схема не требует токенов
template <typename T>
error_code validate_required_schemas(std::span<const sax_token_value> saved, const T& result) {
  static_assert(!has_recursive_requirements<T>());  //  избегаем бесконечной рекурсии
  alignas(dd::coroframe_align()) unsigned char buf[512];
  // передаём все токены каждой схеме
  error_code ec;

  auto validate_one = [&]<typename S>(metel::typelist<S>) {
    ec = validate_schema<S>(saved, result, buf);
    return !ec.failed();
  };
  auto do_validate = [&]<typename... Schemas>(metel::typelist<Schemas...>) {
    (validate_one(metel::typelist<Schemas>{}) && ...);
  };
  do_validate(required_schemas_t<T>{});

  return ec;
}

namespace noexport {

// собирает токены одного json value из tok и парсит значение из них
template <typename T>
tokens_consumer from_tokens_subobject_and_required_schemas(T& v, sax_token& tok, dd::with_stack_resource r) {
  // TODO ёпрст, можно же одновременно создать все генераторы и по всем пускать
  // парсит подобъект и валидирует его
  std::vector<sax_token_value> tokens;
  co_yield dd::elements_of(
      omo::collect_json_value_tokens(tok, [&](sax_token& tok) { tokens.emplace_back(tok); }, r));

  omo::stream_parser_tokens tp(
      [&](sax_token& t, dd::with_stack_resource) { return from_tokens_parser(v, t, r); });
  size_t eaten = std::distance(tokens.begin(), tp.feed(tokens, /*end=*/true));
  if (eaten != tokens.size()) {
    throw parse_error("when parsing schema `{}`: parser ends, but not all tokens are eaten", type_name<T>());
  }
  // переиспользуем токены для валидации схем
  validate_required_schemas<T>(tokens, v);
}

template <typename T>
tokens_consumer from_tokens_subobject_simple_requirements(T& v, sax_token& tok, dd::with_stack_resource r) {
  static_assert(!noexport::needs_tokens_reuse_for_validation<T> && required_schemas_t<T>::size > 0);

  co_yield dd::elements_of(from_tokens_parser(v, tok, r));
  validate_required_schemas(/*saved=*/{}, v);  // не нужны токены, проверяется просто значение
}

}  // namespace noexport

template <typename T>
tokens_consumer from_tokens_subobject(T& v, sax_token& tok, dd::with_stack_resource r) {
  static_assert(metel::is_decayed_v<T>);

  if constexpr (noexport::needs_tokens_reuse_for_validation<T>)
    return noexport::from_tokens_subobject_and_required_schemas(v, tok, r);
  else if (!noexport::needs_tokens_reuse_for_validation<T> && required_schemas_t<T>::size > 0)
    return noexport::from_tokens_subobject_simple_requirements(v, tok, r);
  else  // нет требований
    return from_tokens_parser(v, tok, r);
}

}  // namespace svarg::cxx_support
