#pragma once

#include "svarg/utils/all.hpp"
#include "svarg/frontend/openapi/fundamental_types.hpp"

#include <boost/json/serialize.hpp>
#include <ranges>

namespace svarg {

struct license_object {
  string name;
  // может быть только одно поле identifier или url
  std::optional<string> identifier;  // SPDX апи, типа Apache-2.0
  std::optional<string> url;         // MUST be URL
};

struct contact_object {
  std::optional<string> name;
  std::optional<string> url;    // MUST be URL
  std::optional<string> email;  // MUST be valid email
};

struct info_object {
  string title;
  string version;
  std::optional<string> summary;
  std::optional<string> description;
  std::optional<string> termsOfService;
  optional<contact_object> contact;
  optional<license_object> license;
};

struct server_object {
  struct variable {
    // MUST NOT be empty
    std::optional<array<string>> enum_;
    // если поле enum есть, то значение должно быть оттуда
    string default_;
    std::optional<string> description;
  };
  // URL или шаблон URL (в шаблон подставляются значения из variables)
  string url;
  std::optional<string> description;
  std::optional<string_map<variable>> variables;
};

struct external_documentation_object {
  string url;  // MUST be URL
  std::optional<string> description;
};

struct example_object {
  std::optional<string> summary;
  std::optional<string> description;
  // разрешено только одно value или externalValue
  std::optional<Any> value;
  std::optional<string> externalValue;
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
  std::optional<string_map<string>> mapping;
};

struct xml_object {
  std::optional<string> name;
  std::optional<string> namespace_;  // MUST be URI
  std::optional<string> prefix;
  // дефолт: false
  std::optional<bool> attribute;
  // имеет смысл только для array
  // дефолт: false
  std::optional<bool> wrapped;
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
  std::optional<string> format;
  // МЕТА https://datatracker.ietf.org/doc/draft-ietf-jsonschema-json-schema/
  std::optional<string> title;
  std::optional<string> description;
  std::optional<Any> default_;
  std::optional<bool> deprecated;
  // может присутствовать только в ответе
  std::optional<bool> readOnly;
  // может присутствовать только в запросе
  std::optional<bool> writeOnly;
  std::optional<array<Any>> examples;
  // в 3.1 убрано, остановлено чтобы парсилось и эквивалентно "type": ["x", "null"]
  // а оно в свою очередь эквивалентно anyof
  // Note: svarg считает, что nullable это эквивалент NOT required, а пришедший json null
  // эквивалентен отсутствию поля.
  // Также type: [x, null] эквивалентно nullable = true + type: x
  // Поле nullable типа обозначенное required вызовет предупреждение
  // Бекенд может считать, что такое поле может не прийти и более того, если придёт null на любое другое
  // поле, это может считаться эквивалентным его отсутствию
  std::optional<bool> nullable;

  std::optional<discriminator_object> discriminator;
  optional<xml_object> xml;
  optional<external_documentation_object> externalDocs;
  // убрано в 3.1, остановлено чтобы парсилось - всем похуй на это
  std::optional<Any> example;
  // в openapi написана дичь в стиле иди читай rfc Json Schema и сам ищи что там может быть
  // взято из
  // https://ftp.ripe.net/internet-drafts/draft-bhutton-json-schema-validation-01.html

  // валидация

  std::optional<one_or_array<string>> type;
  // если есть, то должно быть хотя бы одно значение и все значения должны быть уникальны
  std::optional<array<Any>> enum_;
  // == enum с одним значением, все значения должны быть такими
  std::optional<Any> const_;

  // валидация для интов

  // ДОЛЖНО быть > 0
  std::optional<number> multipleOf;
  std::optional<number> maximum;
  std::optional<number> exclusiveMaximum;
  std::optional<number> minimum;
  std::optional<number> exclusiveMinimum;

  // валидация для строк

  std::optional<uint64_t> maxLength;
  std::optional<uint64_t> minLength;
  // SHOULD be регекс ECMA-262
  std::optional<string> pattern;

  // валидация для массивов

  std::optional<uint64_t> maxItems;
  std::optional<uint64_t> minItems;
  // дефолт: false
  std::optional<bool> uniqueItems;
  std::optional<uint64_t> maxContains;
  // если отсутствует эквивалентно 1
  std::optional<uint64_t> minContains;

  // валидация для объектов

  std::optional<uint64_t> maxProperties;
  // если отсутствует эквивалентно 0
  std::optional<uint64_t> minProperties;
  // все элементы должны быть уникальны
  std::optional<array<string>> required;
  // в каждом массиве строки ДОЛЖНЫ быть уникальны
  // списки требуемых полей если какое-то поле существует
  std::optional<string_map<array<string>>> dependentRequired;

  // логические операции над схемами

  // allOf, anyOf, oneOf должны быть не пустым массивом
  // если есть одновременно type: и allOf/anyOf/oneOf/ not, то они обозначают требование, а не представление
  // типа. н-р type: object + anyOf: required [A], required [B]
  std::optional<array<reference_or<schema_object>>> allOf;
  // вопреки интуиции anyOf семантически парсит ВСЕ схемы, а не до первой попавшейся и выдаёт все поля,
  // которые получилось собрать в результате
  // для этого, вероятно, бекенду стоит объединять все типы в anyOf в некий один тип, либо просто делать это
  // набором опциональных полей, т.е. anyOf [A, B, C] => struct { optional A, optional B, optional C }
  std::optional<array<reference_or<schema_object>>> anyOf;
  std::optional<array<reference_or<schema_object>>> oneOf;
  optional<reference_or<schema_object>> not_;

  // условия на схемах...

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
  std::optional<bool> _keyfield_boolean_schema;

  [[nodiscard]] bool is_write_only() const noexcept {
    return writeOnly && *writeOnly;
  }
  [[nodiscard]] bool is_read_only() const noexcept {
    return readOnly && *readOnly;
  }
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

// TODO string switch add or_nullopt

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
  // property name -> encoding info
  // каждый ключ должен присутствовать в schema как property (а значит если есть encoding должна быть и
  // schema)
  optional<string_map<encoding_object>> encoding;
};

// описывает параметр операции, уникальный параметр определяется по ключу
// name + location
// возможные локации: (поле `in`)
// path, query, header, cookie
struct parameter_object {
  string name;
  parameter_location in;
  std::optional<string> description;
  // если `in` == path, то это поле обязательно и оно должно быть `true`
  // дефолт false
  std::optional<bool> required;
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
  // Может быть указано ЛИБО schema ЛИБО content
  optional<reference_or<schema_object>> schema;
  // MUST содержать ровно одно значение
  optional<string_map<media_type_object>> content;
  // разрешено только одно поле - example или examples
  optional<Any> example;
  optional<string_map<reference_or<example_object>>> examples;
};

struct request_body_object {
  // REQUIRED
  // ключ content type (или media type range)
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
  std::optional<string_map<reference_or<header_object>>> headers;
  // ключ contentType или media type range
  optional<string_map<media_type_object>> content;
  optional<string_map<reference_or<link_object>>> links;
};

struct responses_object {
  std::optional<reference_or<response_object>> default_;
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
    from_json(v, res._responses);
    if (auto it = res._responses.find("default"); it != res._responses.end()) {
      res.default_.emplace(std::move(it->second));
      res._responses.erase(it);
    }
    out = std::move(res);
    // имена провалидируются позже (2XX ...)
  }
};

template <>
struct serialize<responses_object> {
  boost::json::value operator()(responses_object const& in) {
    // собираем всё обратно в плоскую string_map
    boost::json::object out;
    if (in.default_.has_value())
      out.emplace("default", serialize<reference_or<response_object>>{}(*in.default_));
    for (auto const& [code, resp] : in._responses)
      out.emplace(code, serialize<reference_or<response_object>>{}(resp));
    return out;
  }
};

struct path_item_object;

// мапа возможных калбеков связанных с родительской операцией
// ключом должны быть {expression}
// https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#runtime-expressions
using callback_object = string_map<reference_or<path_item_object>>;

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
  // map name -> callback
  optional<string_map<reference_or<callback_object>>> callbacks;
  // дефолт: false
  optional<bool> deprecated;
  optional<array<security_requirement_object>> security;
  optional<array<server_object>> servers;
};

struct path_item_object {
  // в спецификации нормально написано, что reference_object и path_item_object полностью пересекаются по
  // полям но для парсинга здесь нет $ref (любой объект с $ref парсит все остальные поля в override fields)
  // при этом summary и description остаются здесь для соответствия спеке
  // optional<string> ref_;
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

  auto all_operations() const noexcept {
    return all(get, put, post, delete_, options, head, patch, trace);
  }
  auto all_operations_with_method() const noexcept {
    return std::views::zip(
               all_operations(),
               // Note: важно что все типы
               std::array{+"get", +"put", +"post", +"delete", +"options", +"head", +"patch", +"trace"}) |
           std::views::filter([](auto&& x) { return !!std::get<0>(x); }) |
           std::views::transform([](auto&& x) -> decltype(auto) {
             auto& [op, str] = x;
             return std::pair<std::string_view, const operation_object&>(str, *op);
           });
  }
};

// TODO здесь надо специальную мапу со сравнением без учёта шаблонизирования?

// хранит пути относительно url сервера
// все пути должны начинаться с '/'
// при поиске подходящего пути приоритет у тех
// которые НЕ шаблонизированы
// пути должны быть уникальны не учитывая имена в шаблонах, т.е.
// /pets/{name} == /pets/{id}
// Note: формально в спеке не написано что это reference object | path item object
// а просто указано path item object, непонятно почему так (в отличие от других мест)
// но в path item object ссылка просто включена в сам объект, поэтому так
using paths_object = string_map<reference_or<path_item_object>>;

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
  optional<string_map<reference_or<path_item_object>>> pathItems;
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
  // Должно быть хотя бы одно paths/webhooks/components
  optional<paths_object> paths;
  optional<string_map<reference_or<path_item_object>>> webhooks;
  optional<components_object> components;
  optional<array<security_requirement_object>> security;
  optional<array<tag_object>> tags;
  optional<external_documentation_object> externalDocs;
};

}  // namespace svarg
