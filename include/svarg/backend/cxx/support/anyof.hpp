#pragma once

#include <tuple>

#include <metel/box.hpp>

#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"
#include "svarg/backend/cxx/support/allof.hpp"

namespace svarg::cxx_support {

template <typename... Types>
struct anyof : private std::tuple<metel::box<Types>...> {
  using base_t = std::tuple<metel::box<Types>...>;

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
struct tuple_size<::svarg::cxx_support::anyof<Types...>> : std::tuple_size<std::tuple<Types...>> {};

template <size_t I, typename... Types>
struct tuple_element<I, ::svarg::cxx_support::anyof<Types...>>
    : std::type_identity<::metel::box<std::tuple_element_t<I, std::tuple<Types...>>>> {};

}  // namespace std

namespace svarg {

template <typename... Types>
struct from_json_value<cxx_support::anyof<Types...>> {
  static error_code operator()(const boost::json::value& j, cxx_support::anyof<Types...>& v) {
    size_t success = 0;
    ((success += !try_from_json(j, v.template get<Types>()).failed()), ...);
    if (success == 0)
      return parse_error("anyOf failed to parse any schema in list");
    return {};
  }
};

template <typename... Types>
struct to_json_value<cxx_support::anyof<Types...>> {
  static boost::json::value operator()(const cxx_support::anyof<Types...>& v) {
    size_t count = 0;
    boost::json::value values[sizeof...(Types)];
    auto serialize_one = [&]<typename T>(std::type_identity<T>) {
      const metel::box<T>& part = v.template get<T>();
      if (part) {
        values[count] = to_json(v);
        ++count;
      }
    };
    (serialize_one(std::type_identity<Types>{}), ...);
    if (count == 0) [[unlikely]]
      throw serialize_error("trying to serialize invalid anyof: its empty");
    for (size_t i = 1; i < count; ++i)
      make_json_union(values[0], std::move(values[i])).rethrow_if_failed();
    return std::move(values[0]);
  }
};

}  // namespace svarg
