# Клиент

На каждую спеку генерируется `struct client` (файл `<spec>/client.hpp`; операции с другими `servers` - в
отдельных `client_<host>`). У клиента транспорт и base path, на каждую операцию - перегрузка `send` по типу
операции:

```cpp
api::client cli{svarg::cxx_support::client_ref(&h2client)};
api::getItem::result_t r = co_await cli.send(api::getItem{...}, hidi::deadline_t::never());
```

Результат - описанный в спеке ответ (несколько ответов - `result_t` с `get_if_<статус>()`). Ошибки
транспорта (соединение, таймаут) - исключения, неописанный статус - `unexpected_response`

## middleware и всё такое

`svarg::cxx_support::any_client` - всё, что умеет отправить запрос. Интерфейс состоит из одного метода:

```cpp
dd::task<int> send_request(hidi::client_request_context&, on_header_fn_ptr, on_data_part_fn_ptr, http_request,
                           deadline_t);
```

Результат - статус ответа или < 0 (`hidi::reqerr_e`).

Контекст заполняется на время запроса и нужен только для потоковых ответов - по нему в колбэках виден статус ответа до получения данных

`client_ref` - ссылка на `hidi::h2client`. Middleware (ретраи, хедеры, авторизация, логирование) - тоже `any_client`, оборачивающий другой (пример - в `any_client.hpp`)

Ответ сгенерированный клиент собирает и парсит сам поверх этого метода
