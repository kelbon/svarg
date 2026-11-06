# Что генерируется в С++

Всё ниже - для спеки с именем `shop.yaml` (namespace и папка берутся из имени файла)

## Схемы

| В спеке | В С++ |
|---|---|
| `components/schemas/Order` | `shop::components::schemas::Order`, файл `shop/schemas/components/schemas/Order.hpp` |
| инлайн схема свойства `delivery` у `Order` | `shop::components::schemas::Order_::properties::delivery` |
| `type: object` | `struct` с полями. Имена полей - имена свойств, превращённые в идентификаторы С++ |
| обязательное свойство | `T name;` |
| необязательное свойство | `std::optional<T> name;` |
| `type: string` | `std::string` |
| `type: integer` | `std::int64_t` (с `format: int32` - `std::int32_t`, см. [форматы](cxx_format.md)) |
| `type: number` | `svarg::number` (целое или double без потери точности) |
| `type: boolean` | `bool` |
| схема без `type`, не ограничивающая | `boost::json::value` |
| `type: array` | `std::vector<T>` |
| `additionalProperties: <схема>` | поле `svarg_additional_properties` (`string_map<T>`) |
| `type: [string, "null"]`, `nullable: true` | `std::optional<T>` |
| рекурсия (`Order.parent` - `Order`) | `metel::box<Order>`, пустой если поля нет |
| `enum` строк | `struct` с `get()` и константами: `order.status.get() = Order_::properties::status::paid` |
| `anyOf: [enum строк, string]` | `struct` со строкой `value`, константами и `is_known()` |
| `allOf` объектов | один `struct` со всеми полями |
| `oneOf` | `struct` с `get()` -> `svarg::cxx_support::oneof<Card, Cash>` |
| `anyOf` | `struct` с `get()` -> `svarg::cxx_support::anyof<Card, Named>`, `get<Card>()` - `metel::box<Card>`, заполнен если значение подошло под `Card` |
| ограничения (`maxLength`, `minimum`...) | `struct` с `get()` и `svarg_constraints`, значение проверяется при разборе и отправке |

`oneOf`/`anyOf` и рекурсивные поля не оборачиваются в `std::optional`: они сами могут быть пустыми

Именованная схема простого типа (`Day: {type: string, format: date}`) - `struct` с `get()`, инлайн - сразу тип:

```cpp
order.items[0].sku = "a-1";
order.items[0].count.emplace().get() = 3;  // count: {type: integer, minimum: 1}
order.status.get() = Order_::properties::status::paid;
order.payment.get().emplace<Card>().number = "4242...";
```

## Операции

```yaml
paths:
  /orders/{id}:
    get:
      operationId: getOrder
      parameters:
        - {name: id, in: path, required: true, schema: {type: integer}}
        - {name: fields, in: query, schema: {type: array, items: {type: string}}}
        - {name: x-request-id, in: header, schema: {type: string}}
        - {name: session, in: cookie, schema: {type: string}}
      responses:
        "200": ...    # application/json: Order, хедер x-rate-limit
        "404": ...    # без тела
        default: ...  # application/problem+json: Problem
```

| В спеке | В С++ |
|---|---|
| операция `getOrder` | `struct shop::getOrder`, файл `shop/operations/getOrder.hpp` |
| параметры `path`, `query`, `header`, `cookie` | поля `op.path.id`, `op.query.fields`, `op.headers.x_request_id`, `op.cookies.session` |
| `requestBody` | `op.body`. Несколько медиа типов - `std::variant` тел, по одному на медиа тип |
| ответ `"200"` | `getOrder::response_200` с полями `headers` и `body` |
| ответ `default` | `getOrder::default_response`, статус в поле `status` |
| несколько ответов | `getOrder::result_t` с `get_if_200()`, `get_if_404()`, `get_if_default()` |
| один ответ | `result_t` - сам ответ |
| `security` | параметры: `http` и `oauth2` - `op.headers.authorization`, `apiKey` - параметр с его именем |
| `servers: [{url: https://shop.example.com/v1}]` | `client::base_path = "/v1"`, маршруты сервера с `/v1` |

На спеку генерируются `shop::server` (`shop/server.hpp`) с обработчиком `handle(getOrder, ...)` на каждую операцию и
`shop::client` (`shop/client.hpp`) с `send(getOrder, deadline)`, см. [сервер](cxx_server.md) и [клиент](cxx_client.md)

```cpp
shop::getOrder op;
op.path.id = 1;
op.headers.authorization = "Bearer " + token;
shop::getOrder::result_t r = co_await cli.send(op, hidi::deadline_after(std::chrono::seconds(5)));
if (auto* ok = r.get_if_200())
  std::println("{} items, rate limit {}", ok->body.items.size(), ok->headers.x_rate_limit.value_or(0));
```

## Файлы

На спеку `shop.yaml` - папка `shop/`:

- `server.hpp`, `client.hpp` - если в спеке есть операции
- `operations/<operationId>.hpp`, `schemas/...` - по заголовку на операцию и схему
- `all_schemas.hpp`, `all_operations.hpp`, `all_webhooks.hpp` - всё вместе
- `shop.cpp` (и `shop_1.cpp`... при указанном `SOURCES_PER_SPEC`> 1) - сервер, клиент, разбор и сериализация операций
