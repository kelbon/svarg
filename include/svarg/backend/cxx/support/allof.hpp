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
    auto parse_one = [&]<typename T>(std::type_identity<T>) -> bool {
      ec = try_from_json(j, v.template get<T>());
      return !ec.failed();
    };
    (parse_one(std::type_identity<Types>{}) && ...);
    return ec;
  }
};

// результат возвращается в `into`
[[nodiscard]] inline error_code make_json_union(boost::json::value& into, boost::json::value&& from) {
  if (into.kind() != from.kind()) {
    return serialize_error("cannot unite json values, kind 1: {}, kind2: {}", e2str(into.kind()),
                           e2str(from.kind()));
  }
  switch (into.kind()) {
    case boost::json::kind::null:
    case boost::json::kind::bool_:
    case boost::json::kind::int64:
    case boost::json::kind::uint64:
    case boost::json::kind::double_:
    case boost::json::kind::string:
    case boost::json::kind::array:
      if (into != from)
        return serialize_error("cannot unite json value `{}` with value `{}`", boost::json::serialize(into),
                               boost::json::serialize(from));
      return {};
    case boost::json::kind::object: {
      auto& into_obj = into.as_object();
      auto& from_obj = from.as_object();
      for (auto&& [k, v] : from_obj) {
        auto* value = into_obj.if_contains(k);
        if (value && *value != v) {
          return serialize_error(
              "cannot unite json values: value of key `{}` mismatch. Value `{}` is not equal to `{}`",
              std::string_view(k), boost::json::serialize(*value), boost::json::serialize(v));
        } else {
          into_obj[k] = std::move(v);
        }
      }
    }
      return {};
  }
  metel::unreachable();
}

template <typename... Types>
struct to_json_value<cxx_support::allof<Types...>> {
  static boost::json::value operator()(const cxx_support::allof<Types...>& v) {
    static_assert(sizeof...(Types) > 1);  // IR должен был отработать
    boost::json::value values[] = {to_json(v.template get<Types>())...};
    for (size_t i = 1; i < sizeof...(Types); ++i)
      make_json_union(values[0], std::move(values[i])).rethrow_if_failed();
    return std::move(values[0]);
  }
};

}  // namespace svarg
