# Пример: NRF по спецификации 3GPP

Реестр сервисов ядра 5G (NRF, TS 29.510 «NF Management») на svarg: реальная спецификация 3GPP целиком, вместе
со всем, на что она ссылается (`test/test_specs`, около 840 сгенерированных заголовков).

- **Сервер** — реестр в памяти: регистрация (PUT, 201 и `Location`), heartbeat (PATCH с JSON Patch), получение,
  список в `application/3gppHal+json` с фильтром по типу, снятие с регистрации, подписки на изменения, ошибки в
  `application/problem+json`, свой маршрут `/health` рядом с операциями спеки. Уведомления подписчикам
  (callback спеки) собираются из сгенерированных структур и печатаются — отправка остаётся приложению.
- **Клиент** — сценарий в `main`: подписаться, зарегистрировать AMF и SMF, отправить heartbeat, найти SMF, снять
  его с регистрации и получить 404.

Сборка — вместе с svarg, `svarg` должен быть в `PATH` на момент конфигурации (`svarg_generate_cxx`):

```
cmake -B build -Dsvarg_ENABLE_EXAMPLES=ON ...
cmake --build build --target svarg_nrf_example
./build/examples/cxx_nrf/svarg_nrf_example
```
