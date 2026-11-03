# Пример: клиент с ответом потоком

Скачивает файлы у сервера из примера `cxx_stream_server` (та же спека). У операции с потоковым ответом
`send(op, on_part, deadline)`: тело ответа 200 приходит в `on_part` по мере прихода, ответ 404 svarg собирает
и разбирает как обычно.

```
cmake -B build -Dsvarg_ENABLE_EXAMPLES=ON ...
cmake --build build --target svarg_stream_server_example svarg_stream_client_example
./build/examples/cxx_stream_server/svarg_stream_server_example &
./build/examples/cxx_stream_client/svarg_stream_client_example
```
