#pragma once

// этот файл - часть openapi.hpp

#include <set>
#include "svarg/frontend/openapi/openapi_object.hpp"

namespace svarg {

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-2
// "The `identifier` field is mutually exclusive of the `url` field."
template <>
struct validate<license_object> {
  void operator()(license_object const& v) {
    if (v.identifier && v.url)
      throw validate_error("license_object: `identifier` and `url` are mutually exclusive");
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-15
// "The `value` field and `externalValue` field are mutually exclusive."
template <>
struct validate<example_object> {
  void operator()(example_object const& v) {
    if (v.value && v.externalValue)
      throw validate_error("example_object: `value` and `externalValue` are mutually exclusive");
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-11
// "The `example` field is mutually exclusive of the `examples` field."
template <>
struct validate<media_type_object> {
  void operator()(media_type_object const& obj) {
    if (obj.example && obj.examples)
      throw validate_error("media_type_object: `example` and `examples` are mutually exclusive");
    // https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#media-type-object
    if (obj.encoding && !obj.encoding->empty() && !obj.schema)
      throw validate_error(
          "media_type_object: all keys from `encoding` must be in `schema`, but `schema` not setted");
  }
};

// https://spec.openapis.org/oas/v3.1.0#header-object
// Header Object follows Parameter Object rules (same fixed fields):
// "The `example` field is mutually exclusive of the `examples` field."
// "A parameter MUST contain either a `schema` property, or a `content` property, but not both."
template <>
struct validate<header_object> {
  void operator()(header_object const& v) {
    if (v.example && v.examples)
      throw validate_error("header_object: `example` and `examples` are mutually exclusive");
    if (v.schema && v.content)
      throw validate_error("header_object: `schema` and `content` are mutually exclusive");
    if (v.content && v.content->size() != 1)
      throw validate_error("header_object: `content` must have exactly one entry");
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-9
// "If the parameter location is `path`, this property is REQUIRED and its value MUST be `true`."
// "The `example` field is mutually exclusive of the `examples` field."
// "A parameter MUST contain either a `schema` property, or a `content` property, but not both."
// https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#parameter-object
template <>
struct validate<parameter_object> {
  void operator()(parameter_object const& v) {
    if (v.in == parameter_location::path && (!v.required || !*v.required))
      throw validate_error("parameter_object `{}`: `required` must be true when `in` is `path`", v.name);
    if (v.example && v.examples)
      throw validate_error("parameter_object `{}`: `example` and `examples` are mutually exclusive", v.name);
    if (!v.schema && !v.content) {
      throw validate_error(
          "parameter_object `{}`: parameter MUST contain either a `schema` property, or a `content` "
          "property",
          v.name);
    }
    if (v.schema && v.content)
      throw validate_error("parameter_object `{}`: `schema` and `content` are mutually exclusive", v.name);
    if (v.content && v.content->size() != 1)
      throw validate_error("parameter_object `{}`: `content` must have exactly one entry", v.name);
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-4
// "The array MUST NOT be empty."
// "If the `enum` is defined, the value MUST exist in the enum's values."
template <>
struct validate<server_object::variable> {
  void operator()(server_object::variable const& v) {
    if (v.enum_) {
      if (v.enum_->empty())
        throw validate_error("server_variable: `enum` must not be empty if present");
      if (std::find(v.enum_->begin(), v.enum_->end(), v.default_) == v.enum_->end())
        throw validate_error("server_variable: `default` value `{}` is not in `enum`", v.default_);
    }
  }
};

template <>
struct validate<server_object> {
  void operator()(server_object const& v) {
    // все {переменные} в url должны быть объявлены в variables
    // все ключи variables должны использоваться в url
    std::string_view url = v.url;
    std::set<std::string> used_vars;
    size_t pos = 0;
    while ((pos = url.find('{', pos)) != std::string_view::npos) {
      size_t end = url.find('}', pos);
      if (end == std::string_view::npos)
        throw validate_error("server_object: unclosed `{{` in url `{}`", v.url);
      used_vars.emplace(url.substr(pos + 1, end - pos - 1));
      pos = end + 1;
    }
    if (v.variables) {
      for (auto const& [key, _] : *v.variables)
        if (!used_vars.count(key))
          throw validate_error("server_object: variable `{}` declared but not used in url `{}`", key, v.url);
    }
    for (auto const& var : used_vars) {
      if (!v.variables || !v.variables->count(var))
        throw validate_error("server_object: url variable `{}` not declared in `variables`", var);
    }
  }
};

// https://spec.openapis.org/oas/v3.1.0#patterned-fields-0
// "Any HTTP status code can be used as the property name."
// "To define a range of response codes, this field MAY contain the uppercase wildcard character `X`.
//  For example, `2XX` represents all response codes between [200-299].
//  Only the following range definitions are allowed: `1XX`, `2XX`, `3XX`, `4XX`, and `5XX`."
template <>
struct validate<responses_object> {
  void operator()(responses_object const& v) {
    if (v._responses.empty()) {
      throw validate_error(
          "The Responses Object MUST contain at least one response code, and if only one response code is "
          "provided it SHOULD be the response for a successful operation call");
    }
    for (auto const& [key, _] : v._responses) {
      if (key.size() != 3)
        throw validate_error("responses_object: invalid status key `{}`", key);
      char h = key[0];
      if (h < '1' || h > '5')
        throw validate_error("responses_object: invalid status key `{}`", key);
      bool wildcard = key[1] == 'X' && key[2] == 'X';
      bool exact = std::isdigit((unsigned char)key[1]) && std::isdigit((unsigned char)key[2]);
      if (!wildcard && !exact)
        throw validate_error("responses_object: invalid status key `{}`", key);
    }
  }
};

// https://spec.openapis.org/oas/v3.1.0#patterned-fields
// "The field name MUST begin with a forward slash (`/`)."
template <>
struct validate<paths_object> {
  void operator()(paths_object const& v) {
    for (auto const& [key, _] : v) {
      if (key.empty() || key[0] != '/')
        throw validate_error("paths_object: path `{}` must start with `/`", key);
    }
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-5
// "All the fixed fields declared above are objects that MUST use keys that match
//  the regular expression: `^[a-zA-Z0-9\.\-_]+$`."
namespace detail {

inline void check_component_keys(auto const& opt_map, std::string_view field) {
  if (!opt_map)
    return;
  for (auto const& [key, _] : *opt_map) {
    if (key.empty())
      throw validate_error("components_object.{}: empty key", field);
    for (char c : key) {
      if (!std::isalnum((unsigned char)c) && c != '.' && c != '-' && c != '_')
        throw validate_error("components_object.{}: key `{}` does not match ^[a-zA-Z0-9\\.\\-_]+$", field,
                             key);
    }
  }
}

}  // namespace detail

template <>
struct validate<components_object> {
  void operator()(components_object const& v) {
    detail::check_component_keys(v.schemas, "schemas");
    detail::check_component_keys(v.responses, "responses");
    detail::check_component_keys(v.parameters, "parameters");
    detail::check_component_keys(v.examples, "examples");
    detail::check_component_keys(v.requestBodies, "requestBodies");
    detail::check_component_keys(v.headers, "headers");
    detail::check_component_keys(v.securitySchemes, "securitySchemes");
    detail::check_component_keys(v.links, "links");
    detail::check_component_keys(v.callbacks, "callbacks");
    detail::check_component_keys(v.pathItems, "pathItems");
  }
};

template <>
struct validate<oauth_flows_object> {
  void operator()(oauth_flows_object const& v) {
    auto check_flow = [](oauth_flow_object const& f, std::string_view name, bool need_auth_url,
                         bool need_token_url) {
      if (need_auth_url && !f.authorizationUrl)
        throw validate_error("oauth_flow_object ({}): `authorizationUrl` is required", name);
      if (need_token_url && !f.tokenUrl)
        throw validate_error("oauth_flow_object ({}): `tokenUrl` is required", name);
    };

    if (v.implicit)
      check_flow(*v.implicit, "implicit", true, false);
    if (v.password)
      check_flow(*v.password, "password", false, true);
    if (v.clientCredentials)
      check_flow(*v.clientCredentials, "clientCredentials", false, true);
    if (v.authorizationCode)
      check_flow(*v.authorizationCode, "authorizationCode", true, true);
  }
};

template <>
struct validate<security_scheme_object> {
  void operator()(security_scheme_object const& v) {
    static constexpr std::string_view valid_types[] = {"apiKey", "http", "mutualTLS", "oauth2",
                                                       "openIdConnect"};
    if (std::find(std::begin(valid_types), std::end(valid_types), v.type) == std::end(valid_types))
      throw validate_error("security_scheme_object: unknown `type` value `{}`", v.type);

    if (v.type == "apiKey") {
      if (!v.name)
        throw validate_error("security_scheme_object (apiKey): `name` is required");
      if (!v.in)
        throw validate_error("security_scheme_object (apiKey): `in` is required");
      static constexpr std::string_view valid_in[] = {"query", "header", "cookie"};
      if (std::find(std::begin(valid_in), std::end(valid_in), *v.in) == std::end(valid_in))
        throw validate_error("security_scheme_object (apiKey): `in` must be query/header/cookie, got `{}`",
                             *v.in);
    } else if (v.type == "http") {
      if (!v.scheme)
        throw validate_error("security_scheme_object (http): `scheme` is required");
    } else if (v.type == "oauth2") {
      if (!v.flows)
        throw validate_error("security_scheme_object (oauth2): `flows` is required");
    } else if (v.type == "openIdConnect") {
      if (!v.openIdConnectUrl)
        throw validate_error("security_scheme_object (openIdConnect): `openIdConnectUrl` is required");
    }
  }
};

template <>
struct validate<openapi_object> {
  void operator()(const openapi_object& v) {
    if (!v.components && !v.webhooks && !v.paths)
      throw validate_error("openapi object must contain atleast one of components/webhooks/paths");
  }
};

template <>
struct validate<schema_object> {
  void operator()(schema_object const& v) {
    if (v.multipleOf && v.multipleOf->get_as_double() <= 0)
      throw validate_error("schema_object: `multipleOf` must be > 0");
    if (v.maxLength && v.minLength && *v.maxLength < *v.minLength)
      throw validate_error("schema_object: `maxLength` must be >= `minLength`");
    if (v.maxItems && v.minItems && *v.maxItems < *v.minItems)
      throw validate_error("schema_object: `maxItems` must be >= `minItems`");
    if (v.maxProperties && v.minProperties && *v.maxProperties < *v.minProperties)
      throw validate_error("schema_object: `maxProperties` must be >= `minProperties`");
    if (v.maxContains && v.minContains && *v.maxContains < *v.minContains)
      throw validate_error("schema_object: `maxContains` must be >= `minContains`");
    if (v.enum_ && v.enum_->empty())
      throw validate_error("schema_object: `enum` must not be empty if present");
    if (v.allOf && v.allOf->empty())
      throw validate_error("schema_object: `allOf` must not be empty");
    if (v.anyOf && v.anyOf->empty())
      throw validate_error("schema_object: `anyOf` must not be empty");
    if (v.oneOf && v.oneOf->empty())
      throw validate_error("schema_object: `oneOf` must not be empty");
    if (v.prefixItems && v.prefixItems->empty())
      throw validate_error("schema_object: `prefixItems` must not be empty");
    if ((v.then || v.else_) && !v.if_)
      throw validate_error("schema_object: `then`/`else` have no effect without `if`");
  }
};

template <>
struct postparse<schema_object> {
  void operator()(schema_object& schema) {
    if (schema.const_) {
      if (schema.enum_ && !std::ranges::contains(*schema.enum_, *schema.const_)) {
        // TODO добавить варн
        // никогда не сработает
        schema._keyfield_boolean_schema = false;
      }
      schema.enum_.emplace().emplace_back(std::move(*schema.const_));
      schema.const_ = std::nullopt;
    }

    if (schema.type) {
      if (std::ranges::count(schema.type->values, "null") > 1)
        throw validate_error("schema_object: `type` with duplicated null values");
      auto it = std::ranges::find(schema.type->values, "null");
      // если type: "null", то оставляем как есть, это не nullable
      if (it != schema.type->values.end() && schema.type->values.size() != 1) {
        schema.nullable = true;
        schema.type->values.erase(it);
      }
      // чтобы не получался пустой массив типов
      if (schema.type->empty())
        schema.type.reset();
    } else if (schema.properties || schema.required || schema.additionalProperties) {
      // TODO dependentRequired / minProperties / maxProperties это тоже неявный объект
      schema.type.emplace().values.push_back("object");
    }
    if (schema.is_write_only() && schema.is_read_only()) {
      // бессмысленно (может так просто вышло после складывания ссылок)
      schema.writeOnly = std::nullopt;
      schema.readOnly = std::nullopt;
    }
  }
};

}  // namespace svarg
