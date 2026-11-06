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

`client_ref` - ссылка на `hidi::h2client`. Middleware (ретраи, хедеры, логирование) - тоже `any_client`, оборачивающий другой:

```cpp
// добавляет x-request-id в каждый запрос и пишет в лог путь и статус ответа
struct request_log {
  svarg::cxx_support::any_client next;
  std::string service = "shop";
  size_t counter = 0;

  dd::task<int> send_request(hidi::client_request_context& ctx, hidi::on_header_fn_ptr on_header,
                             hidi::on_data_part_fn_ptr on_data_part, hidi::http_request req,
                             hidi::deadline_t deadline) {
    req.headers.emplace_back("x-request-id", std::to_string(++counter));
    std::string path = req.path;
    int status = co_await next.send_request(ctx, on_header, on_data_part, std::move(req), deadline);
    std::println("{} {} -> {}", service, path, status);  // < 0 - ошибка транспорта (hidi::reqerr_e)
    co_return status;
  }
};

hidi::h2client h2{hidi::endpoint(boost::asio::ip::address_v4::loopback(), 8080)};
shop::client cli{request_log{svarg::cxx_support::client_ref(&h2)}};
```

Ответ сгенерированный клиент собирает и парсит сам поверх этого метода

## Авторизация

`security` из спеки превращается в обычные параметры операции: `http` (bearer, basic) и `oauth2` - хедер
`authorization`, `apiKey` - параметр с именем из спеки. Если вариантов авторизации несколько, параметры
необязательные

```cpp
shop::getOrder get;  // security: bearer
get.path.id = 1;
get.headers.authorization = "Bearer " + token;

shop::createOrder create;  // security: apiKey в хедере X-Api-Key
create.headers.x_api_key = key;
```

На сервере те же поля уже разобраны, проверка - в обработчике:

```cpp
dd::task<hidi::http_response> handle(shop::getOrder op, hidi::http_request, hidi::request_context) override {
  if (!check_token(op.headers.authorization))
    co_return hidi::http_response{.status = 401};
  ...
}
```
