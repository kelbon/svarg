#pragma once

#include "svarg/backend/cxx/support/stream_parsing.hpp"
#include "svarg/backend/cxx/support/validate_base.hpp"
#include "svarg/backend/cxx/support/constraints.hpp"

#include <array>
#include <exception>
#include <tuple>

namespace svarg::cxx_support {

template <typename T>
tokens_consumer from_tokens_subobject(T& v, sax_token& tok, dd::with_stack_resource r);

namespace noexport {

// значение и требования к нему (required_schemas_t<T>) разбираются одновременно по одним и тем же токенам:
// каждый токен отдаётся основному разбору и каждой схеме-требованию. Ошибки - как в DOM: сначала разбор
// значения (сразу), потом требования по порядку, потом ограничения, даже если требование нарушено раньше по
// тексту. У каждого разбора свой стек-ресурс: общий освобождает память только в обратном порядке, а разборы
// идут вперемешку
template <typename T, typename... Schemas>
tokens_consumer from_tokens_with_requirements(T& v, sax_token& tok, metel::typelist<Schemas...>,
                                              dd::with_stack_resource) {
  static_assert(!has_recursive_requirements<T>());  //  избегаем бесконечной рекурсии
  constexpr size_t n = sizeof...(Schemas);
  std::array<dd::stack_resource, n + 1> resources;
  std::tuple<Schemas...> schemas;  // значения требований не нужны, важно лишь что разобрались
  std::array<std::exception_ptr, n> errors;
  std::array<tokens_consumer, n + 1> parsers;  // [0] - значение, [i + 1] - требование i

  // вызов разбора (и для скаляра сразу разбор первого токена) или продвижение на следующий токен
  auto requirement_step = [&](size_t i, auto&& step) {
    if (errors[i])
      return;
    try {
      step();
    } catch (...) {
      errors[i] = std::current_exception();
    }
  };
  parsers[0] = from_tokens_parser(v, tok, dd::with_stack_resource(resources[0]));
  [&]<size_t... I>(std::index_sequence<I...>) {
    (requirement_step(I,
                      [&] {
                        parsers[I + 1] =
                            from_tokens_subobject(std::get<I>(schemas), tok, dd::with_stack_resource(resources[I + 1]));
                      }),
     ...);
  }(std::index_sequence_for<Schemas...>{});

  for (;;) {
    if (!parsers[0].empty())
      ++parsers[0].cur_iterator();
    for (size_t i = 0; i < n; ++i) {
      if (!parsers[i + 1].empty())
        requirement_step(i, [&] { ++parsers[i + 1].cur_iterator(); });
    }
    if (parsers[0].empty())  // значение кончилось
      break;
    co_yield {};
  }
  for (size_t i = 0; i < n; ++i) {
    if (errors[i])
      std::rethrow_exception(errors[i]);
    if (!parsers[i + 1].empty())
      throw parse_error("when parsing schema `{}`: value ended, but its requirement did not", type_name<T>());
  }
  if constexpr (has_constraints<T>)
    check_constraints(v).rethrow_if_failed();
}

template <typename T>
tokens_consumer from_tokens_with_constraints(T& v, sax_token& tok, dd::with_stack_resource r) {
  co_yield dd::elements_of(from_tokens_parser(v, tok, r));
  check_constraints(v).rethrow_if_failed();
}

}  // namespace noexport

template <typename T>
tokens_consumer from_tokens_subobject(T& v, sax_token& tok, dd::with_stack_resource r) {
  static_assert(metel::is_decayed_v<T>);

  if constexpr (required_schemas_t<T>::size > 0)
    return noexport::from_tokens_with_requirements(v, tok, required_schemas_t<T>{}, r);
  else if constexpr (has_constraints<T>)
    return noexport::from_tokens_with_constraints(v, tok, r);
  else  // нет требований
    return from_tokens_parser(v, tok, r);
}

}  // namespace svarg::cxx_support
