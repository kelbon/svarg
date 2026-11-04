# Как использовать

Типичный проект на С++ использует svarg подключая через CPM:

```cmake
CPMAddPackage("gh:kelbon/svarg@1.0.0") # не забудьте указать версию

svarg_generate_cxx(
    TARGET               api_generated
    SPECS_DIR            ${CMAKE_CURRENT_SOURCE_DIR}/openapi
    OUT_DIR              ${CMAKE_CURRENT_BINARY_DIR}/generated
    SPECS                api.yaml
    # эти параметры опциональны, маппятся в CLI аргументы svarg, см. [svarg cli](openapi2cxx.md#cli-интерфейс-svarg)
    INCLUDE_DIRS         ...         # -I
    STREAM_CONTENT_TYPES ...         # --stream-content-type
    FORMATS              ...         # --add-format
    FILE_PREFIX          ...         # --file-prefix: свои кодеки и типы форматов
    SOURCES_PER_SPEC     ...         # --sources-per-spec
)
target_link_libraries(my_app PRIVATE api_generated)
```

Функция `svarg_generate_cxx` сама заботится о выставлении зависимостей, списке сгенерированных файлов, корректной перегенерации после изменения спек, при этом точно отслеживает изменение какой спеки действительно влияет на генерацию

## Поддержка OpenAPI

 ✔ любые `$ref` и количество файлов спек, `$ref` с дополнительными полями

 ✔ `anyOf`, `oneOf`, `allOf`, `not`. Поддержано поведение `allOf` как "наследования" или объединения схем

 ✔ OAuth2 и другие виды авторизации (генерируются в С++ как параметры требующие заполнения)

 ✔ рекурсивные схемы

 ✔ format - в том числе [любые пользовательские](cxx_format.md)

 ✔ свои [медиа типы](cxx_media_types.md) (content-type)

 ✔ ограничения `minLength`/`maxLength`, `pattern` (ECMAScript), `minimum`/`maximum`/`exclusive*`, `multipleOf`, `minItems`/`maxItems`, uniqueItems`, `minProperties`/`maxProperties`. Опция `--ignore-requirements` отключает проверки

 ✔ сложные схемы с только required и другие  способы выразить требование схемой

 ✔ `type: ["string", "null"]`

 ✔ `discriminator` и `mapping`

 ✔ `callbacks` и `webhooks` - генерируются как схемы, отправляются пользователем

 ✔ `nullable` при отправке ставит `null` пустому значению

 ✔ `additionalProperties`: `false`, `true` или схема

 ✔ схема указанная как `true`/`false` также поддерживается
 
 ✔ OpenAPI файлы в JSON формате

## Расширения

#### x-name-hint

Добавляет нормальное имя сгенерированной структуре

Пример:
```yaml
    OrderStatusEvent:
      type: object
      properties:
        status:
          type: string
          enum: [pending, shipped, delivered]
          x-name-hint: orderStatusEvent_e
```

## что поддерживает сгенерированный С++ код

Сгенерированные клиенты и серверы используют [hidi](https://github.com/kelbon/hidi)

- ✔ HTTP/2
- ✔ SSL/TLS
- ✔ потоковая отправка сервером, потоковое получение данных клиентом
- ✔ для больших спек .cpp умеет делиться на любое нужное количество частей (`SOURCES_PER_SPEC`) для ускорения компиляции

#### если вы получаете ошибки сборки

(скорее всего на windows) о неустановленном nasm или невыставленной переменной `CMAKE_ASM_*`:

Убедитесь что у вас установлен nasm - он нужен для openSSL

```cpp
choco install nasm
-- ИЛИ
winget install nasm
```

Если ошибка не ушла, то вероятно enable_language в CMake конфликтует с boost context (который мог попасть к вам через boost asio)

Решение (для windows):

```cmake
if (WIN32)
  set(BOOST_CONTEXT_IMPLEMENTATION winfib CACHE STRING "Boost.Context implementation (fcontext, ucontext, winfib)" FORCE)
endif()
```

## CLI интерфейс svarg

svarg cli преобразует OpenAPI (YAML/JSON) спецификации в С++, svarg-ir преобразует только до уровня svarg IR, а svarg-cxx преобразует IR -> C++ код

```
svarg -s <папка со спеками> -o <папка для кода> [опции] api.yaml [другие спеки...]
```

Спеки из командной строки ищутся в `-s` (затем в `-I`), `$ref` - относительно файла, в котором записаны

Опции, влияющие на сгенерированный код:

| Опция | По умолчанию | Что делает |
|---|---|---|
| `--optimize` (`-O2`) | `true` | упрощает схемы перед генерацией (меньше типов) |
| `--simple-identifiers` | `true` | режим C++ имён |
| `--unsetted-additional-properties` | `true` | незаданный `additionalProperties` |
| `--sources-per-spec N` | `1` | на сколько `.cpp` делится код каждой спеки, см. ниже |
| `--stream-content-type` | | потоковые медиа типы ответов |
| `--file-prefix` | | текст в начале каждого заголовка, например `#include` для своих специализаций парсинга |
| `--add-format` | | C++ тип формата: `string+date=my::date`, см. [форматы](schemas.md#форматы) |
| `--ignore-requirements` | | не проверять ограничения и требования схем: меньше кода, неполная проверка |
