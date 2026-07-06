#pragma once

// этот файл - часть openapi.hpp

#include <set>
#include "svarg/frontend/openapi/openapi_object.hpp"

namespace svarg {

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-2
// "The `identifier` field is mutually exclusive of the `url` field."
template <>
struct validate<license_object> {
  error_code operator()(const license_object& v) {
    if (v.identifier && v.url)
      return validate_error("license_object: `identifier` and `url` are mutually exclusive");
    return {};
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-15
// "The `value` field and `externalValue` field are mutually exclusive."
template <>
struct validate<example_object> {
  error_code operator()(const example_object& v) {
    if (v.value && v.externalValue)
      return validate_error("example_object: `value` and `externalValue` are mutually exclusive");
    return {};
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-11
// "The `example` field is mutually exclusive of the `examples` field."
template <>
struct validate<media_type_object> {
  error_code operator()(const media_type_object& obj) {
    if (obj.example && obj.examples)
      return validate_error("media_type_object: `example` and `examples` are mutually exclusive");
    // https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#media-type-object
    if (obj.encoding && !obj.encoding->empty() && !obj.schema)
      return validate_error(
          "media_type_object: all keys from `encoding` must be in `schema`, but `schema` not setted");
    return {};
  }
};

// https://spec.openapis.org/oas/v3.1.0#header-object
// Header Object follows Parameter Object rules (same fixed fields):
// "The `example` field is mutually exclusive of the `examples` field."
// "A parameter MUST contain either a `schema` property, or a `content` property, but not both."
template <>
struct validate<header_object> {
  error_code operator()(const header_object& v) {
    if (v.example && v.examples)
      return validate_error("header_object: `example` and `examples` are mutually exclusive");
    if (v.schema && v.content)
      return validate_error("header_object: `schema` and `content` are mutually exclusive");
    if (v.content && v.content->size() != 1)
      return validate_error("header_object: `content` must have exactly one entry");
    return {};
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-9
// "If the parameter location is `path`, this property is REQUIRED and its value MUST be `true`."
// "The `example` field is mutually exclusive of the `examples` field."
// "A parameter MUST contain either a `schema` property, or a `content` property, but not both."
// https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#parameter-object
template <>
struct validate<parameter_object> {
  error_code operator()(const parameter_object& v) {
    if (v.in == parameter_location::path && (!v.required || !*v.required))
      return validate_error("parameter_object `{}`: `required` must be true when `in` is `path`", v.name);
    if (v.example && v.examples)
      return validate_error("parameter_object `{}`: `example` and `examples` are mutually exclusive", v.name);
    if (!v.schema && !v.content) {
      return validate_error(
          "parameter_object `{}`: parameter MUST contain either a `schema` property, or a `content` "
          "property",
          v.name);
    }
    if (v.schema && v.content)
      return validate_error("parameter_object `{}`: `schema` and `content` are mutually exclusive", v.name);
    if (v.content && v.content->size() != 1)
      return validate_error("parameter_object `{}`: `content` must have exactly one entry", v.name);
    return {};
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-4
// "The array MUST NOT be empty."
// "If the `enum` is defined, the value MUST exist in the enum's values."
template <>
struct validate<server_object::variable> {
  error_code operator()(const server_object::variable& v) {
    if (v.enum_) {
      if (v.enum_->empty())
        return validate_error("server_variable: `enum` must not be empty if present");
      if (std::find(v.enum_->begin(), v.enum_->end(), v.default_) == v.enum_->end())
        return validate_error("server_variable: `default` value `{}` is not in `enum`", v.default_);
    }
    return {};
  }
};

template <>
struct validate<server_object> {
  error_code operator()(const server_object& v) {
    // все {переменные} в url должны быть объявлены в variables
    // все ключи variables должны использоваться в url
    std::string_view url = v.url;
    std::set<std::string> used_vars;
    size_t pos = 0;
    while ((pos = url.find('{', pos)) != std::string_view::npos) {
      size_t end = url.find('}', pos);
      if (end == std::string_view::npos)
        return validate_error("server_object: unclosed `{{` in url `{}`", v.url);
      used_vars.emplace(url.substr(pos + 1, end - pos - 1));
      pos = end + 1;
    }
    if (v.variables) {
      for (const auto& [key, _] : *v.variables)
        if (!used_vars.count(key))
          return validate_error("server_object: variable `{}` declared but not used in url `{}`", key, v.url);
    }
    for (const auto& var : used_vars) {
      if (!v.variables || !v.variables->count(var))
        return validate_error("server_object: url variable `{}` not declared in `variables`", var);
    }
    return {};
  }
};

// https://spec.openapis.org/oas/v3.1.0#patterned-fields-0
// "Any HTTP status code can be used as the property name."
// "To define a range of response codes, this field MAY contain the uppercase wildcard character `X`.
//  For example, `2XX` represents all response codes between [200-299].
//  Only the following range definitions are allowed: `1XX`, `2XX`, `3XX`, `4XX`, and `5XX`."
template <>
struct validate<responses_object> {
  error_code operator()(const responses_object& v) {
    if (v._responses.empty()) {
      return validate_error(
          "The Responses Object MUST contain at least one response code, and if only one response code is "
          "provided it SHOULD be the response for a successful operation call");
    }
    for (const auto& [key, _] : v._responses) {
      if (key.size() != 3)
        return validate_error("responses_object: invalid status key `{}`", key);
      char h = key[0];
      if (h < '1' || h > '5')
        return validate_error("responses_object: invalid status key `{}`", key);
      bool wildcard = key[1] == 'X' && key[2] == 'X';
      bool exact = std::isdigit((unsigned char)key[1]) && std::isdigit((unsigned char)key[2]);
      if (!wildcard && !exact)
        return validate_error("responses_object: invalid status key `{}`", key);
    }
    return {};
  }
};

// https://spec.openapis.org/oas/v3.1.0#patterned-fields
// "The field name MUST begin with a forward slash (`/`)."
template <>
struct validate<paths_object> {
  error_code operator()(const paths_object& v) {
    for (const auto& [key, _] : v) {
      if (key.empty() || key[0] != '/')
        return validate_error("paths_object: path `{}` must start with `/`", key);
    }
    return {};
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-5
// "All the fixed fields declared above are objects that MUST use keys that match
//  the regular expression: `^[a-zA-Z0-9\.\-_]+$`."
namespace detail {

[[nodiscard]] inline bool check_component_keys(const auto& opt_map, std::string_view field, error_code& ec) {
  if (!opt_map)
    return true;
  for (const auto& [key, _] : *opt_map) {
    if (key.empty()) {
      ec = validate_error("components_object.{}: empty key", field);
      return false;
    }
    for (char c : key) {
      if (!std::isalnum((unsigned char)c) && c != '.' && c != '-' && c != '_') {
        ec =
            validate_error("components_object.{}: key `{}` does not match ^[a-zA-Z0-9\\.\\-_]+$", field, key);
        return false;
      }
    }
  }
  return true;
}

}  // namespace detail

template <>
struct validate<components_object> {
  error_code operator()(const components_object& v) {
    error_code ec;
    // && просто для обрывания на ошибке
    (void)(detail::check_component_keys(v.schemas, "schemas", ec) &&
           detail::check_component_keys(v.responses, "responses", ec) &&
           detail::check_component_keys(v.parameters, "parameters", ec) &&
           detail::check_component_keys(v.examples, "examples", ec) &&
           detail::check_component_keys(v.requestBodies, "requestBodies", ec) &&
           detail::check_component_keys(v.headers, "headers", ec) &&
           detail::check_component_keys(v.securitySchemes, "securitySchemes", ec) &&
           detail::check_component_keys(v.links, "links", ec) &&
           detail::check_component_keys(v.callbacks, "callbacks", ec) &&
           detail::check_component_keys(v.pathItems, "pathItems", ec));
    return ec;
  }
};

template <>
struct validate<oauth_flows_object> {
  error_code operator()(const oauth_flows_object& v) {
    auto check_flow = [](const oauth_flow_object& f, std::string_view name, bool need_auth_url,
                         bool need_token_url) -> error_code {
      if (need_auth_url && !f.authorizationUrl)
        return validate_error("oauth_flow_object ({}): `authorizationUrl` is required", name);
      if (need_token_url && !f.tokenUrl)
        return validate_error("oauth_flow_object ({}): `tokenUrl` is required", name);
      return {};
    };
    error_code ec;
    if (v.implicit) {
      ec = check_flow(*v.implicit, "implicit", true, false);
      if (ec)
        return ec;
    }
    if (v.password) {
      ec = check_flow(*v.password, "password", false, true);
      if (ec)
        return ec;
    }
    if (v.clientCredentials) {
      ec = check_flow(*v.clientCredentials, "clientCredentials", false, true);
      if (ec)
        return ec;
    }
    if (v.authorizationCode) {
      ec = check_flow(*v.authorizationCode, "authorizationCode", true, true);
      if (ec)
        return ec;
    }
    return ec;
  }
};

template <>
struct validate<security_scheme_object> {
  error_code operator()(const security_scheme_object& v) {
    static constexpr std::string_view valid_types[] = {"apiKey", "http", "mutualTLS", "oauth2",
                                                       "openIdConnect"};
    if (std::find(std::begin(valid_types), std::end(valid_types), v.type) == std::end(valid_types))
      return validate_error("security_scheme_object: unknown `type` value `{}`", v.type);

    if (v.type == "apiKey") {
      if (!v.name)
        return validate_error("security_scheme_object (apiKey): `name` is required");
      if (!v.in)
        return validate_error("security_scheme_object (apiKey): `in` is required");
      static constexpr std::string_view valid_in[] = {"query", "header", "cookie"};
      if (std::find(std::begin(valid_in), std::end(valid_in), *v.in) == std::end(valid_in))
        return validate_error("security_scheme_object (apiKey): `in` must be query/header/cookie, got `{}`",
                              *v.in);
    } else if (v.type == "http") {
      if (!v.scheme)
        return validate_error("security_scheme_object (http): `scheme` is required");
    } else if (v.type == "oauth2") {
      if (!v.flows)
        return validate_error("security_scheme_object (oauth2): `flows` is required");
    } else if (v.type == "openIdConnect") {
      if (!v.openIdConnectUrl)
        return validate_error("security_scheme_object (openIdConnect): `openIdConnectUrl` is required");
    }
    return {};
  }
};

template <>
struct validate<openapi_object> {
  error_code operator()(const openapi_object& v) {
    if (!v.components && !v.webhooks && !v.paths)
      return validate_error("openapi object must contain atleast one of components/webhooks/paths");
    return {};
  }
};

template <>
struct validate<schema_object> {
  error_code operator()(const schema_object& v) {
    if (v.multipleOf && v.multipleOf->get_as_double() <= 0)
      return validate_error("schema_object: `multipleOf` must be > 0");
    if (v.maxLength && v.minLength && *v.maxLength < *v.minLength)
      return validate_error("schema_object: `maxLength` must be >= `minLength`");
    if (v.maxItems && v.minItems && *v.maxItems < *v.minItems)
      return validate_error("schema_object: `maxItems` must be >= `minItems`");
    if (v.maxProperties && v.minProperties && *v.maxProperties < *v.minProperties)
      return validate_error("schema_object: `maxProperties` must be >= `minProperties`");
    if (v.maxContains && v.minContains && *v.maxContains < *v.minContains)
      return validate_error("schema_object: `maxContains` must be >= `minContains`");
    if (v.enum_ && v.enum_->empty())
      return validate_error("schema_object: `enum` must not be empty if present");
    if (v.allOf && v.allOf->empty())
      return validate_error("schema_object: `allOf` must not be empty");
    if (v.anyOf && v.anyOf->empty())
      return validate_error("schema_object: `anyOf` must not be empty");
    if (v.oneOf && v.oneOf->empty())
      return validate_error("schema_object: `oneOf` must not be empty");
    if (v.prefixItems && v.prefixItems->empty())
      return validate_error("schema_object: `prefixItems` must not be empty");
    if ((v.then || v.else_) && !v.if_)
      return validate_error("schema_object: `then`/`else` have no effect without `if`");
    return {};
  }
};

template <>
struct postparse<schema_object> {
  error_code operator()(schema_object& schema) {
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
        return validate_error("schema_object: `type` with duplicated null values");
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
    return {};
  }
};

}  // namespace svarg
