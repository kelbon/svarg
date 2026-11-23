## Форматы

`format` применим к `string`, `integer`, `number` или `object` и передаётся в `FORMATS` (`svarg_generate_cxx`) в таком формате:

`<тип>+<формат>=<C++ тип>`

Например:

`string+date=my::date`

`object+abc=my::abc` - схема объекта рендерится в `my::abc` как есть. Остальные свойства в этом случае игнорируются

Пример:

```yaml
  # FORMATS object+point=my::point
  Point:
    type: object
    format: point
  # в С++ коде будет `using Point = my::point`
```

По умолчанию поддерживается: `int32`, `int64`, `float`,`double`.

Для своего типа нужны специализации (для исключения проблем с ODR заголовок с ними лучше подключить через `FILE_PREFIX`):

```cpp
// текст: параметры, поля form, text/plain, части multipart
template <>
struct svarg::cxx_support::text_value<my::date> {
  static std::string serialize(const my::date&);
  static svarg::error_code parse(std::string_view, my::date&);
};
// JSON
template <>
struct svarg::to_json_value<my::date> {
  boost::json::value operator()(const my::date&) const;
};
template <>
struct svarg::from_json_value<my::date> {
  svarg::error_code operator()(const boost::json::value&, my::date&) const;
};
```

Тела `application/json` пишутся сразу в буфер, без `boost::json::value`: значение обходится событиями `to_tokens` (см. `to_tokens.hpp`), JSON из них пишет `json_writer`. Тип без своей специализации `to_tokens` обходится через `to_json_value`, это медленнее. Обход напрямую:

```cpp
template <>
struct svarg::cxx_support::to_tokens<my::date> {
  static void operator()(const my::date& v, auto& w) {
    w.string(svarg::cxx_support::text_value<my::date>::serialize(v));
  }
};
```

Пользовательские форматы отключают проверки на `minimum`, `pattern` и тому подобное
