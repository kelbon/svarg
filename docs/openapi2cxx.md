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
    FILE_PREFIX          ...         # --file-prefix: свои кодеки, типы форматов и хуки
    SOURCES_PER_SPEC     ...         # --sources-per-spec
)
target_link_libraries(my_app PRIVATE api_generated)
```

Несколько значений перечисляются через пробел, значение с пробелом - в кавычках:

```cmake
svarg_generate_cxx(
    TARGET               shop_api
    SPECS_DIR            ${CMAKE_CURRENT_SOURCE_DIR}/openapi
    OUT_DIR              ${CMAKE_CURRENT_BINARY_DIR}/generated
    SPECS                users.yaml orders.yaml admin/*  # admin/* - все спеки в openapi/admin
    INCLUDE_DIRS         ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/third_party/specs
    STREAM_CONTENT_TYPES text/event-stream application/x-ndjson
    FORMATS              string+date=my::date string+uuid=my::uuid "integer+unix-time=std::chrono::sys_seconds"
    FILE_PREFIX          "#include \"my_codecs.hpp\""  # одна строка: несколько заголовков подключите из него
    SOURCES_PER_SPEC     4
)
```

Функция `svarg_generate_cxx` сама заботится о выставлении зависимостей, списке сгенерированных файлов, корректной перегенерации после изменения спек, при этом точно отслеживает изменение какой спеки действительно влияет на генерацию

## Поддержка OpenAPI

 ✔ любые `$ref` и количество файлов спек, `$ref` с дополнительными полями

 ✔ `anyOf`, `oneOf`, `allOf`, `not`. Поддержано поведение `allOf` как "наследования" или объединения схем

 ✔ OAuth2 и другие виды авторизации (генерируются в С++ как необязательные параметры - проверять их задача сервера, удобно в [хуках](cxx_client.md#хуки))

 ✔ рекурсивные схемы

 ✔ format - в том числе [любые пользовательские](cxx_format.md)

 ✔ свои [медиа типы](cxx_media_types.md) (content-type)

 ✔ `path`, `header`, `cookie`, `query` параметры

 ✔ `style` и `explode` параметров: `simple`, `form`, `label`, `matrix`, `spaceDelimited`, `pipeDelimited`, `deepObject`

 ✔ default ответы

 ✔ ограничения `minLength`/`maxLength`, `pattern` (ECMAScript), `minimum`/`maximum`/`exclusive*`, `multipleOf`, `minItems`/`maxItems`, `uniqueItems`, `minProperties`/`maxProperties`. Опция `--ignore-requirements` отключает проверки

 ✔ сложные схемы с только required и другие  способы выразить требование схемой

 ✔ `type: ["string", "null"]`

 ✔ `discriminator` и `mapping`

 ✔ `callbacks` и `webhooks` - генерируются как схемы, отправляются пользователем

 ✔ `nullable` при отправке ставит `null` пустому значению (при получении парсер считает его эквивалентным отсутствием поля)

 ✔ `additionalProperties`: `false`, `true` или схема

 ✔ схема указанная как `true`/`false` также поддерживается

 ✔ OpenAPI файлы в JSON формате

## Не поддержано

 ✘ Swagger 2.0 (можно сконвертировать в OpenAPI 3, например [swagger2openapi](https://github.com/Mermade/oas-kit))

 ✘ никем не поддерживаемые и не используемые `if`/`then`/`else`, `patternProperties` и т.п. в JSON Schema игнорируются

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

✔ HTTP/2

✔ SSL/TLS

✔ потоковая отправка сервером, потоковое получение данных клиентом

✔ для больших спек .cpp умеет делиться на любое нужное количество частей (`SOURCES_PER_SPEC`) для ускорения компиляции

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

svarg cli преобразует OpenAPI (YAML/JSON) спецификации в С++, svarg-ir преобразует только до уровня svarg IR, а svarg-ir-cxx преобразует IR -> C++ код

```
svarg -s <папка со спеками> -o <папка для кода> [опции] api.yaml [другие спеки...]
```

Спеки из командной строки ищутся в `-s` (затем в `-I`), `$ref` - относительно файла, в котором записаны

Опции, влияющие на сгенерированный код:

| Опция | По умолчанию | Что делает |
|---|---|---|
| `--optimize` (`-O2`) | `true` | упрощает схемы перед генерацией (меньше типов) |
| `--simple-identifiers` | `true` | false добавляет гарантию, что имена сгенерированных типов не пересекутся между собой |
| `--unsetted-additional-properties` | `true` | если `additionalProperties` не выставлен это true или false? |
| `--sources-per-spec N` | `1` | на сколько `.cpp` делится код каждой спеки, см. ниже |
| `--stream-content-type` | | потоковые медиа типы ответов |
| `--file-prefix` | | текст в начале каждого заголовка, например `#include` для своих кодеков и хуков |
| `--add-format` | | C++ тип формата: `string+date=my::date`, см. [форматы](cxx_format.md) |
| `--ignore-requirements` | | не проверять ограничения и требования схем: меньше кода, неполная проверка |
| `--allow-remove` | `only_inline_schemas` | какие схемы оптимизация может удалять: `noone`, `only_inline_schemas`, `any` |

Остальные опции:

| Опция | По умолчанию | Что делает |
|---|---|---|
| `--out` (`-o`) | | папка для сгенерированного кода |
| `--specs-dir` (`-s`) | текущая папка | папка со спеками |
| `--include-dirs` (`-I`) | | дополнительные папки для поиска спек |
| `--allow-network-refs` | `false` | `$ref` по URL (`https://...`), svarg должен быть собран с `svarg_ENABLE_NETWORK_REFS` |
| `--references-max-recursion-depth` | `50` | максимальная глубина вложенности `$ref` |
| `--depfile` | | записать депфайл: от каких прочитанных спек зависят сгенерированные `.cpp` (для `DEPFILE` в CMake) |
| `--dry-run` | | ничего не генерировать, только записать список файлов в `generated_files.txt` |
| `--help` (`-h`) | | список всех опций |
