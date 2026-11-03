# svarg

Генератор C++ из OpenAPI: типы схем, сервер и клиент поверх [hidi](https://github.com/kelbon/hidi) (HTTP/2).
Запросы и ответы разбираются и проверяются по спеке, обработчику приходит уже готовая операция.

## Пример

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
          description: найден
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/Item"
        "404":
          description: нет такого
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
CPMAddPackage("gh:kelbon/svarg@1.0.0")

# код генерируется при сборке
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
  items_server server(hidi::server_ssl_context_ptr(nullptr));  // без TLS
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

Больше примеров — в [examples](examples): поток в ответе, реальная спека 3GPP (NRF).

## Документация

- [Генерация](docs/generation.md) — svarg из командной строки, CMake, сгенерированные файлы, большие спеки
- [Схемы и типы](docs/schemas.md) — что получается из схем: null, лишние свойства, anyOf/oneOf, перечисления
- [Сервер](docs/server.md) — обработчики, свои маршруты, выбор маршрута
- [Клиент](docs/client.md) — `send`, middleware, ответы потоком
- [Медиа типы](docs/media_types.md) — свой медиа тип, JSON под другим именем, потоковые медиа типы
