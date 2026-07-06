#pragma once

#include "svarg/backend/cxx/support/stream_parsing.hpp"

namespace svarg::cxx_support {

namespace noexport {

template <typename R>
constexpr bool requirement_needs_tokens_reuse() {
  // TODO для некоторых требований можно обойтись проверкой значения после парса
  // min/max/pattern/наличие каких-то полей и отсутствие других и тд
  return true;
}

// проверяет только для одного уровня требований
template <typename... Requirements>
constexpr bool requirements_needs_tokens_reuse(metel::typelist<Requirements...>) {
  return (requirement_needs_tokens_reuse<Requirements>() || ...);
}

// true для схем, которым нужно сохранять при парсинге токены для дальнейшего переиспользования
// Note: не учитывает oneof/anyof, только требования
template <typename Schema>
constexpr bool needs_tokens_reuse_for_validation =
    noexport::requirements_needs_tokens_reuse(required_schemas_t<Schema>{});

template <typename Schema, typename T>
void validate_parsed_object(const T& s) {
  static_assert(!requirement_needs_tokens_reuse<Schema>());
  static_assert(required_schemas_t<T>{}.contains(std::type_identity<Schema>{}));
  // TODO проводит валидацию по схемам не требующим заново парсить токены
  // min/max/prefixElements и так далее (они должны появится в IR в виде required schemas со
  // специальным парсингом)
}

}  // namespace noexport

template <typename T>
tokens_consumer parse_and_validate(T& v, sax_token& tok, dd::with_stack_resource r);

// pre: в 'saved' должны быть сохранены все токены одного json value
template <typename Schema, typename T>
void validate_schema(const T& result, std::span<const sax_token_value> saved, std::span<unsigned char> buf) {
  static_assert(!has_recursive_requirements<Schema>());
  if constexpr (noexport::requirement_needs_tokens_reuse<Schema>) {
    Schema schema;
    omo::stream_parser_tokens parser(
        [&](sax_token& token, dd::with_stack_resource r) { return parse_and_validate(schema, token, r); },
        buf);
    size_t eaten = std::distance(saved.begin(), parser.feed(saved, /*end=*/true));
    if (eaten != saved.size())
      throw parse_error("not all tokens consumed by validator");
    (void)schema;  // главное что не вылетело исключение
  } else {
    noexport::validate_parsed_object<Schema>(result);
  }
}

// `saved` может быть пустым, если ни одна схема не требует токенов
template <typename T>
void validate_required_schemas(const T& result, std::span<const sax_token_value> saved) {
  static_assert(!has_recursive_requirements<T>());  //  избегаем бесконечной рекурсии
  alignas(dd::coroframe_align()) unsigned char buf[512];
  // передаём все токены каждой схеме
  auto do_validate = [&]<typename... Schemas>(metel::typelist<Schemas...>) {
    (validate_schema<Schemas>(result, saved, buf), ...);
  };
  do_validate(required_schemas_t<T>{});
}

namespace noexport {

// собирает токены одного json value из tok и парсит значение из них
template <typename T>
tokens_consumer parse_and_validate_from_tokens(T& v, sax_token& tok, dd::with_stack_resource r) {
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
  validate_required_schemas<T>(v, tokens);
}

template <typename T>
tokens_consumer parse_and_validate_simple_requirements(T& v, sax_token& tok, dd::with_stack_resource r) {
  static_assert(!noexport::needs_tokens_reuse_for_validation<T> && required_schemas_t<T>::size > 0);

  co_yield dd::elements_of(from_tokens_parser(v, tok, r));
  validate_required_schemas(v, /*saved=*/{});  // не нужны токены, проверяется просто значение
}

}  // namespace noexport

// TODO ёпрст, можно же одновременно создать все генераторы и по всем пускать
template <typename T>
tokens_consumer parse_and_validate(T& v, sax_token& tok, dd::with_stack_resource r) {
  static_assert(metel::is_decayed_v<T>);

  if constexpr (noexport::needs_tokens_reuse_for_validation<T>)
    return noexport::parse_and_validate_from_tokens(v, tok, r);
  else if (!noexport::needs_tokens_reuse_for_validation<T> && required_schemas_t<T>::size > 0)
    return noexport::parse_and_validate_simple_requirements(v, tok, r);
  else  // нет требований
    return from_tokens_parser(v, tok, r);
}

}  // namespace svarg::cxx_support
