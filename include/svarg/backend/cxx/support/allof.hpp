#pragma once

#include <tuple>

namespace svarg::cxx_support {

template <typename... Types>
struct allof : private std::tuple<Types...> {
  using base_t = std::tuple<Types...>;

  template <size_t I>
  decltype(auto) get() & {
    return std::get<I>(static_cast<base_t&>(*this));
  }
  template <size_t I>
  decltype(auto) get() && {
    return std::get<I>(static_cast<base_t&&>(*this));
  }
  template <size_t I>
  decltype(auto) get() const& {
    return std::get<I>(static_cast<const base_t&>(*this));
  }

  template <size_t I>
  decltype(auto) get() const&& {
    return std::get<I>(static_cast<const base_t&&>(*this));
  }

  template <typename T>
  decltype(auto) get() & {
    return std::get<T>(static_cast<base_t&>(*this));
  }
  template <typename T>
  decltype(auto) get() && {
    return std::get<T>(static_cast<base_t&&>(*this));
  }
  template <typename T>
  decltype(auto) get() const& {
    return std::get<T>(static_cast<const base_t&>(*this));
  }

  template <typename T>
  decltype(auto) get() const&& {
    return std::get<T>(static_cast<const base_t&&>(*this));
  }

  using base_t::base_t;
  using base_t::operator=;
};

}  // namespace svarg::cxx_support

namespace std {

template <typename... Types>
struct tuple_size<::svarg::cxx_support::allof<Types...>> : std::tuple_size<std::tuple<Types...>> {};

template <size_t I, typename... Types>
struct tuple_element<I, ::svarg::cxx_support::allof<Types...>>
    : std::type_identity<std::tuple_element_t<I, std::tuple<Types...>>> {};

}  // namespace std
