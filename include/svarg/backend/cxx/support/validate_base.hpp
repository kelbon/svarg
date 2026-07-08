#pragma once

#include <metel/meta.hpp>

#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/errors.hpp"

namespace svarg::cxx_support {

template <typename T>
using required_schemas_t = decltype([] {
  if constexpr (has_required_schemas<T>)
    return std::type_identity<typename T::required_schemas>{};
  else
    return std::type_identity<metel::typelist<>>{};
}())::type;

namespace noexport {

template <typename T, typename... Ts>
constexpr auto typelist_rm(metel::typelist<Ts...> l) {
  if constexpr (!l.contains(std::type_identity<T>{}))
    return l;
  else
    return (metel::typelist<>{} + ... +
            std::conditional_t<std::is_same_v<Ts, T>, metel::typelist<>, metel::typelist<Ts>>{});
}

// возвращает ВСЕ требуемые схемы, включая требуемые схемы требуемых схем.
// Без дубликатов и обрабатывая возможную рекурсию
template <typename T, typename... Types>
consteval auto all_required_schemas(metel::typelist<Types...> collected_) {
  // избегаем self reference
  if constexpr (collected_.contains(std::type_identity<T>{}))
    return metel::typelist<>{};
  else {
    constexpr metel::typelist collected = collected_ | metel::typelist<T>{};
    metel::typelist R = required_schemas_t<T>{};
    return [=]<typename... Schemas>(metel::typelist<Schemas...>) {
      return (R | ... | all_required_schemas<Schemas>(collected | R));
    }(R);
  }
}

}  // namespace noexport

template <typename Requirement, typename Schema>
constexpr bool is_recursive_requirement() {
  std::type_identity<Schema> t;
  return noexport::all_required_schemas<Requirement>(metel::typelist<>{}).contains(t);
}

template <typename Schema>
constexpr bool has_recursive_requirements() {
  // само по себе проверяет только первый уровень, но если эта проверка стоит на каждом уровне, то
  // рекурсивность отловится
  return []<typename... Requirements>(metel::typelist<Requirements...>) {
    return (is_recursive_requirement<Requirements, Schema>() || ...);
  }(required_schemas_t<Schema>{});
}

template <typename T>
using all_required_schemas_t = decltype(noexport::typelist_rm<std::decay_t<T>>(
    noexport::all_required_schemas<std::decay_t<T>>(metel::typelist<>{})));

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
[[nodiscard]] error_code validate_parsed_object(const T& s) {
  static_assert(!requirement_needs_tokens_reuse<Schema>());
  static_assert(required_schemas_t<T>{}.contains(std::type_identity<Schema>{}));
  // TODO проводит валидацию по схемам не требующим заново парсить токены
  // min/max/prefixElements и так далее (они должны появится в IR в виде required schemas со
  // специальным парсингом)
  return {};
}

}  // namespace noexport

}  // namespace svarg::cxx_support
