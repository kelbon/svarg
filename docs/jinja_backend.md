# jinja backend

[jinja_backend](../jinja_backend/) читает svarg IR и рендерит по нему свои jinja2 шаблоны. Сам ни в какой язык не
генерирует

```
cd jinja_backend/src
python -m generator.cli --ir api.yaml --templates templates/ --output out/ [--var ns=my_api] [--debug]
```
Нужны jinja2 и pyyaml

- `--ir` - svarg IR (YAML или JSON), например вывод svarg-ir
- `--var key=value` - переменная во всех шаблонах, можно несколько раз
- `--debug` - при ошибке не удалять файлы. По умолчанию удаляются все записанные этим запуском

## Шаблоны

Бекенд использует эти определяемые пользователем файлы:
```
templates/
  schema/      # каждый .j2 рендерится для каждой схемы
  operation/   # для каждой операции
  webhook/     # для каждого вебхука. Если папки нет, вебхуки рендерятся шаблонами operation/
  filters.py   # необязательно: FILTERS = {"имя": функция}
  globals.py   # необязательно: GLOBALS = {"имя": значение}
```

Каждый шаблон ровно один раз вызывает `set_output_path(путь)`, путь относительно `--output`.
`set_output_path(none)` - файл не создаётся. Рендеры в один путь дописываются в один файл по порядку

```jinja
{{ set_output_path("handlers/" ~ (operation.id | snake_case) ~ ".py") }}
def {{ operation.id | snake_case }}(request):
    ...
```

В шаблоне доступны:

- `schema`, `operation` или `webhook` - сущность [svarg IR](svarg_ir.md). Вебхук, отрендеренный шаблоном из
  operation/, называется `operation`
- `ir_kind` - `"schema"`, `"operation"` или `"webhook"`
- переменные из `--var`
- фильтры и одноимённые функции `snake_case`, `camel_case`, `pascal_case`, `upper_case`
- фильтр `indent(width=4, first=False)`, пустые строки не получают отступ

Свои фильтры и функции с теми же именами перекрываются встроенными. Обращение к неопределённой переменной - ошибка
