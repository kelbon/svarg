"""
Дополнительные фильтры для jinja2-окружения кодогенератора C++.

Регистрация:

    from codegen import filters
    env.filters.update(filters.FILTERS)

Фильтры намеренно работают через duck typing (`type(x).__name__`,
`getattr`), чтобы НЕ импортировать модули `schema_types` / `operations`
и не завязываться на их путь. Передавать сюда нужно живые dataclass-объекты
(Schema, SchemaRef, ObjectFieldInfo, ...), а не их .to_dict()-представление.

Всё, что касается соответствия "тип IR -> тип C++", вынесено в CONFIG —
правьте его под свой проект (namespace, тип для `any`, контейнеры и т.п.).
"""

from __future__ import annotations

import re
from enum import Enum
from typing import Any


# ---------------------------------------------------------------------------
# Конфигурация маппинга на C++. Меняйте под свой проект.
# ---------------------------------------------------------------------------

CONFIG: dict[str, Any] = {
    "optional": "std::optional",
    "vector": "std::vector",
    "variant": "std::variant",
    "map": "std::map",
    "string": "std::string",
    # Тип для schema = `any` / meta / additionalProperties без схемы и т.п.
    "any_type": "nlohmann::json",
    # fundamental_e -> тип C++
    "fundamentals": {
        "null": "std::nullptr_t",
        "boolean": "bool",
        "integer": "std::int64_t",
        "number": "double",
        "string": "std::string",
        "any": "nlohmann::json",
        "never": "std::monostate",
    },
}

# @string@ / @integer@ / ... (spec фундаментального schema_ref) -> fundamental_e
_SPEC_TO_FUNDAMENTAL = {
    "@null@": "null",
    "@boolean@": "boolean",
    "@integer@": "integer",
    "@number@": "number",
    "@string@": "string",
    "@any@": "any",
    "@never@": "never",
}

# имя класса варианта schema_type.data -> короткий "kind" для dispatch в шаблоне
_KIND_BY_CLASS = {
    "ObjectSchema": "object",
    "ArraySchema": "array",
    "EnumSchema": "enum",
    "OneofSchema": "oneof",
    "AnyofSchema": "anyof",
    "AllofSchema": "allof",
    "NotSchema": "not",
}

# https://en.cppreference.com/w/cpp/keyword
_CPP_KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
    "bool", "break", "case", "catch", "char", "char8_t", "char16_t",
    "char32_t", "class", "compl", "concept", "const", "consteval",
    "constexpr", "constinit", "const_cast", "continue", "co_await",
    "co_return", "co_yield", "decltype", "default", "delete", "do", "double",
    "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
    "float", "for", "friend", "goto", "if", "inline", "int", "long",
    "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr",
    "operator", "or", "or_eq", "private", "protected", "public", "register",
    "reinterpret_cast", "requires", "return", "short", "signed", "sizeof",
    "static", "static_assert", "static_cast", "struct", "switch", "template",
    "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
    "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
    "wchar_t", "while", "xor", "xor_eq",
}


# ---------------------------------------------------------------------------
# Имена и идентификаторы
# ---------------------------------------------------------------------------

def _split_words(s: str) -> list[str]:
    """Разбивает строку на слова: режет по не-буквам/цифрам и по camelCase."""
    s = re.sub(r"[^0-9A-Za-z]+", " ", str(s))
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", s)  # fooBar -> foo Bar
    s = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", s)  # HTTPServer -> HTTP Server
    return [w for w in s.split() if w]


def to_pascal_case(s: str) -> str:
    words = _split_words(s)
    name = "".join(w[:1].upper() + w[1:] for w in words) or "Unnamed"
    if name[0].isdigit():
        name = "_" + name
    return name


def to_snake_case(s: str) -> str:
    words = _split_words(s)
    name = "_".join(w.lower() for w in words) or "field"
    if name[0].isdigit():
        name = "_" + name
    return name


def cpp_ident(s: str) -> str:
    """snake_case-идентификатор поля + защита от C++ keyword."""
    name = to_snake_case(s)
    if name in _CPP_KEYWORDS:
        name += "_"
    return name


def type_name_from_jsonptr(jp: Any) -> str:
    """json pointer вида /components/schemas/User -> User."""
    parts = [p for p in str(jp).split("/") if p]
    return to_pascal_case(parts[-1]) if parts else "Anonymous"


# ---------------------------------------------------------------------------
# Резолв типов
# ---------------------------------------------------------------------------

def schema_kind(data: Any) -> str:
    """Короткий тег варианта schema_type.data для dispatch в шаблоне."""
    if isinstance(data, Enum):  # FundamentalE
        return "fundamental"
    return _KIND_BY_CLASS.get(type(data).__name__, "unknown")


def cpp_schema_name(schema: Any) -> str:
    """Имя C++ типа для верхнеуровневой Schema (по name, иначе по jsonptr)."""
    name = getattr(schema, "name", None)
    if name:
        return to_pascal_case(name)
    return type_name_from_jsonptr(getattr(schema, "jsonptr", None))


def cpp_type_for_ref(ref: Any) -> str:
    """SchemaRef -> имя/тип C++.

    Порядок: фундаментальный тип (@string@ и т.п.) -> name -> jsonptr -> spec.
    """
    spec = getattr(ref, "spec", None)
    fund = _SPEC_TO_FUNDAMENTAL.get(spec)
    if fund is not None:
        return CONFIG["fundamentals"][fund]
    name = getattr(ref, "name", None)
    if name:
        return to_pascal_case(name)
    jsonptr = getattr(ref, "jsonptr", None)
    if jsonptr:
        return type_name_from_jsonptr(jsonptr)
    return to_pascal_case(spec or "Unknown")


def cpp_fundamental(e: Any) -> str:
    """FundamentalE -> тип C++."""
    value = e.value if isinstance(e, Enum) else str(e)
    return CONFIG["fundamentals"].get(value, CONFIG["any_type"])


def cpp_optional(type_str: str) -> str:
    return f'{CONFIG["optional"]}<{type_str}>'


# ---------------------------------------------------------------------------
# Поля / параметры / тела / enum-значения
# ---------------------------------------------------------------------------

def cpp_field_type(field: Any) -> str:
    """ObjectFieldInfo -> тип C++ с учётом required (обёртка в optional<>)."""
    t = cpp_type_for_ref(field.type)
    if not getattr(field, "required", False):
        t = cpp_optional(t)
    return t


def _location_value(loc: Any) -> str:
    return getattr(loc, "value", loc)


def cpp_param_type(param: Any) -> str:
    """OperationParameter -> тип C++.

    path-параметры всегда обязательны; остальные — optional, если
    required не выставлен явно в True.
    """
    t = cpp_type_for_ref(param.schema)
    required = (getattr(param, "required", None) is True) or \
        _location_value(getattr(param, "location", None)) == "path"
    if not required:
        t = cpp_optional(t)
    return t


def cpp_body_type(body: Any) -> str:
    return cpp_type_for_ref(body.schema)


def _enum_identifier(value: Any) -> str:
    if isinstance(value, bool):
        return "k_true" if value else "k_false"
    if isinstance(value, int):
        return f"k_{value}" if value >= 0 else f"k_neg_{abs(value)}"
    ident = to_snake_case(str(value))
    if ident in _CPP_KEYWORDS:
        ident += "_"
    return ident


def cpp_enum_value(value: Any) -> str:
    """Значение из EnumSchema.values -> enumerator C++.

    enum class в C++ хранит только целочисленные значения, поэтому для int
    подставляем `= N`, а для строк/прочего генерируем идентификатор и
    оставляем исходное значение в комментарии.
    """
    ident = _enum_identifier(value)
    if isinstance(value, bool) or not isinstance(value, int):
        return f"{ident} /* {value!r} */"
    return f"{ident} = {value}"


# ---------------------------------------------------------------------------
# Имена для операций / статусов ответов
# ---------------------------------------------------------------------------

def cpp_operation_name(op: Any) -> str:
    op_id = getattr(op, "id", None)
    if op_id:
        return to_pascal_case(op_id)
    method = getattr(op, "method", "op")
    path = getattr(op, "path", "")
    return to_pascal_case(f"{method}_{path}")


def cpp_status_ident(resp: Any) -> str:
    """ResponseDesc -> имя поля в struct Responses."""
    status = getattr(resp, "status", None)
    s = status.to_string() if hasattr(status, "to_string") else str(status)
    if s == "default":
        return "default_response"
    return "response_" + s.lower()


FILTERS = {
    # имена
    "to_pascal_case": to_pascal_case,
    "to_snake_case": to_snake_case,
    "cpp_ident": cpp_ident,
    # резолв типов
    "schema_kind": schema_kind,
    "cpp_schema_name": cpp_schema_name,
    "cpp_type_for_ref": cpp_type_for_ref,
    "cpp_fundamental": cpp_fundamental,
    "cpp_optional": cpp_optional,
    # поля / параметры / тела / enum
    "cpp_field_type": cpp_field_type,
    "cpp_param_type": cpp_param_type,
    "cpp_body_type": cpp_body_type,
    "cpp_enum_value": cpp_enum_value,
    # операции
    "cpp_operation_name": cpp_operation_name,
    "cpp_status_ident": cpp_status_ident,
}
