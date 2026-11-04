## Форматы

`format` применим только к `string`, `integer` или `number` и передаётся в `FORMATS` (`svarg_generate_cxx`) в таком формате:

`<тип>+<формат>=<C++ тип>`

Например:

`string+date=my::date`

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

Пользовательские форматы отключают проверки на `minimum`, `pattern` и тому подобное
