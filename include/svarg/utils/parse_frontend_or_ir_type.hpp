#pragma once

#include <boost/pfr.hpp>

#include "svarg/common_types.hpp"
#include "svarg/errors.hpp"
#include "svarg/utils/type_name.hpp"
#include "svarg/utils/unescape_field_name.hpp"
#include "svarg/validate.hpp"
#include "svarg/from_json_value.hpp"

namespace svarg {

namespace noexport {

template <typename T>
struct is_optional_field : std::false_type {};
template <typename T>
struct is_optional_field<optional<T>> : std::true_type {};
template <typename T>
struct is_optional_field<std::optional<T>> : std::true_type {};

template <typename T>
constexpr bool has_field(std::string_view name) noexcept {
  return [&]<size_t... Is>(std::index_sequence<Is...>) {
    return ((unescape_field_name(boost::pfr::get_name<Is, T>()) == name) || ...);
  }(std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
}

template <typename T, size_t I>
bool parse_field(const boost::json::object& obj, T& out, error_code& ec) {
  using field_type = std::decay_t<decltype(boost::pfr::get<I>(out))>;
  constexpr std::string_view name = unescape_field_name(boost::pfr::get_name<I, T>());
  auto it = obj.find(name);
  if (it == obj.end()) {
    if (is_optional_field<field_type>::value)
      return true;
    else {
      ec = parse_error(" field `{}` in type `{}` is REQUIRED but not present", name, type_name<T>());
      return false;
    }
  }
  ec = try_from_json(it->value(), boost::pfr::get<I>(out));
  if (ec) {
    ec.reformat("/`{}`{}", name, old_message);
    return false;
  }
  return true;
}

template <front_or_ir_type T, size_t... Is>
error_code parse_all_fields(const boost::json::object& obj, T& out, std::index_sequence<Is...>) {
  error_code ec;
  (parse_field<T, Is>(obj, out, ec) && ...);
  return ec;
}

}  // namespace noexport

// TODO складывать такие поля в meta
template <front_or_ir_type>
struct allow_additional_field {
  [[nodiscard]] static bool allow(std::string_view name) noexcept {
    // https://spec.openapis.org/oas/v3.1.0#specification-extensions
    return name.starts_with("x-");
  }
};

template <typename T>
concept parsable_even_from_not_object = front_or_ir_type<T> && requires(const boost::json::value& v, T& out) {
  { T::parse_if_not_object(v, out) } -> std::same_as<error_code>;
};

template <front_or_ir_type T>
struct from_json_value<T> {
  static_assert(metel::is_decayed_v<T>);

  error_code operator()(const boost::json::value& v, T& out) {
    // TODO? можно развернуть парсинг - вместо того чтобы он зависел от количества полей в типе, он должен
    // зависеть от количества ключей в джсоне т.е. идти по ключам и маппить куда они должны попасть. И
    // проверять в конце, что все требуемые поля пришли
    const auto* obj = v.if_object();
    if (!obj) {
      if constexpr (parsable_even_from_not_object<T>)
        return T::parse_if_not_object(v, out);
      else
        return parse_error(" object expected, got `{}`", e2str(v.kind()));
    }
    for (auto&& [key, _] : *obj) {
      std::string_view k(key);
      if (!noexport::has_field<T>(k) && !allow_additional_field<T>{}.allow(k))
        return parse_error(" not allowed field `{}` in type `{}`", k, type_name<T>());
    }
    error_code ec =
        noexport::parse_all_fields(*obj, out, std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
    if (ec)
      return ec;
    ec = postparse<T>{}(out);
    if (ec)
      return ec;
    return validate<T>{}(std::as_const(out));
  }
};

}  // namespace svarg
