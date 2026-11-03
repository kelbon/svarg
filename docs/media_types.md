# Медиа типы

Тело запроса или ответа кодируется по своему медиа типу из спеки:

| Медиа тип | Кодек | Схема |
|---|---|---|
| `application/json`, любой `...+json` | `json_codec` | любая |
| `application/x-www-form-urlencoded` | `form_codec` | объект |
| `multipart/*` | `multipart_codec` | объект, свойство — часть |
| остальное (`text/plain`, `application/octet-stream`...) | `raw_codec` | строка, число, bool, енам — как есть |

Те же кодеки кодируют части multipart, поля form и параметры с `content:`. Схема, которая не подходит к
формату (объект с `text/plain`), — ошибка компиляции.

## Мой медиа тип — это JSON

Медиа тип JSON без суффикса `+json` (например `application/vnd.github.object`) считается сырым. Подключите к нему
`json_codec`:

```cpp
// my_codecs.hpp
template <>
struct svarg::cxx_support::media_codec<"application/vnd.github.object"> : svarg::cxx_support::json_codec {};
```

Специализация должна быть видна во всех сгенерированных файлах, поэтому заголовок подключается опцией
генератора `--file-prefix '#include "my_codecs.hpp"'`.

## Свой формат

Кодек — структура с двумя функциями:

```cpp
struct my_xml_codec {
  // media_type - без параметров, в params можно добавить параметры content-type, данные - в конец out
  template <typename T>
  static void serialize(const T& value, std::string_view media_type, svarg::content_type_params& params,
                        hidi::http_body_bytes& out);
  // content_type - значение хедера целиком
  template <typename T>
  static svarg::error_code parse(std::string_view content_type, std::string_view data, T& out);
};

template <>
struct svarg::cxx_support::media_codec<"application/xml"> : my_xml_codec {};
template <>
struct svarg::cxx_support::media_codec<"+xml"> : my_xml_codec {};  // любой ...+xml
```

Кодек выбирается так: `media_codec<медиа тип>`, затем `media_codec<суффикс>`, затем встроенный. Подключение — как
выше, через `--file-prefix`. Для обхода полей объекта в кодеке есть `FOR_EACH_FIELD_INDEX_OF(T)`,
`get_field_name<I, T>()`, `get_field<I>(obj)`, пример — `test/generated_api/test_codecs.hpp`.

## Потоковый медиа тип

Тело ответа потокового медиа типа не собирается целиком: сервер отправляет его частями, клиент получает частями
в колбэк. По умолчанию потоковый только `application/octet-stream`, свои добавляются опцией:

```cmake
svarg_generate_cxx(... STREAM_CONTENT_TYPES text/event-stream application/x-ndjson)
```

Как отправлять и принимать — в [клиенте](client.md#ответы-потоком).
