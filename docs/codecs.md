# Тела и кодеки

## Встроенные форматы

Тело запроса или ответа кодируется по своему медиа типу из спеки:

| Медиа тип | Кодек | Схема |
|---|---|---|
| `application/json`, любой `...+json` (`problem+json`, `merge-patch+json`, `vnd.x+json`) | `json_codec` | любая |
| `application/x-www-form-urlencoded` | `form_codec` | объект |
| `multipart/*` | `multipart_codec` | объект, свойство — часть |
| всё остальное (`text/plain`, `application/octet-stream`, `application/vnd.3gpp.ngap`...) | `raw_codec` | строка, число, bool или енам, значение как есть |

Те же кодеки используются для частей multipart, полей form и параметров с `content:`.

Схема, не подходящая к формату (например объект с `text/plain`), — ошибка компиляции.

## Свой медиа тип

Кодек — структура с двумя статическими функциями:

```cpp
struct my_xml_codec {
  // `media_type` - без параметров; в `params` можно добавить параметры хедера content-type;
  // данные дописываются в конец `out`
  template <typename T>
  static void serialize(const T& value, std::string_view media_type, svarg::content_type_params& params,
                        hidi::http_body_bytes& out);
  // `content_type` - значение хедера целиком, с параметрами
  template <typename T>
  static svarg::error_code parse(std::string_view content_type, std::string_view data, T& out);
};
```

Подключается специализацией `media_codec` для медиа типа или суффикса:

```cpp
template <>
struct svarg::cxx_support::media_codec<"application/xml"> : my_xml_codec {};

template <>
struct svarg::cxx_support::media_codec<"+xml"> : my_xml_codec {};  // любой ...+xml
```

Порядок выбора: `media_codec<медиа тип>`, `media_codec<суффикс>`, встроенный.

Специализация должна быть видна во всех сгенерированных файлах, иначе разные единицы трансляции выберут разные
кодеки. Поэтому заголовок с ней подключается опцией генератора:

```
svarg --file-prefix '#include "my_codecs.hpp"' ...
```

Для обхода полей сгенерированных объектов в своём кодеке есть `FOR_EACH_FIELD_INDEX_OF(T)`,
`get_field_name<I, T>()`, `get_field<I>(obj)`, `is_field_present<I>(obj)`, `get_field_index<T>(name)` — пример
в `test/generated_api/test_codecs.hpp`.
