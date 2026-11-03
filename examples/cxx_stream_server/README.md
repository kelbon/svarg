# Пример: сервер с ответом потоком

`GET /files/{name}` отдаёт файл по кускам: ответ 200 — `application/octet-stream` с
хедером `x-file-size` из спеки, 404 — обычный JSON ответ. Тело потока — корутина `hidi::streaming_body_t`,
отправка — `ctx.stream_response(rsp.status, make_response_headers(rsp), body)`.

Клиент к нему — пример `cxx_stream_client`.

```
cmake -B build -Dsvarg_ENABLE_EXAMPLES=ON ...
cmake --build build --target svarg_stream_server_example
./build/examples/cxx_stream_server/svarg_stream_server_example   # localhost:8080
```
