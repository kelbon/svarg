#pragma once

#include <string>
#include <variant>

#include "svarg/common_types.hpp"
#include "svarg/serialize.hpp"

namespace svarg {

// data types

// integer, string, number

// formats
// int32, int64, float, double, password, ... и тд может быть определено пользователем (data ещё, но видимо
// убрали в новой версии)

struct reference_object {
  string ref_;  // MUST be URI, в json как $ref
  optional<string> summary;
  optional<string> description;
  // какие-то дополнительные поля переопределяющие то что лежит по ссылке
  string_map<Any> _keyfield_override_fields;
};

template <typename T>
using reference_or = std::variant<reference_object, T>;

template <>
struct parse<reference_object> {
  void operator()(boost::json::value const& v, reference_object& out) {
    auto const* obj = v.if_object();

    if (!obj)
      throw parse_error(" object expected, got `{}`", e2str(v.kind()));
    auto refit = obj->find("$ref");
    if (refit == obj->end())
      throw parse_error(" field `$ref` in reference object is REQUIRED but not present");
    out.ref_ = refit->value().as_string();

    if (auto it = obj->find("summary"); it != obj->end())
      out.summary = std::string(it->value().as_string());

    if (auto it = obj->find("description"); it != obj->end())
      out.description = std::string(it->value().as_string());

    for (auto const& [key, val] : *obj) {
      if (key != "$ref" && key != "summary" && key != "description")
        out._keyfield_override_fields[std::string(key)] = Any{val};
    }
  }
};

template <typename T>
struct parse<reference_or<T>> {
  void operator()(boost::json::value const& v, reference_or<T>& out) {
    auto const* obj = v.if_object();
    if (!obj) {
      if constexpr (parsable_even_from_not_object<T>) {
        out = T::parse_if_not_object(v);
        return;
      }
      throw parse_error(" object expected, got `{}`", e2str(v.kind()));
    }
    if (obj->contains("$ref")) {
      reference_object ref{};
      parse<reference_object>{}(v, ref);
      out = std::move(ref);
    } else {
      T val{};
      parse<T>{}(v, val);
      out = std::move(val);
    }
  }
};

template <typename T>
struct serialize<reference_or<T>> {
  boost::json::value operator()(reference_or<T> const& v) {
    return std::visit(
        [](auto const& x) -> boost::json::value {
          return to_json(x);  //
        },
        v);
  }
};

}  // namespace svarg
