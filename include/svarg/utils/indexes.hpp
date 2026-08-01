#pragma once

#include <utility>

#include <boost/pfr.hpp>

namespace svarg::noexport {

template <size_t Max>
struct for_all_indexes_impl {
  template <typename F>
  constexpr decltype(auto) operator*(F&& fn) {
    return std::forward<F>(fn)(std::make_index_sequence<Max>{});
  }

  template <typename F>
  constexpr void operator/(F&& fn) {
    auto helper = [&]<size_t... Is>(std::index_sequence<Is...>) {
      (fn.template operator()<Is>(), ...);  //
    };
    return helper(std::make_index_sequence<Max>{});
  }
};

// применяет функцию ко всем индексам сразу [0, INDEX_MAX) (не включая верхнюю границу)
// функция может использовать пак Is... из индексов
#define APPLY_INDEXES(INDEX_MAX)                                                    \
  ::svarg::noexport::for_all_indexes_impl<(INDEX_MAX)>{}* [&]<::std::size_t... Is>( \
      ::std::index_sequence<Is...>)

// применяет функцию ко всем индексам по очереди [0, INDEX_MAX) (не включая верхнюю границу)
// функция может использовать константу I
#define FOR_EACH_INDEX(INDEX_MAX) \
  ::svarg::noexport::for_all_indexes_impl<(INDEX_MAX)>{} / [&]<::std::size_t I>()

}  // namespace svarg::noexport
