# svarg IR

IR - YAML или JSON файл. Посмотреть IR своей спеки:

```
svarg-ir -s <папка со спеками> -O2 -o api.ir.yaml api.yaml
```

Для такой спеки:

```yaml
openapi: 3.1.0
info:
  title: items
  version: "1.0"
paths:
  /items/{id}:
    get:
      operationId: getItem
      parameters:
        - {name: id, in: path, required: true, schema: {type: integer}}
      responses:
        "200":
          description: found
          content:
            application/json:
              schema: {$ref: "#/components/schemas/Item"}
        "404":
          description: no such item
components:
  schemas:
    Item:
      type: object
      required: [id, name]
      properties:
        id: {type: integer}
        name: {type: string, maxLength: 64}
        kind: {type: string, enum: [book, pen]}
```

IR выглядит так:

```yaml
"schemas":
  - "spec": "items"
    "jsonptr": "/components/schemas/Item"
    "name": "Item"
    "type":
      "object":
        "fields":
          - "type":
              "spec": "@integer@"
            "name": "id"
            "required": true
          - "type":
              "spec": "items"
              "jsonptr": "/components/schemas/Item/properties/kind"
            "name": "kind"
            "required": false
          - "type":
              "spec": "items"
              "jsonptr": "/components/schemas/Item/properties/name"
            "name": "name"
            "required": true
  - "spec": "items"
    "jsonptr": "/components/schemas/Item/properties/kind"
    "type":
      "enum":
        "type":
          "spec": "@string@"
        "values":
          - "book"
          - "pen"
  - "spec": "items"
    "jsonptr": "/components/schemas/Item/properties/name"
    "type":
      "fundamental": "string"
    "required":
      - "spec": "items"
        "jsonptr": "/components/schemas/Item/properties/name/constraints"
  - "spec": "items"
    "jsonptr": "/components/schemas/Item/properties/name/constraints"
    "type":
      "constraints":
        "maxLength": 64
"operations":
  - "id": "getItem"
    "spec": "items"
    "servers":
      - "url": "/"
    "path": "/items/{id}"
    "method": "GET"
    "parameters":
      - "location": "path"
        "name": "id"
        "schema":
          "spec": "@integer@"
        "required": true
    "responses":
      - "status": "200"
        "bodies":
          - "contentType": "application/json"
            "schema":
              "name": "Item"
              "spec": "items"
              "jsonptr": "/components/schemas/Item"
            "required": true
      - "status": "404"
    "priority": 0
```

Описание всех полей - [ir_object.hpp](../include/svarg/ir/ir_object.hpp)

## Верхний уровень

- `schemas` - все схемы
- `operations` - операции
- `webhooks` - вебхуки и колбеки: те же операции, но без `path`

## Схема

Схема определяется парой `spec` (имя файла спеки без расширения) + `jsonptr` (где она в спеке). Схемы
ссылаются друг на друга такой же парой (`name` можно добавить для читаемости). Ссылка на простой тип -
`{spec: "@integer@"}`: `@null@`, `@boolean@`, `@integer@`, `@number@`, `@string@`, `@any@` (любое значение),
`@never@` (никакое)

- `name` - есть у именованных схем (`components/schemas/...`), у инлайн схем нет
- `type` - представление значения, ровно одно из:
  - `fundamental` - `null`, `boolean`, `integer`, `number`, `string`, `any`, `never`
  - `enum` - `type` (ссылка на тип значений), `values`, `open` (допустимо и не перечисленное значение)
  - `object` - `fields` (`name`, `type`, `required`, `default_value`), `additional_properties` (ссылка на
    схему или нет)
  - `array` - `element`
  - `oneOf`, `anyOf` - `schemas`, `discriminator` (`propertyName`, `mapping`: значение -> схема)
  - `allOf` - `schemas`
  - `not` - `type`
  - `constraints` - `minLength`, `maxLength`, `pattern`, `minimum`, `maximum`, `exclusiveMinimum`,
    `exclusiveMaximum`, `multipleOf`, `minItems`, `maxItems`, `uniqueItems`, `minProperties`,
    `maxProperties`. Встречается только как требование
- `required` - требования: схемы, под которые значение тоже должно подходить, но которые не меняют его
  представление. Например `type: string, maxLength: 64` - строка с требованием `constraints`
- `meta` - то, что специфично для языка спецификаций и может пригодиться бекенду, например `x-format-hint`
  (`format` из OpenAPI)

## Операция

- `id`, `spec`, `method` (в верхнем регистре), `path` (относительно сервера), `servers`
- `parameters` - `location` (`path`, `query`, `header`, `cookie`), `name`, `schema`, `required`, `encoding`
  (медиа тип значения), `auth_related` (параметр авторизации), `default_value`, `meta`
- `bodies` - по телу на медиа тип: `contentType`, `schema`, `required`, `membersEncoding` и `membersHeaders`
  (медиа типы и хедеры частей multipart)
- `responses` - `status` (`"200"`, `"2XX"`), `headers` (`name`, `schema`, `required`), `bodies`
- `defaultResponse` - ответ на любой другой статус
- `priority` - порядок выбора, если пути операций неоднозначны (меньше - раньше)
