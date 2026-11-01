# Сервер и маршрутизация

## Сервер спеки

На каждую спеку генерируется `struct server` (файл `<spec>/server.hpp`). Это `hidi::h2server`, у которого на
каждую операцию есть виртуальный обработчик:

```cpp
struct my_server : api::server {
  using api::server::server;  // конструкторы - как у hidi::h2server

  dd::task<hidi::http_response> do_getItem(api::getItem op, hidi::http_request req,
                                           hidi::request_context ctx) override {
    // op уже разобран и проверен: параметры, тело
    api::getItem::response_200 rsp;
    rsp.body = ...;
    co_return make_response(rsp);
  }
};
```

Обработчик возвращает сырой `http_response`: обычно его строит `make_response` из описанного в спеке
ответа, но можно вернуть что угодно.

Кроме операций у сервера виртуальные обработчики:

- `on_unknown_operation(req, ctx)` — ни один маршрут не подошёл (по умолчанию 404);
- `on_bad_request(req, error, ctx)` — маршрут найден, но запрос не соответствует операции (параметры,
  тело, ограничения схемы), по умолчанию 400;
- `on_user_route(id, req, ctx)` — подошёл свой маршрут (см. ниже), по умолчанию `on_unknown_operation`.

Операция доступна по base path каждого своего сервера из спеки: для `servers: [{url: https://x.com/v1}]` и
пути `/items/{id}` маршрут — `/v1/items/{id}`.

## Роутер

`svarg::cxx_support::router` (`support/router.hpp`) по методу и пути запроса находит id маршрута. Обработчиков
он не хранит: что значит id, решает владелец роутера. Значения параметров пути роутер тоже не возвращает —
владелец знает шаблон маршрута по id и разбирает путь по нему (сгенерированный сервер — в `from_request`).
Поэтому `find` константный и не выделяет память.

```cpp
router r;
r.add_route(hidi::http_method_e::GET, "/health", 0);         // путь без шаблонов
r.add_route(hidi::http_method_e::GET, "/items/{id}", 1);     // параметры на весь сегмент
r.add_before(hidi::http_method_e::GET, "/files/{name}.{ext}", 2);  // любой шаблон, линейно
std::optional<size_t> id = r.find(req.method, path);         // путь без query, не декодированный
r.remove_route(hidi::http_method_e::GET, "/items/{x}");      // форма та же, что /items/{id}
r.remove_before(hidi::http_method_e::GET, "/files/{name}.{ext}");
```

Добавлять и удалять маршруты можно в любой момент, в том числе пока обрабатывается запрос, найденный по
удаляемому маршруту.

## Свои маршруты

`routes()` сервера — его роутер. Операции спеки в нём занимают id от 0 до `server::user_route_begin`, свои
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

Маршрут операции спеки можно и убрать: `server.routes().remove_route(method, path)` — запрос по нему уйдёт в
`on_unknown_operation`.

## Порядок выбора маршрута

Для каждого метода по порядку:

1. **Пути без шаблонов** (`/health`, `/v1/items/custom`) — хеш-таблица. Выигрывают у всех.
2. **Список «ДО»** (`add_before`) — шаблоны проверяются линейно в порядке добавления, выигрывает первый
   подошедший.
3. **Шаблоны** (`/items/{id}/data`) — trie. Сегмент пути — либо литерал, либо параметр целиком. Параметр —
   один сегмент без `/`, кроме последнего в шаблоне (см. «Хвост»).

Ни один не подошёл — `on_unknown_operation`.

Метод выбирает таблицу сразу: если `/items/special` есть только для `POST`, запрос `GET /items/special` уйдёт
в `GET /items/{id}`.

Среди шаблонов trie при нескольких подходящих выигрывает тот, у кого литерал раньше:

| Маршруты | Запрос | Выбран |
|---|---|---|
| `/a/b/{y}`, `/a/{x}/b` | `/a/b/b` | `/a/b/{y}` |
| `/items/special/info`, `/items/{id}/details` | `/items/special/details` | `/items/{id}/details` (литеральная ветка — тупик, откат к параметру) |
| `/items/{id}`, `/items/{id}/details` | `/items/5/x` | `/items/{id}`, `id = 5/x` (хвост) |

Один метод и одна форма шаблона (`/a/{x}` и `/a/{y}`) — ошибка: при генерации для операций спеки,
исключение в `add_route` для своих маршрутов.

### Хвост

Последний параметр шаблона забирает остаток пути вместе с `/`: `/repos/{owner}/{repo}/contents/{path}`
подходит к `/repos/me/svarg/contents/src/main.cpp` с `path = src/main.cpp` — так значения шлют клиенты GitHub.
Хвост проверяется последним на своём уровне: если подходит более точный шаблон, выигрывает он.

Цена: `/items/{id}` подходит и к `/items/5/unknown`, такой запрос получит не 404, а ошибку разбора `id`
(`on_bad_request`), если `id` не строка, или попадёт в обработчик с `id = 5/unknown`, если строка.

### Список «ДО»

Сюда идут смешанные сегменты — литерал и параметр в одном сегменте (`/files/{name}.{ext}`, `/v{version}`),
которых нет в trie. Операции спеки с такими путями генератор добавляет сюда сам, в порядке специализации и
приоритета (выше в спеке — раньше). В реальных API они почти не встречаются: в GitHub, Stripe, Kubernetes и
3GPP (около 2000 путей) их нет ни одного.

Значение любого параметра шаблона «ДО» может содержать `/`: `/files/{name}.{ext}` подходит и к
`/files/dir/a.txt`. Поэтому шаблон «ДО» выигрывает у шаблонов trie, даже если по смыслу менее специфичен.
Пути без шаблонов (п. 1) всё равно проверяются раньше.

## Значения параметров пути

Значение параметра percent-кодируется, только если в нём есть символы, недопустимые в сегменте пути (`/`, `?`,
`%`, пробел, не-ASCII...), или разделитель из шаблона. При разборе значение декодируется. Массивы и объекты в
пути — `1,2` и `R=100,G=200` / `R,100,G,200` (style simple), см. раздел про параметры.
