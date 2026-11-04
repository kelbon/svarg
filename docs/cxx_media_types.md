## Переиспользование парсеров и сериализаторов другого медиа типа

```cpp
// указывает, что "application/vnd.github.object" ведёт себя как "application/json"
template <>
struct svarg::cxx_support::media_type_base<"application/vnd.github.object"> {
  static constexpr std::string_view value = "application/json";
};
// указывает что все кодеки заканчивающиеся на "+kv" ведут себя как "application/json"
template <>
struct svarg::cxx_support::media_type_base<"+kv"> {
  static constexpr std::string_view value = "application/json";
};
// по умолчанию уже +json, +xml кодеки ссылаются на json/xml соответственно
```

Специализации должны быть видны во всех сгенерированных файлах, не забывайте добавить их в `FILE_PREFIX`

## Свой формат

Кодек - структура с двумя функциями:

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
```

Кодек выбирается так: `media_codec<медиа тип>`, затем кодек базового медиа типа (`media_type_base` медиа
типа, затем его суффикса), затем встроенный. Медиа типы - в нижнем регистре. Для обхода полей объекта в кодеке есть `FOR_EACH_FIELD_INDEX_OF(T)`,
`get_field_name<I, T>()`, `get_field<I>(obj)`, пример - `test/generated_api/test_codecs.hpp`.

## Потоковый медиа тип

Потоковый запрос или нет определяется по медиа типу. По умолчанию потоковый только `application/octet-stream`, свои добавляются опцией `STREAM_CONTENT_TYPES`


Пример потоковой отправки и приёма можно найти [здесь для клиента](../examples/cxx_stream_client/) и [здесь для сервера](../examples/cxx_stream_server/)
