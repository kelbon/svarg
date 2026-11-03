# Клиент и ответы потоком

## Клиент спеки

На каждую спеку генерируется `struct client` (файл `<spec>/client.hpp`; операции с другими `servers` — в
отдельных `client_<host>`). У клиента транспорт и base path, на каждую операцию — перегрузка `send` по типу
операции:

```cpp
api::client cli{svarg::cxx_support::client_ref(&h2client)};
api::getItem::result_t r = co_await cli.send(api::getItem{...}, hidi::deadline_t::never());
```

Результат — описанный в спеке ответ (несколько ответов — `result_t` с `get_if_<статус>()`). Ошибки
транспорта (соединение, таймаут) — исключения, неописанный статус — `unexpected_response`.

## Транспорт

`svarg::cxx_support::any_client` — всё, что умеет отправить запрос. Интерфейс как у
`hidi::h2client::send_request` с контекстом и колбэками:

```cpp
dd::task<int> send_request(hidi::client_request_context&, on_header_fn_ptr, on_data_part_fn_ptr, http_request,
                           deadline_t);
```

Результат — статус ответа или < 0 (`hidi::reqerr_e`). Контекст заполняется на время запроса: по нему в
колбэках виден статус ответа. `client_ref` — ссылка на `hidi::h2client`. Middleware (ретраи, хедеры,
авторизация, логирование) — тоже `any_client`, оборачивающий другой (пример — в `any_client.hpp`). Ответ
сгенерированный клиент собирает и разбирает сам поверх этого метода.

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

Тело потокового медиа типа в структуре ответа — `svarg::cxx_support::stream_body` без данных. Ответ, как и в
спеке, выбирается по статусу, а тело ответа — по content-type: у одного статуса могут быть и обычные тела, и
потоковые (например `application/json` и `text/event-stream`, как у chat completions с `stream: true`).

Сервер отправляет тело потоком средствами hidi:

```cpp
dd::task<hidi::http_response> handle(api::getBlob op, hidi::http_request, hidi::request_context ctx) override {
  api::getBlob::response_200 rsp;  // хедеры ответа, если есть
  co_return ctx.stream_response(rsp.status, svarg::cxx_support::make_response_headers(rsp),
                                read_file_parts(path));
}
```

`make_response_headers` — хедеры ответа и content-type, `read_file_parts` — корутина, возвращающая
`hidi::streaming_body_t`. Если у ответа несколько тел, медиа тип выбирается телом:

```cpp
api::getChat::response_200 rsp;
if (!stream) {
  rsp.body.emplace<api::getChat::response_200::application_json_body>().get() = answer;
  co_return make_response(rsp);
}
rsp.body.emplace<api::getChat::response_200::text_event_stream_body>();
co_return ctx.stream_response(rsp.status, make_response_headers(rsp), events());
```

`make_response` с потоковым телом и `make_response_headers` с обычным — исключение, а для ответа, единственное
тело которого — поток, `make_response` не компилируется.

### Приём на клиенте

У операции с потоковыми телами ответов вместо `send(op, deadline)` перегрузка с колбэком для тела потока:

```cpp
dd::task<api::getBlob::result_t> send(const api::getBlob& op, svarg::cxx_support::on_stream_part_fn_ptr on_part,
                                      hidi::deadline_t deadline);
```

Статус и хедеры приходят раньше тела, поэтому тело выбирается сразу: если по статусу и content-type это
потоковое тело спеки, оно приходит в `on_part(status, content_type, part, last)` по мере прихода, иначе
собирается и разбирается как обычно (в том числе ответы не из спеки и content-type, которого нет в спеке).
В результате у потокового тела — `stream_body`, хедеры разобраны. Колбэк синхронный: внутри нельзя `co_await`,
данные нужно обработать сразу (например отдать потоковому парсеру с `.feed`).

```cpp
auto on_part = [&](int /*status*/, std::string_view /*content_type*/, std::span<const hidi::byte_t> part,
                   bool /*last*/) {
  file.write((const char*)part.data(), part.size());
};
api::getBlob::result_t r = co_await cli.send(api::getBlob{...}, &on_part, deadline);
if (auto* ok = r.get_if_200()) ...         // тело уже в файле, хедеры разобраны
else if (auto* nf = r.get_if_404()) ...    // JSON собран и разобран
```

Примеры целиком: `examples/cxx_stream_server` (сервер) и `examples/cxx_stream_client` (клиент к нему).
