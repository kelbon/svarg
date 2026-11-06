#pragma once

#include "svarg/backend/cxx/support/stream_parsing.hpp"
#include "svarg/backend/cxx/support/validate_base.hpp"
#include "svarg/backend/cxx/support/constraints.hpp"

#include <array>
#include <exception>
#include <span>
#include <tuple>

namespace svarg::cxx_support {

template <typename T>
tokens_consumer from_tokens_subobject(T& v, sax_token& tok, dd::with_stack_resource r);

namespace noexport {

// пропускает одно значение
inline tokens_consumer skip_json_value(sax_token& tok, dd::with_stack_resource r) {
  return omo::collect_json_value_tokens(tok, [](sax_token&) {}, r);
}

// разборы одного значения в разные значения одновременно: альтернативы oneOf/anyOf, части allOf, not,
// требования. У каждого свой стек-ресурс (пустой): общий освобождает память только в обратном порядке, а
// разборы идут вперемешку
template <size_t N>
struct parallel_parsers {
  std::array<dd::stack_resource, N> resources;
  std::array<std::exception_ptr, N> errors;  // ошибка разбора i, после неё он не продолжается
  std::array<tokens_consumer, N> parsers;

  // начинает разбор i с первого токена значения (скаляр разбирается сразу)
  template <typename T>
  void start(size_t i, T& out, sax_token& tok) {
    try {
      parsers[i] = from_tokens_subobject(out, tok, dd::with_stack_resource(resources[i]));
    } catch (...) {
      errors[i] = std::current_exception();
    }
  }

  [[nodiscard]] bool parsed(size_t i) const noexcept {
    return !errors[i];
  }

  void rethrow_first_error() const {
    for (const std::exception_ptr& e : errors)
      if (e)
        std::rethrow_exception(e);
  }
};

void feed_in_parallel_step(std::span<tokens_consumer> parsers, std::span<std::exception_ptr> errors);
void feed_in_parallel_end(std::span<tokens_consumer> parsers, std::span<std::exception_ptr> errors);

// каждый токен значения отдаётся `main` и всем `parsers`. Ошибка main - сразу ошибка, ошибки остальных
// запоминаются. Значение кончается вместе с main, не закончивший к этому моменту разбор получает ошибку
// pre: все созданы на первом токене значения
// Кадр - не в стек-ресурсе: main может разбираться в ресурсе родителя, и его вложенные кадры выделяются поверх
// этого. При ошибке main они ещё живы, когда этот кадр уже уничтожается
inline tokens_consumer feed_in_parallel(tokens_consumer& main, std::span<tokens_consumer> parsers,
                                        std::span<std::exception_ptr> errors) {
  for (;;) {
    if (!main.empty())
      ++main.cur_iterator();
    feed_in_parallel_step(parsers, errors);
    if (main.empty())  // значение кончилось
      break;
    co_yield {};
  }
  feed_in_parallel_end(parsers, errors);
}

inline void feed_in_parallel_step(std::span<tokens_consumer> parsers, std::span<std::exception_ptr> errors) {
  for (size_t i = 0; i < parsers.size(); ++i) {
    if (errors[i] || parsers[i].empty())
      continue;
    try {
      ++parsers[i].cur_iterator();
    } catch (...) {
      errors[i] = std::current_exception();
    }
  }
}

inline void feed_in_parallel_end(std::span<tokens_consumer> parsers, std::span<std::exception_ptr> errors) {
  for (size_t i = 0; i < parsers.size(); ++i) {
    if (!errors[i] && !parsers[i].empty())
      errors[i] = std::make_exception_ptr(parse_error("value ended, but its parser did not"));
  }
}

// значение и требования к нему (required_schemas_t<T>) разбираются одновременно по одним и тем же токенам.
// Ошибки - как в DOM: сначала разбор значения (сразу), потом требования по порядку, потом ограничения, даже если
// требование нарушено раньше по тексту. Значение разбирается в стек-ресурсе родителя: кроме него туда никто не
// пишет, пока идёт разбор
template <typename T, typename... Schemas>
tokens_consumer from_tokens_with_requirements(T& v, sax_token& tok, metel::typelist<Schemas...>,
                                              dd::with_stack_resource r) {
  static_assert(!has_recursive_requirements<T>());  //  избегаем бесконечной рекурсии
  std::tuple<Schemas...> schemas;  // значения требований не нужны, важно лишь что разобрались
  parallel_parsers<sizeof...(Schemas)> requirements;
  tokens_consumer main = from_tokens_parser(v, tok, r);
  [&]<size_t... I>(std::index_sequence<I...>) {
    (requirements.start(I, std::get<I>(schemas), tok), ...);
  }(std::index_sequence_for<Schemas...>{});
  co_yield dd::elements_of(feed_in_parallel(main, requirements.parsers, requirements.errors));
  requirements.rethrow_first_error();
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
