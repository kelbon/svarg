# Клиент и ответы потоком

## Клиент спеки

На каждую спеку генерируется `struct client` (файл `<spec>/client.hpp`; операции с другими `servers` — в
отдельных `client_<host>`). У клиента транспорт и base path, на каждую операцию — перегрузка `send` по типу
операции:

```cpp
api::client cli{svarg::cxx_support::client_ref(h2client)};
api::getItem::result_t r = co_await cli.send(api::getItem{...}, hidi::deadline_t::never());
```

Результат — описанный в спеке ответ (несколько ответов — `result_t` с `get_if_<статус>()`). Ошибки
транспорта (соединение, таймаут) — исключения, неописанный статус — `unexpected_response`.

## Транспорт

`svarg::cxx_support::any_client` — всё, что умеет отправить запрос. Интерфейс как у
`hidi::h2client::send_request` с колбэками:

```cpp
dd::task<int> send_request(on_header_fn_ptr, on_data_part_fn_ptr, http_request, deadline_t);
```

Результат — статус ответа или < 0 (`hidi::reqerr_e`). `client_ref` — ссылка на `hidi::h2client`.
Middleware (ретраи, хедеры, авторизация, логирование) — тоже `any_client`, оборачивающий другой (пример — в
`any_client.hpp`). Ответ целиком сгенерированный клиент собирает сам поверх этого метода.

## Ответы потоком

Тело ответа потокового медиа типа не собирается целиком: сервер отправляет его частями, клиент получает
частями. По умолчанию потоковый медиа тип один — `application/octet-stream`, свои добавляются опцией
генератора:

```
svarg --stream-content-type text/event-stream ...
```

```cmake
svarg_generate_cxx(... STREAM_CONTENT_TYPES text/event-stream)
```

У структуры такого ответа нет тела, только статус, хедеры и `stream_content_type`. Если у того же статуса
есть и другие медиа типы, их тела тоже не генерируются: ответ с потоковым медиа типом — поток целиком.

Сервер отправляет тело средствами hidi:

```cpp
dd::task<hidi::http_response> handle(api::getBlob op, hidi::http_request, hidi::request_context ctx) override {
  api::getBlob::response_200 rsp;  // хедеры ответа, если есть
  co_return ctx.stream_response(rsp.status, make_response_headers(rsp), read_file_parts(path));
}
```

`make_response_headers` — хедеры ответа и content-type, `read_file_parts` — корутина, возвращающая
`hidi::streaming_body_t`. `make_response` для такого ответа не компилируется.

### Приём на клиенте

Если среди ответов операции есть поток, на клиенте вся операция — поток. svarg не разбирает ни один её
ответ, у клиента вместо `send(op, deadline)` одна перегрузка, как у hidi:

```cpp
dd::task<int> send(const api::getBlob& op, hidi::on_header_fn_ptr on_header,
                   hidi::on_data_part_fn_ptr on_data_part, hidi::deadline_t deadline);
```

Хедеры любого ответа приходят в `on_header`, тело любого ответа — в `on_data_part` по мере прихода,
результат — статус или < 0 (`hidi::reqerr_e`). Колбэки синхронные: внутри нельзя `co_await`, данные нужно
обработать сразу (например отдать потоковому парсеру с `.feed`).

Разобрать ответ можно теми же функциями, что использует обычный `send`:

- `parse_response<Op>(http_response&)` — выбирает описанный ответ по статусу и разбирает его, результат —
  `Op::result_t`. У ответа потоком разбираются статус и хедеры (тела в структуре нет). Статус < 0 —
  исключение, неописанный статус — `unexpected_response`;
- `from_response(http_response, R&)` — разбор в конкретный ответ `R`, если статус уже проверен;
- `match_content_type(R::stream_content_type, content_type)` — пришло ли тело потокового медиа типа.

Статус hidi сообщает только в конце, поэтому во время приёма поток определяется по content-type:

```cpp
hidi::http_response rsp;  // хедеры и тело непотоковых ответов (например ошибки в JSON)
bool streaming = false;
auto on_header = [&](std::string_view name, std::string_view value) {
  if (name == "content-type" &&
      svarg::match_content_type(api::getBlob::response_200::stream_content_type, value))
    streaming = true;
  rsp.headers.emplace_back(std::string(name), std::string(value));
};
auto on_data = [&](std::span<const hidi::byte_t> part, bool last) {
  if (streaming)
    file.write(part);
  else
    rsp.body.insert(rsp.body.end(), part.begin(), part.end());
};
rsp.status = co_await cli.send(api::getBlob{...}, &on_header, &on_data, hidi::deadline_t::never());
api::getBlob::result_t r = svarg::cxx_support::parse_response<api::getBlob>(rsp);
if (auto* ok = r.get_if_200()) ...         // тело уже в файле, хедеры разобраны
else if (auto* nf = r.get_if_404()) ...    // тело разобрано из rsp.body
```
