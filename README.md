# svarg

Компилятор спецификаций. В комплекте идёт OpenAPI -> C++, но инструмент этим не ограничен.

## Почему svarg

- сервер и клиент из одной спеки, HTTP/2 на корутинах, без фреймворка вокруг - обычная библиотека
- комбинаторы и ограничения JSON Schema: `anyOf`, `oneOf`, `allOf`, `not`, дискриминаторы, `minLength`, `pattern`...
- спеки любого размера: десятки файлов со ссылками друг на друга, код делится на нужное количество `.cpp`
- подключается одной строкой через CPM, код генерируется при сборке, ни Java, ни Python не нужны

Пример на настоящей промышленной спецификации - [реестр сервисов сети 5G](examples/cxx_nrf): 13 связанных
файлов спек, около 500 сгенерированных заголовков, сервер и клиент целиком

## Пример OpenAPI -> C++

`openapi/items.yaml`:

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
        - name: id
          in: path
          required: true
          schema:
            type: integer
      responses:
        "200":
          description: found
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/Item"
        "404":
          description: no such item
components:
  schemas:
    Item:
      type: object
      required: [id, name]
      properties:
        id:
          type: integer
        name:
          type: string
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.21)
project(items CXX)
set(CMAKE_CXX_STANDARD 23)

include(cmake/CPM.cmake)

# зависимости подключаются через [CPM](https://github.com/cpm-cmake/cpm.cmake)
# сгенерированные клиент и сервер используют [hidi](https://github.com/kelbon/hidi)
CPMAddPackage("gh:kelbon/svarg@1.0.0") # не забудьте указать версию

# код генерируется при сборке, ничего в PATH подключать не нужно
svarg_generate_cxx(
    TARGET    items_api
    SPECS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/openapi
    OUT_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated
    SPECS     items.yaml
)

add_executable(server server.cpp)
target_link_libraries(server PRIVATE items_api)
add_executable(client client.cpp)
target_link_libraries(client PRIVATE items_api)
```

`server.cpp`:

```cpp
#include "items/server.hpp"

using svarg::cxx_support::make_response;

struct items_server : items::server {
  using items::server::server;

  dd::task<hidi::http_response> handle(items::getItem op, hidi::http_request,
                                       hidi::request_context) override {
    if (op.path.id != 1)
      co_return make_response(items::getItem::response_404{});
    items::getItem::response_200 rsp;
    rsp.body.id = 1;
    rsp.body.name = "first";
    co_return make_response(rsp);
  }
};

int main() {
  items_server server(hidi::server_ssl_context_ptr(nullptr));  // для включения TLS достаточно передать контекст
  server.listen(hidi::server_endpoint{.addr = {boost::asio::ip::address_v4::loopback(), 8080}});
  server.run();
}
```

`client.cpp`:

```cpp
#include <print>

#include "items/client.hpp"

int main() {
  hidi::h2client h2{hidi::endpoint(boost::asio::ip::address_v4::loopback(), 8080)};
  items::client cli{svarg::cxx_support::client_ref(&h2)};

  auto get = [&]() -> dd::task<void> {
    items::getItem op;
    op.path.id = 1;
    items::getItem::result_t r = co_await cli.send(op, hidi::deadline_after(std::chrono::seconds(5)));
    if (auto* ok = r.get_if_200())
      std::println("{}", ok->body.name);
    else
      std::println("not found");
    h2.ioctx().stop();
  };
  get().start_and_detach();
  h2.ioctx().run();
}
```

Более серьёзные примеры в [examples](examples)

## Документация для OpenAPI -> C++

- [Если у вас возникли проблемы при сборке](docs/openapi2cxx.md#если-вы-получаете-ошибки-сборки)
- [OpenAPI -> C++](docs/openapi2cxx.md) - подробно о том что поддержано
- [Что генерируется](docs/cxx_codegen.md) - что написано в спеке и что получается в С++
- [format](docs/cxx_format.md) - добавление своих format при генерации в С++
- [Медиа типы](docs/cxx_media_types.md) - добавление своих медиа типов при генерации в С++
- [Сервер](docs/cxx_server.md) - обработчики, routing
- [Клиент](docs/cxx_client.md) - отправка запросов, middleware
- [Концепция svarg](docs/what_is_svarg.md) - описание самого инструмента, создание своего бекенда и фронтенда
