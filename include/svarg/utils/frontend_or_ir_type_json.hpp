#pragma once

#include <boost/pfr.hpp>

#include "svarg/common_types.hpp"
#include "svarg/errors.hpp"
#include "svarg/utils/type_name.hpp"
#include "svarg/utils/unescape_field_name.hpp"
#include "svarg/validate.hpp"
#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"
#include "svarg/concepts.hpp"
#include "svarg/utils/svarg_assert.hpp"
#include "svarg/utils/indexes.hpp"

namespace svarg {

namespace noexport {

template <typename T>
struct svarg_is_optional_field : std::false_type {};
template <typename T>
struct svarg_is_optional_field<optional<T>> : std::true_type {};
template <typename T>
struct svarg_is_optional_field<std::optional<T>> : std::true_type {};

template <typename T>
constexpr bool svarg_object_has_field(std::string_view name) noexcept {
  return APPLY_INDEXES(boost::pfr::tuple_size_v<T>) {
    return ((unescape_field_name(boost::pfr::get_name<Is, T>()) == name) || ...);
  };
}

template <typename T, size_t I>
bool parse_frontorir_field(const boost::json::object& obj, T& out, error_code& ec) {
  using field_type = std::decay_t<decltype(boost::pfr::get<I>(out))>;
  constexpr std::string_view name = unescape_field_name(boost::pfr::get_name<I, T>());
  if constexpr (is_keyfield(name)) {
    return true;
  }
  auto it = obj.find(name);
  if (it == obj.end()) {
    if (svarg_is_optional_field<field_type>::value)
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

}  // namespace noexport

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
    // этот код парсит только спеку, перфоманс здесь не критичен
    const auto* obj = v.if_object();
    if (!obj) {
      if constexpr (parsable_even_from_not_object<T>)
        return T::parse_if_not_object(v, out);
      else
        return parse_error(" object expected, got `{}`", e2str(v.kind()));
    }
    for (auto&& [key, val] : *obj) {
      std::string_view k(key);
      if (!noexport::svarg_object_has_field<T>(k)) {
        if (!allow_additional_field<T>{}.allow(k))
          return parse_error(" not allowed field `{}` in type `{}`", k, type_name<T>());
        else {
          if constexpr (allows_meta_fields<std::decay_t<T>>) {
            if (is_meta_key(std::string_view(key)))
              out.SVARG_META_KEYFIELD[std::string(key)] = Any(val);
          }
        }
      }
    }
    error_code ec = APPLY_INDEXES(boost::pfr::tuple_size_v<T>) {
      error_code ec;
      (noexport::parse_frontorir_field<T, Is>(*obj, out, ec) && ...);
      return ec;
    };
    if (ec)
      return ec;
    ec = postparse<T>{}(out);
    if (ec)
      return ec;
    return validate<T>{}(std::as_const(out));
  }
};

// сериализация

namespace noexport {

template <front_or_ir_type T, size_t I>
void frontent_type_serialize_field(const T& in, boost::json::object& out) {
  constexpr std::string_view raw_name = boost::pfr::get_name<I, T>();
  constexpr std::string_view name = unescape_field_name(raw_name);

  // пропускаем внутренние keyfield поля (они не часть JSON-схемы)
  if constexpr (is_keyfield(raw_name))
    return;

  const auto& field = boost::pfr::get<I>(in);

  if constexpr (requires { field.has_value(); }) {
    if (!field.has_value())
      return;
    out.emplace(name, to_json(*field));
  } else {
    out.emplace(name, to_json(field));
  }
}

}  // namespace noexport

template <front_or_ir_type T>
struct to_json_value<T> {
  static_assert(std::is_same_v<T, std::decay_t<T>>);

  boost::json::value operator()(const T& in) const {
    // костыль для schema_object
    if constexpr (requires { in._keyfield_boolean_schema; }) {
      if (in._keyfield_boolean_schema.has_value())
        return *in._keyfield_boolean_schema;
    }
    boost::json::object out;
    APPLY_INDEXES(boost::pfr::tuple_size_v<T>) {
      (noexport::frontent_type_serialize_field<T, Is>(in, out), ...);
    };
    if constexpr (allows_meta_fields<std::decay_t<T>>) {
      for (auto& [key, value] : in.SVARG_META_KEYFIELD) {
        SVARG_ASSERT(is_meta_key(std::string_view(key)));
        out[key] = value.value;
      }
    }
    return out;
  }
};

}  // namespace svarg
