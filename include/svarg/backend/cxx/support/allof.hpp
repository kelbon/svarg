#pragma once

#include <span>
#include <tuple>

#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"

namespace svarg::cxx_support {

template <typename... Types>
struct allof : private std::tuple<Types...> {
  static_assert(metel::is_unique_types<Types...>());

  using base_t = std::tuple<Types...>;

  using base_t::base_t;
  using base_t::operator=;

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
};

}  // namespace svarg::cxx_support

namespace std {

template <typename... Types>
struct tuple_size<::svarg::cxx_support::allof<Types...>> : std::tuple_size<std::tuple<Types...>> {};

template <size_t I, typename... Types>
struct tuple_element<I, ::svarg::cxx_support::allof<Types...>>
    : std::type_identity<std::tuple_element_t<I, std::tuple<Types...>>> {};

}  // namespace std

namespace svarg {

template <typename... Types>
struct from_json_value<cxx_support::allof<Types...>> {
  static error_code operator()(const boost::json::value& j, cxx_support::allof<Types...>& v) {
    error_code ec;
    auto parse_one = [&]<typename T>(std::type_identity_t<T>) -> bool {
      ec = try_from_json(j, v.template get<T>());
      return !ec.failed();
    };
    (parse_one(std::type_identity<Types>{}) && ...);
    return ec;
  }
};

template <typename... Types>
struct to_json_value<cxx_support::allof<Types...>> {
  // сериализация для allof запрещена, т.к. он должен появляться только в требованиях
  // непонятно как сериализовать allof<anyof<A, B>, C> и тд
  // все allof полностью из объектов или фундаменталов преобразуются в IR
  static boost::json::value operator()(const cxx_support::allof<Types...>& v) = delete;
};

}  // namespace svarg
