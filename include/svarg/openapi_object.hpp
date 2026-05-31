#pragma once

#include "svarg/utils/parse_json.hpp"

#include <boost/json/serialize.hpp>

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
  // TODO генерировать дефолтный маппинг перед IR, если его нет
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
  static schema_object parse_if_not_object(boost::json::value const& j) {
    assert(!j.is_object());
    auto* b = j.if_bool();
    if (!b)
      throw parse_error("`{}` is not a valid schema object, not even boolean...", boost::json::serialize(j));
    return schema_object{._keyfield_boolean_schema = *b};
  }

  // схема пускает кого угодно
  [[nodiscard]] bool allow_always() const noexcept {
    return _keyfield_boolean_schema && *_keyfield_boolean_schema;
  }
  // схема не пускает никого
  [[nodiscard]] bool allow_never() const noexcept {
    return _keyfield_boolean_schema && !_keyfield_boolean_schema;
  }

  // "int32", "password" и тд, в том числе расширенные форматы известные пользователю
  optional<string> format;
  // МЕТА https://datatracker.ietf.org/doc/draft-ietf-jsonschema-json-schema/
  optional<string> title;
  optional<string> description;
  optional<Any> default_;
  optional<bool> deprecated;
  optional<bool> readOnly;
  optional<bool> writeOnly;
  optional<array<Any>> examples;
  // в 3.1 убрано, остановлено чтобы парсилось и эквивалентно "type": ["x", "null"]
  // а оно в свою очередь эквивалентно anyof
  optional<bool> nullable;

  optional<discriminator_object> discriminator;
  optional<xml_object> xml;
  optional<external_documentation_object> externalDocs;
  // убрано в 3.1, остановлено чтобы парсилось - всем похуй на это
  optional<Any> example;
  // в openapi написана дичь в стиле иди читай rfc Json Schema и сам ищи что там может быть
  // взято из
  // https://ftp.ripe.net/internet-drafts/draft-bhutton-json-schema-validation-01.html

  // валидация

  optional<one_or_array<string>> type;
  // если есть, то должно быть хотя бы одно значение и все значения должны быть уникальны
  optional<array<Any>> enum_;
  // == enum с одним значением, все значения должны быть такими (нахуя блять?)
  optional<Any> const_;

  // валидация для интов

  // ДОЛЖНО быть > 0
  optional<number> multipleOf;
  optional<number> maximum;
  optional<number> exclusiveMaximum;
  optional<number> minimum;
  optional<number> exclusiveMinimum;

  // валидация для строк

  optional<uint64_t> maxLength;
  optional<uint64_t> minLength;
  // SHOULD be регекс ECMA-262
  optional<string> pattern;

  // валидация для массивов

  optional<uint64_t> maxItems;
  optional<uint64_t> minItems;
  // дефолт: false
  optional<bool> uniqueItems;
  optional<uint64_t> maxContains;
  // если отсутствует эквивалентно 1
  optional<uint64_t> minContains;

  // валидация для объектов

  optional<uint64_t> maxProperties;
  // если отсутствует эквивалентно 0
  optional<uint64_t> minProperties;
  // все элементы должны быть уникальны
  optional<array<string>> required;
  // в каждом массиве строки ДОЛЖНЫ быть уникальны
  // списки требуемых полей если какое-то поле существует
  optional<string_map<array<string>>> dependentRequired;

  // логические операции над схемами

  // allOf, anyOf, oneOf должны быть не пустым массивом
  optional<array<reference_or<schema_object>>> allOf;
  optional<array<reference_or<schema_object>>> anyOf;
  optional<array<reference_or<schema_object>>> oneOf;
  optional<reference_or<schema_object>> not_;

  // условия на схемах... (бесполезная хуйня, это никто не будет реализовывать)

  // проверяет по схеме - если прошло, то проверяет ещё по then
  // если не прошло, проверяет по else (если их нет ничего не написано)
  optional<reference_or<schema_object>> if_;
  // не работает если if нет
  optional<reference_or<schema_object>> then;
  // не работает если if нет
  optional<reference_or<schema_object>> else_;
  // если есть такое-то поле, то нужно провалидировать такой-то схемой
  optional<string_map<reference_or<schema_object>>> dependentSchemas;

  // для элементов массива

  // массив должен быть не пустым
  optional<array<reference_or<schema_object>>> prefixItems;
  // для валидации всех после prefixItems
  optional<reference_or<schema_object>> items;
  optional<reference_or<schema_object>> contains;

  // для элементов объекта

  // имя -> схема для валидации
  optional<string_map<reference_or<schema_object>>> properties;
  // регекс ecma-262 -> схема для валидации
  optional<string_map<reference_or<schema_object>>> patternProperties;
  optional<reference_or<schema_object>> additionalProperties;
  optional<reference_or<schema_object>> propertyNames;

  // какая-то уже совсем заумная хуета

  optional<reference_or<schema_object>> unevaluatedItems;
  optional<reference_or<schema_object>> unevaluatedProperties;
  // специальный филд на случай если схема additionalProperties: true/false
  // (в других местах это тоже разрешено)
  // если есть и true - другие поля неважны, всех пускаем
  // если есть и false - другие поля неважны, всех НЕ пускаем
  // "_keyfield" специальный префикс для таких костылей
  optional<bool> _keyfield_boolean_schema;
};

static_assert(parsable_even_from_not_object<schema_object>);

template <>
struct allow_additional_field<schema_object> {
  static bool allow(std::string_view name) noexcept {
    // $schema/ $id/$defs и прочая гиковская дрянь из метасхем
    return name.starts_with('$') || name.starts_with("x-");
  }
};

struct media_type_object;

// parameter_object с зафиксированным in == header
struct header_object {
  // string name; имя берётся из ключа мапы в которой хранится объект
  // parameter_location in; == header
  optional<string> description;
  optional<bool> required;
  optional<bool> deprecated;
  // единственное валидное значение - "simple"
  optional<string> style;
  optional<bool> explode;
  optional<bool> allowReserved;
  optional<reference_or<schema_object>> schema;
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
  optional<reference_or<schema_object>> schema;
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
  optional<reference_or<schema_object>> schema;
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
  // ключи должны быть строками, даже в ямле (для совместимости с json)
  // https://spec.openapis.org/oas/v3.1.0#patterned-fields-0
  // на удивление может быть прямо 1XX написано, но
  // "Only the following range definitions are allowed: 1XX, 2XX, 3XX, 4XX, and 5XX"
  string_map<reference_or<response_object>> _responses;  // имя не из доки
};

template <>
struct parse<responses_object> {
  void operator()(boost::json::value const& v, responses_object& out) {
    // responses это по сути стринг мапа с ключами 200/201 и тд + возможный default
    responses_object res;
    parse<decltype(res._responses)>{}(v, res._responses);
    if (auto it = res._responses.find("default"); it != res._responses.end()) {
      res.default_.emplace(std::move(it->second));
      res._responses.erase(it);
    }
    out = std::move(res);
    // имена провалидируются позже (2XX ...)
  }
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

struct oauth_flow_object {
  // REQUIRED if "oauth2 ("implicit", "authorizationCode")"
  optional<string> authorizationUrl;
  // REQUIRED if "oauth2 ("password", "clientCredentials", "authorizationCode")"
  optional<string> tokenUrl;
  optional<string> refreshUrl;
  // REQUIRED if oauth2
  // scope name -> short description
  string_map<string> scopes;
};

struct oauth_flows_object {
  optional<oauth_flow_object> implicit;
  optional<oauth_flow_object> password;
  optional<oauth_flow_object> clientCredentials;
  optional<oauth_flow_object> authorizationCode;
};
// TODO дописать валидацию
struct security_scheme_object {
  // возможные значения: "apiKey", "http", "mutualTLS", "oauth2"
  // "openIdConnect"
  string type;
  optional<string> description;
  // REQUIRED IF apiKey
  // имя используемого хедера, query или cookie параметра
  optional<string> name;
  // REQUIRED IF apiKey
  // возможные значения: "query", "header", "cookie"
  optional<string> in;
  // REQUIRED IF http (RFC7235)
  optional<string> scheme;
  // применимо к http ("bearer")
  optional<string> bearerFormat;
  // REQUIRED IF oauth2
  optional<oauth_flows_object> flows;
  // REQUIRED if openIdConnect
  optional<string> openIdConnectUrl;
};

struct operation_object {
  optional<array<string>> tags;
  optional<string> summary;
  optional<string> description;
  optional<external_documentation_object> externalDocs;
  // должно быть уникально среди всех операций апи
  optional<string> operationId;
  optional<array<reference_or<parameter_object>>> parameters;
  optional<reference_or<request_body_object>> requestBody;
  optional<responses_object> responses;
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
  optional<string_map<reference_or<schema_object>>> schemas;
  optional<string_map<reference_or<response_object>>> responses;
  optional<string_map<reference_or<parameter_object>>> parameters;
  optional<string_map<reference_or<example_object>>> examples;
  optional<string_map<reference_or<request_body_object>>> requestBodies;
  optional<string_map<reference_or<header_object>>> headers;
  optional<string_map<reference_or<security_scheme_object>>> securitySchemes;
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
  optional<array<tag_object>> tags;
  optional<external_documentation_object> externalDocs;
};

}  // namespace svarg
