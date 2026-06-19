"""
Jinja-хелперы для кодогенерации.

Регистрируются в Jinja Environment как фильтры и глобальные функции.
Пользователь обращается к ним прямо из шаблона:

    {{ operation.id | cpp_id }}
    {{ schema.name | cpp_id }}
    {{ "some string" | snake_case }}
"""

from __future__ import annotations

import re
import unicodedata


# ---------------------------------------------------------------------------
# Утилиты для работы со строками / идентификаторами
# ---------------------------------------------------------------------------


def _normalize_unicode(s: str) -> str:
    """Заменяет Unicode-символы их ASCII-транслитерацией где возможно."""
    return unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode("ascii")


def _split_words(s: str) -> list[str]:
    """
    Разбивает строку на слова по:
    - пробелам / знакам препинания / спецсимволам
    - camelCase / PascalCase границам
    - цифробуквенным границам

    Пример:
        "getUserById"  -> ["get", "User", "By", "Id"]
        "some-string"  -> ["some", "string"]
        "HTTP_STATUS"  -> ["HTTP", "STATUS"]
        "addr2line"    -> ["addr", "2", "line"]
    """
    # Вставляем разделители на camelCase-границах
    s = re.sub(r"([a-z])([A-Z])", r"\1_\2", s)
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", s)
    # Отделяем цифры от букв
    s = re.sub(r"([a-zA-Z])([0-9])", r"\1_\2", s)
    s = re.sub(r"([0-9])([a-zA-Z])", r"\1_\2", s)
    # Разбиваем по любым небуквенным/нецифровым символам
    words = re.split(r"[^a-zA-Z0-9]+", s)
    return [w for w in words if w]


# Зарезервированные ключевые слова C++17
_CPP_KEYWORDS: frozenset[str] = frozenset(
    {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
        "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
        "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected",
        "public", "register", "reinterpret_cast", "requires", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
        "switch", "template", "this", "thread_local", "throw", "true", "try",
        "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
        "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
        # не ключевые, но зарезервированные в глобальном namespace
        "NULL", "EOF",
    }
)


def _sanitize_cpp_keyword(name: str) -> str:
    """Если имя совпадает с C++ keyword, добавляет суффикс _."""
    if name in _CPP_KEYWORDS:
        return name + "_"
    return name


# ---------------------------------------------------------------------------
# Публичные фильтры — идентификаторы
# ---------------------------------------------------------------------------


def cpp_id(s: str) -> str:
    """
    Превращает произвольную строку в валидный C++ идентификатор в snake_case.

        "getUserById"   -> "get_user_by_id"
        "Content-Type"  -> "content_type"
        "404response"   -> "response_404"  (не может начинаться с цифры)
        "class"         -> "class_"        (ключевое слово)
        ""              -> "_"             (пустая строка)
    """
    s = _normalize_unicode(s)
    words = _split_words(s)
    if not words:
        return "_"
    result = "_".join(w.lower() for w in words)
    # Идентификатор не может начинаться с цифры
    if result[0].isdigit():
        result = "_" + result
    return _sanitize_cpp_keyword(result)


def cpp_type_id(s: str) -> str:
    """
    Превращает произвольную строку в валидный C++ идентификатор в PascalCase —
    удобно для имён типов/классов/структур.

        "getUserById"  -> "GetUserById"
        "user_profile" -> "UserProfile"
        "HTTP_client"  -> "HttpClient"
    """
    s = _normalize_unicode(s)
    words = _split_words(s)
    if not words:
        return "_"
    result = "".join(w.capitalize() for w in words)
    if result[0].isdigit():
        result = "_" + result
    return _sanitize_cpp_keyword(result)


def snake_case(s: str) -> str:
    """Конвертирует строку в snake_case (без C++-специфики)."""
    s = _normalize_unicode(s)
    words = _split_words(s)
    if not words:
        return ""
    return "_".join(w.lower() for w in words)


def camel_case(s: str) -> str:
    """Конвертирует строку в camelCase."""
    s = _normalize_unicode(s)
    words = _split_words(s)
    if not words:
        return ""
    return words[0].lower() + "".join(w.capitalize() for w in words[1:])


def pascal_case(s: str) -> str:
    """Конвертирует строку в PascalCase."""
    s = _normalize_unicode(s)
    words = _split_words(s)
    if not words:
        return ""
    return "".join(w.capitalize() for w in words)


def upper_case(s: str) -> str:
    """Конвертирует строку в UPPER_SNAKE_CASE."""
    s = _normalize_unicode(s)
    words = _split_words(s)
    if not words:
        return ""
    return "_".join(w.upper() for w in words)


# ---------------------------------------------------------------------------
# Публичные фильтры — разное
# ---------------------------------------------------------------------------


def indent_filter(s: str, width: int = 4, first: bool = False) -> str:
    """
    Добавляет отступ к каждой строке многострочного текста.
    Параметр first=True включает отступ для первой строки тоже.

    Удобен для вставки блоков кода внутрь других блоков:
        {{ body | indent(4) }}
    """
    prefix = " " * width
    lines = s.splitlines(keepends=True)
    result = []
    for i, line in enumerate(lines):
        if i == 0 and not first:
            result.append(line)
        else:
            # Не добавляем отступ к пустым строкам
            result.append(prefix + line if line.strip() else line)
    return "".join(result)


# ---------------------------------------------------------------------------
# Реестр — что регистрируется в Jinja Environment
# ---------------------------------------------------------------------------

#: Фильтры, добавляемые в env.filters
FILTERS: dict[str, object] = {
    "cpp_id": cpp_id,
    "cpp_type_id": cpp_type_id,
    "snake_case": snake_case,
    "camel_case": camel_case,
    "pascal_case": pascal_case,
    "upper_case": upper_case,
    "indent": indent_filter,  # переопределяет встроенный jinja indent для единообразия
}

#: Глобальные функции, добавляемые в env.globals (доступны без | синтаксиса)
GLOBALS: dict[str, object] = {
    "cpp_id": cpp_id,
    "cpp_type_id": cpp_type_id,
    "snake_case": snake_case,
    "camel_case": camel_case,
    "pascal_case": pascal_case,
    "upper_case": upper_case,
}
