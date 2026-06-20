"""
Дополнительные фильтры для jinja2-окружения кодогенератора C++.

Регистрация:
    from codegen import filters
    env.filters.update(filters.FILTERS)
"""

from __future__ import annotations

import re
from enum import Enum
from typing import Any


# ---------------------------------------------------------------------------
# Конфигурация маппинга на C++
# ---------------------------------------------------------------------------

CONFIG: dict[str, Any] = {
    "namespace": "cxxgen",
    "optional": "std::optional",
    "vector": "std::vector",
    "variant": "std::variant",
    "map": "std::map",
    "string": "std::string",
    "any_type": "nlohmann::json",
    "fundamentals": {
        "null":    "std::nullptr_t",
        "boolean": "bool",
        "integer": "std::int64_t",
        "number":  "double",
        "string":  "std::string",
        "any":     "nlohmann::json",
        "never":   "std::monostate",
    },
}

_SPEC_TO_FUNDAMENTAL = {
    "@null@":    "null",
    "@boolean@": "boolean",
    "@integer@": "integer",
    "@number@":  "number",
    "@string@":  "string",
    "@any@":     "any",
    "@never@":   "never",
}

_KIND_BY_CLASS = {
    "ObjectSchema": "object",
    "ArraySchema":  "array",
    "EnumSchema":   "enum",
    "OneofSchema":  "oneof",
    "AnyofSchema":  "anyof",
    "AllofSchema":  "allof",
    "NotSchema":    "not",
}

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
    s = re.sub(r"[^0-9A-Za-z]+", " ", str(s))
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", s)
    s = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", s)
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
    """snake_case + защита от ключевых слов C++."""
    name = to_snake_case(s)
    if name in _CPP_KEYWORDS:
        name += "_"
    return name


def type_name_from_jsonptr(jp: Any) -> str:
    parts = [p for p in str(jp).split("/") if p]
    return to_pascal_case(parts[-1]) if parts else "Anonymous"


# ---------------------------------------------------------------------------
# Резолв типов
# ---------------------------------------------------------------------------

def schema_kind(data: Any) -> str:
    if isinstance(data, Enum):
        return "fundamental"
    return _KIND_BY_CLASS.get(type(data).__name__, "unknown")


def cpp_schema_name(schema: Any) -> str:
    name = getattr(schema, "name", None)
    if name:
        return to_pascal_case(name)
    return type_name_from_jsonptr(getattr(schema, "jsonptr", None))


def cpp_type_for_ref(ref: Any) -> str:
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
    value = e.value if isinstance(e, Enum) else str(e)
    return CONFIG["fundamentals"].get(value, CONFIG["any_type"])


def cpp_optional(type_str: str) -> str:
    return f'{CONFIG["optional"]}<{type_str}>'


# ---------------------------------------------------------------------------
# Поля / параметры / тела / enum
# ---------------------------------------------------------------------------

def cpp_field_type(field: Any) -> str:
    t = cpp_type_for_ref(field.type)
    if not getattr(field, "required", False):
        t = cpp_optional(t)
    return t


def _location_value(loc: Any) -> str:
    return getattr(loc, "value", loc)


def cpp_param_type(param: Any) -> str:
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
    ident = _enum_identifier(value)
    if isinstance(value, bool) or not isinstance(value, int):
        return f"{ident} /* {value!r} */"
    return f"{ident} = {value}"


# ---------------------------------------------------------------------------
# Операции / ответы
# ---------------------------------------------------------------------------

def cpp_operation_name(op: Any) -> str:
    op_id = getattr(op, "id", None)
    if op_id:
        return to_pascal_case(op_id)
    method = getattr(op, "method", "op")
    path = getattr(op, "path", "")
    return to_pascal_case(f"{method}_{path}")


def cpp_status_ident(resp: Any) -> str:
    status = getattr(resp, "status", None)
    s = status.to_string() if hasattr(status, "to_string") else str(status)
    if s == "default":
        return "response_default"
    return "response_" + s.lower()


# ---------------------------------------------------------------------------
# Пути выходных файлов
# ---------------------------------------------------------------------------

def _unescape_jsonptr_segment(s: str) -> str:
    """RFC 6901: ~1 -> /, ~0 -> ~ (порядок важен)."""
    return s.replace("~1", "/").replace("~0", "~")


def _safe_fs_segment(s: str) -> str:
    """Делает из раскодированного сегмента JSON-указателя безопасный путь ФС."""
    # Слэши внутри сегмента (появляются после ~1-декодирования) -> _
    s = s.replace("/", "_").replace("\\", "_")
    # Убираем ведущие точки (скрытые файлы Unix)
    s = s.lstrip(".")
    return s or "_"


def schema_output_path(schema: Any) -> str:
    """Schema -> относительный путь .hpp файла.

    Структура: <spec>/<ptr-dirs...>/<name|last-segment>.hpp
    Пример:
      spec='petstore.yaml', jsonptr='/components/schemas/User', name='User'
      -> petstore.yaml/components/schemas/User/User.hpp

      spec='petstore.yaml', jsonptr='/components/schemas/User', name=None
      -> petstore.yaml/components/schemas/User.hpp

      jsonptr='/paths/~1pets~1{id}/get', name=None
      -> petstore.yaml/paths/_pets_{id}/get.hpp
    """
    spec    = str(getattr(schema, "spec", "unknown"))
    jsonptr = str(getattr(schema, "jsonptr", "") or "")
    name    = getattr(schema, "name", None)

    # Декодируем и санируем каждый сегмент указателя
    ptr_parts = [
        _safe_fs_segment(_unescape_jsonptr_segment(seg))
        for seg in jsonptr.strip("/").split("/")
        if seg
    ]

    if name:
        # Имя есть -> кладём рядом с последним сегментом указателя как отдельный файл
        filename = to_pascal_case(name) + ".hpp"
    elif ptr_parts:
        # Нет имени -> файл называется по последнему сегменту указателя
        filename = ptr_parts[-1] + ".hpp"
        ptr_parts = ptr_parts[:-1]
    else:
        filename = "schema.hpp"

    parts = [spec] + ptr_parts + [filename]
    return "/".join(parts)


def operation_output_path(op: Any) -> str:
    """Operation -> относительный путь .hpp файла.

    Структура: <spec>/operations/<OperationName>.hpp
    Если operation_id есть -> PascalCase от него.
    Иначе -> METHOD_path_slug.hpp
    """
    spec   = str(getattr(op, "spec", "unknown"))
    op_id  = getattr(op, "id", None)
    method = str(getattr(op, "method", "op"))
    path   = str(getattr(op, "path", ""))

    if op_id:
        filename = to_pascal_case(op_id) + ".hpp"
    else:
        path_slug = re.sub(r"[^0-9A-Za-z_]+", "_", path.strip("/")).strip("_")
        if path_slug:
            filename = f"{method.upper()}_{path_slug}.hpp"
        else:
            filename = f"{method.upper()}.hpp"

    return f"{spec}/operations/{filename}"


# ---------------------------------------------------------------------------
# cxxgen:: helpers (используются из шаблонов)
# ---------------------------------------------------------------------------

def cxxgen_ns() -> str:
    return CONFIG["namespace"]


FILTERS = {
    # имена
    "to_pascal_case":      to_pascal_case,
    "to_snake_case":       to_snake_case,
    "cpp_ident":           cpp_ident,
    # типы
    "schema_kind":         schema_kind,
    "cpp_schema_name":     cpp_schema_name,
    "cpp_type_for_ref":    cpp_type_for_ref,
    "cpp_fundamental":     cpp_fundamental,
    "cpp_optional":        cpp_optional,
    # поля / параметры / тела / enum
    "cpp_field_type":      cpp_field_type,
    "cpp_param_type":      cpp_param_type,
    "cpp_body_type":       cpp_body_type,
    "cpp_enum_value":      cpp_enum_value,
    # операции
    "cpp_operation_name":  cpp_operation_name,
    "cpp_status_ident":    cpp_status_ident,
    # пути выходных файлов
    "schema_output_path":  schema_output_path,
    "operation_output_path": operation_output_path,
}
