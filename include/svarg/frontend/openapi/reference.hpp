#pragma once

#include <string>
#include <variant>

#include "svarg/common_types.hpp"
#include "svarg/serialize.hpp"
#include "svarg/utils/parse_ref.hpp"

#include "svarg/utils/parse_frontend_or_ir_type.hpp"

namespace svarg {

// TODO support formats
// int32, int64, float, double, password, ... и тд может быть определено пользователем (data ещё, но видимо
// убрали в новой версии)

struct reference_object {
  SVARG_FRONTEND_TYPE;

  string ref_;  // MUST be URI, в json как $ref
  optional<string> summary;
  optional<string> description;
  // какие-то дополнительные поля переопределяющие то что лежит по ссылке
  string_map<Any> _keyfield_override_fields;

  bool operator==(const reference_object& r) const {
    if (ref_ == r.ref_) {
      return summary == r.summary && description == r.description &&
             _keyfield_override_fields == r._keyfield_override_fields;
    }
    if (summary != r.summary || description != r.description ||
        _keyfield_override_fields != r._keyfield_override_fields)
      return false;
    // ref_ и r.ref_ могли не совпасть так как в одном случае json pointer экранирован, а в другом нет
    return parse_ref(ref_).json_ptr == parse_ref(r.ref_).json_ptr;
  }
};

template <typename T>
struct reference_or : private std::variant<reference_object, T> {
  // не отмечено, так как вспомогательный тип SVARG_FRONTEND_TYPE;

  static_assert(!std::is_same_v<T, reference_object>);

 private:
  using base_t = std::variant<reference_object, T>;

 public:
  using base_t::base_t;
  using base_t::operator=;

  reference_object* get_if_ref() noexcept {
    return std::get_if<reference_object>(static_cast<base_t*>(this));
  }
  const reference_object* get_if_ref() const noexcept {
    return std::get_if<reference_object>(static_cast<const base_t*>(this));
  }

  T* get_if_value() noexcept {
    return std::get_if<T>(static_cast<base_t*>(this));
  }
  const T* get_if_value() const noexcept {
    return std::get_if<T>(static_cast<const base_t*>(this));
  }

  template <typename... Args>
  T& emplace_value(Args&&... args) {
    return static_cast<base_t&>(*this).template emplace<T>(std::forward<Args>(args)...);
  }

  template <typename... Args>
  reference_object& emplace_ref(Args&&... args) {
    return static_cast<base_t&>(*this).template emplace<reference_object>(std::forward<Args>(args)...);
  }

  template <typename Vtor>
  decltype(auto) visit(Vtor&& vtor) {
    return std::visit(std::forward<Vtor>(vtor), static_cast<base_t&>(*this));
  }
  template <typename Vtor>
  decltype(auto) visit(Vtor&& vtor) const {
    return std::visit(std::forward<Vtor>(vtor), static_cast<const base_t&>(*this));
  }
};

template <>
struct from_json_value<reference_object> {
  error_code operator()(const boost::json::value& v, reference_object& out) {
    const auto* obj = v.if_object();

    if (!obj)
      return parse_error(" object expected, got `{}`", e2str(v.kind()));
    auto refit = obj->find("$ref");
    if (refit == obj->end())
      return parse_error(" field `$ref` in reference object is REQUIRED but not present");
    out.ref_ = refit->value().as_string();

    if (auto it = obj->find("summary"); it != obj->end())
      out.summary = std::string(it->value().as_string());

    if (auto it = obj->find("description"); it != obj->end())
      out.description = std::string(it->value().as_string());

    for (const auto& [key, val] : *obj) {
      if (key != "$ref" && key != "summary" && key != "description")
        out._keyfield_override_fields[std::string(key)] = Any{val};
    }
    return {};
  }
};

template <typename T>
struct from_json_value<reference_or<T>> {
  error_code operator()(const boost::json::value& v, reference_or<T>& out) {
    const auto* obj = v.if_object();
    if (!obj) {
      if constexpr (parsable_even_from_not_object<T>)
        return T::parse_if_not_object(v, out.emplace_value());
      else
        return parse_error(" object expected, got `{}`", e2str(v.kind()));
    }
    if (obj->contains("$ref"))
      return try_from_json<reference_object>(v, out.emplace_ref());
    return try_from_json<T>(v, out.emplace_value());
  }
};

template <typename T>
struct to_json_value<reference_or<T>> {
  boost::json::value operator()(const reference_or<T>& v) const {
    return v.visit([](const auto& x) -> boost::json::value { return to_json(x); });
  }
};

}  // namespace svarg
