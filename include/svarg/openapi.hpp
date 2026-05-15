#pragma once

#include <filesystem>

#include "svarg/fundamental_types.hpp"
#include "svarg/validate.hpp"

/*
в этом файле openapi_object из спецификации openapi 3.1
с возможностью распарсить входной ямл или json в boost::json::value
*/

namespace svarg {

struct license_object {
  string name;
  // может быть только одно поле identifier или url
  optional<string> identifier;  // SPDX апи, типа Apache-2.0
  optional<string> url;         // MUST be URL
};

struct contact_object {
  optional<string> name;
  optional<string> url;    // MUST be URL
  optional<string> email;  // MUST be valid email
};

struct info_object {
  string title;
  string version;
  optional<string> summary;
  optional<string> description;
  optional<string> termsOfService;
  optional<contact_object> contact;
  optional<license_object> license;
};

struct server_object {
  struct variable {
    // MUST NOT be empty. Если пусто, значит поля не было
    optional<array<string>> enum_;
    // если поле enum есть, то значение должно быть оттуда
    string default_;
    optional<string> description;
  };
  // URL или шаблон URL (в шаблон подставляются значения из variables)
  string url;
  optional<string> description;
  optional<string_map<variable>> variables;
};

struct external_documentation_object {
  string url;  // MUST be URL
  optional<string> description;
};

struct example_object {
  optional<string> summary;
  optional<string> description;
  // разрешено только одно value или externalValue
  optional<Any> value;
  optional<string> externalValue;
};

/*
пример:

MyResponseType:
  oneOf:
  - $ref: '#/components/schemas/Cat'
  - $ref: '#/components/schemas/Dog'
  - $ref: '#/components/schemas/Lizard'
  discriminator:
    propertyName: petType

заставляет ожидать в респонсе petType со значением
равным одному из имён типов (Cat/Dog/Lizard)
*/
struct discriminator_object {
  string propertyName;
  optional<string_map<string>> mapping;
};

struct xml_object {
  optional<string> name;
  optional<string> namespace_;  // MUST be URI
  optional<string> prefix;
  // дефолт: false
  optional<bool> attribute;
  // имеет смысл только для array
  // дефолт: false
  optional<bool> wrapped;
};

struct schema_object {
  discriminator_object discriminator;
  optional<xml_object> xml;
  optional<external_documentation_object> externalDocs;
  optional<Any> example;
};

struct media_type_object;

// parameter_object с зафиксированным in == header
struct header_object {
  // string name; имя берётся из ключа мапы в которой хранится объект
  // parameter_location in; == header
  optional<string> description;
  optional<bool> required;
  optional<bool> deprecated;
  optional<bool> allowEmptyValue;
  optional<string> style;
  optional<bool> explode;
  optional<bool> allowReserved;
  optional<schema_object> schema;
  optional<Any> example;
  optional<string_map<reference_or<example_object>>> examples;
  optional<string_map<media_type_object>> content;
};

struct encoding_object {
  optional<string> contentType;
  // должно быть проигнорировано, если у запроса тело не multipart
  // и Content-Disposition / Content-Type тоже описываются отдельно и игнорируются
  optional<string_map<reference_or<header_object>>> headers;
  // то же самое что в parameter_object
  optional<string> style;
  optional<bool> explode;
  optional<bool> allowReserved;
};

struct media_type_object {
  optional<schema_object> schema;
  // разрешено только одно поле example / examples
  optional<Any> example;
  optional<string_map<reference_or<example_object>>> examples;
  optional<string_map<encoding_object>> encoding;
};

// описывает параметр операции, уникальный параметр определяется по ключу
// name + location
// возможные локации: (поле `in`)
// path, query, header, cookie
struct parameter_object {
  string name;
  parameter_location in;
  optional<string> description;
  // если `in` == path, то это поле обязательно и оно должно быть `true`
  // дефолт false
  optional<bool> required;
  // дефолт false
  optional<bool> deprecated;
  // разрешено только для in == query
  // дефолт false
  // СОВЕТУЮТ НЕ ИСПОЛЬЗОВАТЬ
  optional<bool> allowEmptyValue;
  // по сути почти только для query как записывать параметр
  // default: query -> form, path -> simple, header -> simple, cookie -> form
  optional<string> style;
  // если true параметры массивы или объекты генерят отдельный параметр для каждого значения в массиве или
  // пары ключ-значение в мапе.
  // дефолт: style == form ? true : false
  optional<bool> explode;
  // разрешать ли символы :/?#[]@!$&'()*+,;= без %
  // имеет смысл только если in == query
  // дефолт: false
  optional<bool> allowReserved;
  optional<schema_object> schema;
  // разрешено только одно поле - example или examples
  optional<Any> example;
  optional<string_map<reference_or<example_object>>> examples;
  optional<string_map<media_type_object>> content;
};

struct request_body_object {
  // REQUIRED
  string_map<media_type_object> content;
  optional<string> description;
  // дефолт: false
  optional<bool> required;
};

// бесполезная *****
struct link_object {
  optional<string> operationRef;
  optional<string> operationId;
  optional<string_map<Any /*|{expression}*/>> parameters;
  optional<Any /*|{expression}*/> requestBody;
  optional<string> description;
  optional<server_object> server;
};

struct response_object {
  string description;
  // Content-Type игнорируется. Независимо от регистра
  optional<string_map<reference_or<header_object>>> headers;
  optional<string_map<media_type_object>> content;
  optional<string_map<reference_or<link_object>>> links;
};

struct responses_object {
  optional<reference_or<response_object>> default_;
  // все ключи должны быть 1XX / 2XX / 3XX / 4XX / 5XX
  string_map<reference_or<response_object>> responses;  // имя не из доки
};

struct path_item_object;

// мапа возможных калбеков связанных с родительской операцией
// ключом должны быть {expression}
// https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#runtime-expressions
using callback_object = string_map<path_item_object>;

// список требуемых security схем для выполнения операции
// имена ДОЛЖНЫ соответствовать схемам декларированным в Security Schemas под Components Object
// если тип схемы "oauth2" или "openIdConnect", то значение это список scope names требуемых для исполнения
// и список может быть пустым, если авторизация не требует специфического скоупа
using security_requirement_object = string_map<array<string>>;

struct operation_object {
  optional<array<string>> tags;
  optional<string> summary;
  optional<string> description;
  optional<external_documentation_object> externalDocs;
  // должно быть уникально среди всех операций апи
  optional<string> operationId;
  optional<array<reference_or<parameter_object>>> parameters;
  optional<reference_or<request_body_object>> requestBody;
  optional<response_object> responses;
  optional<string_map<reference_or<callback_object>>> callbacks;
  // дефолт: false
  optional<bool> deprecated;
  optional<array<security_requirement_object>> security;
  optional<array<server_object>> servers;
};

struct path_item_object {
  // ссылка на другой path_item_object
  // "базовый" объхект по сути, поведение зависит от реализации
  // у меня думаю надо чтобы новые поля перекрывали базовые
  optional<string> $ref;
  optional<string> summary;
  optional<string> description;
  optional<operation_object> get;
  optional<operation_object> put;
  optional<operation_object> post;
  optional<operation_object> delete_;
  optional<operation_object> options;
  optional<operation_object> head;
  optional<operation_object> patch;
  optional<operation_object> trace;
  optional<array<server_object>> servers;
  // все параметры должны быть уникальны (ключ name + location)
  optional<array<reference_or<parameter_object>>> parameters;
};

// хранит пути относительно url сервера
// все пути должны начинаться с '/'
// при поиске подходящего пути приоритет у тех
// которые НЕ шаблонизированы
// пути должны быть уникальны не учитывая имена в шаблонах, т.е.
// /pets/{name} == /pets/{id}
using paths_object = string_map<path_item_object>;

struct components_object {
  optional<string_map<schema_object>> schemas;
  optional<string_map<reference_or<response_object>>> responses;
  optional<string_map<reference_or<parameter_object>>> parameters;
  optional<string_map<reference_or<example_object>>> examples;
  optional<string_map<reference_or<request_body_object>>> requestBodies;
  optional<string_map<reference_or<header_object>>> headers;
  optional<string_map<reference_or<security_requirement_object>>> securotySchemes;
  optional<string_map<reference_or<link_object>>> links;
  optional<string_map<reference_or<callback_object>>> callbacks;
  optional<string_map<path_item_object>> pathItems;
};

struct tag_object {
  string name;
  optional<string> description;
  optional<external_documentation_object> externalDocs;
};

struct openapi_object {
  // версия типа 3.1.0
  string openapi;
  info_object info;
  optional<string> jsonSchemaDialect;
  // если этого поля нет или пустое, то это эквивалентно
  // одному server_object{.url = "/"}
  optional<array<server_object>> servers;
  optional<paths_object> paths;
  optional<string_map<path_item_object>> webhooks;
  optional<components_object> components;
  optional<array<security_requirement_object>> security;
  optional<tag_object> tags;
  optional<external_documentation_object> externalDocs;
};

std::string yaml2json(std::string_view yaml);
std::string load_yaml_as_json(std::filesystem::path yaml);

struct svarg_options {
  // если встречается $ref без #, т.е. включающая весь файл внутри неё может быть ещё одна такая же ссылка и
  // тд при этом алгоритмически невозможно понять когда это начало зацикливаться. Чтобы избежать переполнения
  // памяти стоит просто ограничение в глубину
  size_t rawjson_ref_max_depth = 3;
  // если встречается ссылка в виде URL переходить ли по ней (это может быть небезопасно, по умолчанию
  // выключено)
  // TODO под это ещё кеш надо сделать, чтобы лишний раз не ходить с запросами
  bool resolve_url_refs = false;
};

boost::json::value preprocess_openapi_spec(boost::json::value json, const svarg_options& options);

// конвертирует из ямла в json
openapi_object parse_yaml_spec(std::string_view yaml, const svarg_options& options);
openapi_object parse_json_spec(std::string_view json, const svarg_options& options);

// ожидается значение прошедшее preprocess_openapi_spec
openapi_object parse_spec(boost::json::value, const svarg_options& options);
// может быть YAML (.yaml, .yml) или JSON (.json .jsonc .json5)
openapi_object parse_spec(std::filesystem::path path, const svarg_options& options);

// TODO в итоговом джсоне уже не должно быть всяких параметерс и тд прочей хуйни
// должен быть просто список типов, список операций (там прямо перечисленно какие типы в параметрах, их
// названия, где лежат, какие HTTP методы допустимы, тела и их контент типы и тд)
// TODO крч парсинг с валидацией, вероятно надо сразу выделить мой парсер генераторный. Надо его в отдельный
// проект. Причём токены он будет получать из чего угодно (чтобы можно было подменить эту часть)

///
/// ВАЛИДАЦИЯ
///

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
  void operator()(media_type_object const& v) {
    if (v.example && v.examples)
      throw validate_error("media_type_object: `example` and `examples` are mutually exclusive");
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
  }
};

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-9
// "If the parameter location is `path`, this property is REQUIRED and its value MUST be `true`."
// "The `example` field is mutually exclusive of the `examples` field."
// "A parameter MUST contain either a `schema` property, or a `content` property, but not both."
template <>
struct validate<parameter_object> {
  void operator()(parameter_object const& v) {
    if (v.in == parameter_location::path && (!v.required || !*v.required))
      throw validate_error("parameter_object `{}`: `required` must be true when `in` is `path`", v.name);
    if (v.example && v.examples)
      throw validate_error("parameter_object `{}`: `example` and `examples` are mutually exclusive", v.name);
    if (v.schema && v.content)
      throw validate_error("parameter_object `{}`: `schema` and `content` are mutually exclusive", v.name);
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

// https://spec.openapis.org/oas/v3.1.0#patterned-fields-0
// "Any HTTP status code can be used as the property name."
// "To define a range of response codes, this field MAY contain the uppercase wildcard character `X`.
//  For example, `2XX` represents all response codes between [200-299].
//  Only the following range definitions are allowed: `1XX`, `2XX`, `3XX`, `4XX`, and `5XX`."
template <>
struct validate<responses_object> {
  void operator()(responses_object const& v) {
    for (auto const& [key, _] : v.responses) {
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
    detail::check_component_keys(v.securotySchemes, "securitySchemes");
    detail::check_component_keys(v.links, "links");
    detail::check_component_keys(v.callbacks, "callbacks");
    detail::check_component_keys(v.pathItems, "pathItems");
  }
};

}  // namespace svarg
