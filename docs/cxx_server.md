# Сервер

На каждую спеку генерируется `struct server` (файл `<spec>/server.hpp`). На
каждую операцию есть виртуальный обработчик - перегрузка `handle` по типу операции:

```cpp
struct my_server : api::server {
  using api::server::server;

  dd::task<hidi::http_response> handle(api::getItem op, hidi::http_request req,
                                       hidi::request_context ctx) override {
    api::getItem::response_200 rsp;
    rsp.body = ...;
    co_return make_response(rsp);
  }
};
```

Обработчик возвращает сырой `http_response`: обычно его строит `make_response` из описанного в спеке
ответа, но можно вернуть что угодно. Ответ потоковым медиа типом отправляется через `ctx.stream_response`

Кроме операций у сервера виртуальные обработчики:

- `on_unknown_operation(req, ctx)` - ни один маршрут не подошёл (по умолчанию 404)
- `on_bad_request(req, error, ctx)` - маршрут найден, но запрос не соответствует операции (параметры,
  тело, ограничения схемы), по умолчанию 400
- `on_user_route(id, req, ctx)` - подошёл маршрут не из спеки (см. ниже), по умолчанию `on_unknown_operation`

Общую для операций проверку (например авторизацию) до вызова обработчика можно сделать хуком `before_handle`,
см. [хуки](cxx_client.md#хуки)

Для `servers: [{url: https://example.com/v1}]` и пути `/items/{id}` маршрут - `/v1/items/{id}`

## Роутер (маршрутизация)

Все операции из спек уже зарегистрированы на сервере, но можно добавить свои. Добавлять и удалять маршруты можно в любой момент (однопоточно!)

Note: для кривых шаблонов путей вроде {name}-{mda} из-за возможных неоднозначностей поиск идёт ДО всех остальных путей и в порядке их добавления (для тех что в спеке порядок в котором они идут в спеке)

`routes()` сервера - его роутер. Операции спеки в нём занимают id от 0 до `server::user_route_begin`, свои
маршруты добавляются с id от `user_route_begin` и приходят в `on_user_route`:

```cpp
struct my_server : api::server {
  using api::server::server;

  dd::task<hidi::http_response> on_user_route(size_t id, hidi::http_request req,
                                              hidi::request_context ctx) override {
    switch (id - user_route_begin) {
      case 0: co_return hidi::http_response{.status = 200};  // /health
      ...
    }
  }
};

server.routes().add_route(hidi::http_method_e::GET, "/health", my_server::user_route_begin);
```

Маршрут операции спеки можно и убрать: `server.routes().remove_route(method, path)` - запрос по нему уйдёт в
`on_unknown_operation`.

## Тонкости роутинга

Правила интуитивные, сложные случаи легче проверить реализацией [`compare_paths_specialized`](../src/utils/path_tools.cpp)

Путь оканчивающийся на шаблон захватывает всё оставшееся.
Так, например, шаблон пути `/repos/{owner}/{repo}/contents/{path}` распарсит аргумент {path} в `/repos/me/svarg/contents/src/main.cpp`
как `src/main.cpp`
