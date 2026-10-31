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

  // pre: T встречается в Types ровно один раз
  template <typename T>
  decltype(auto) get() & {
    return std::get<metel::box<T>>(static_cast<base_t&>(*this));
  }
  template <typename T>
  decltype(auto) get() && {
    return std::get<metel::box<T>>(static_cast<base_t&&>(*this));
  }
  template <typename T>
  decltype(auto) get() const& {
    return std::get<metel::box<T>>(static_cast<const base_t&>(*this));
  }

  template <typename T>
  decltype(auto) get() const&& {
    return std::get<metel::box<T>>(static_cast<const base_t&&>(*this));
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

// части обходятся по индексу: одинаковые типы в anyOf допустимы, а в tuple лежат box<T>
template <typename... Types>
struct from_json_value<cxx_support::anyof<Types...>> {
  static error_code operator()(const boost::json::value& j, cxx_support::anyof<Types...>& v) {
    size_t success = 0;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      auto parse_one = [&]<size_t I>(std::index_sequence<I>) {
        auto& part = v.template get<I>();
        if (try_from_json(j, part.emplace()).failed())
          part.reset();  // не подошедшая часть не должна остаться частично заполненной
        else
          ++success;
      };
      (parse_one(std::index_sequence<Is>{}), ...);
    }(std::index_sequence_for<Types...>{});
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
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      auto serialize_one = [&]<size_t I>(std::index_sequence<I>) {
        const auto& part = v.template get<I>();
        if (part) {
          values[count] = to_json(*part);
          ++count;
        }
      };
      (serialize_one(std::index_sequence<Is>{}), ...);
    }(std::index_sequence_for<Types...>{});
    if (count == 0) [[unlikely]]
      throw serialize_error("trying to serialize invalid anyof: its empty");
    for (size_t i = 1; i < count; ++i)
      make_json_union(values[0], std::move(values[i])).rethrow_if_failed();
    return std::move(values[0]);
  }
};

}  // namespace svarg
