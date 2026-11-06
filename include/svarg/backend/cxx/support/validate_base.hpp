#pragma once

#include <metel/meta.hpp>

#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/errors.hpp"

namespace svarg::cxx_support {

template <typename T>
using required_schemas_t = decltype([] {
  if constexpr (has_required_schemas<T>)
    return std::type_identity<typename T::svarg_required_schemas>{};
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

}  // namespace svarg::cxx_support
