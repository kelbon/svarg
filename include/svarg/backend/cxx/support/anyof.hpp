#pragma once

#include <tuple>

#include <metel/box.hpp>

#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"

namespace svarg::cxx_support {

template <typename... Types>
struct anyof : private std::tuple<metel::box<Types>...> {
  using base_t = std::tuple<metel::box<Types>...>;

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
struct tuple_size<::svarg::cxx_support::anyof<Types...>> : std::tuple_size<std::tuple<Types...>> {};

template <size_t I, typename... Types>
struct tuple_element<I, ::svarg::cxx_support::anyof<Types...>>
    : std::type_identity<::metel::box<std::tuple_element_t<I, std::tuple<Types...>>>> {};

}  // namespace std

namespace svarg {

template <typename... Types>
struct from_json_value<cxx_support::anyof<Types...>> {
  static error_code operator()(const boost::json::value& j, cxx_support::anyof<Types...>& v) {
    ;  // TODO;
  }
};

template <typename... Types>
struct to_json_value<cxx_support::anyof<Types...>> {
  static boost::json::value operator()(const cxx_support::anyof<Types...>& v) {
    ;  // TODO;
  }
};

}  // namespace svarg
