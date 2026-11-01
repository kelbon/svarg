"""
Jinja-хелперы для кодогенерации.

Регистрируются в Jinja Environment как фильтры и глобальные функции.
Пользователь обращается к ним прямо из шаблона:

    {{ operation.id | snake_case }}
    {{ schema.name | pascal_case }}
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


# ---------------------------------------------------------------------------
# Публичные фильтры — имена в разных стилях (для любого целевого языка)
# ---------------------------------------------------------------------------


def snake_case(s: str) -> str:
    """Конвертирует строку в snake_case."""
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


class GeneratorError(Exception):
    """Базовый класс ошибок генератора."""

# маркер не выставленного в шаблоне output_path
_MISSING = object()
_svarg_output_path = _MISSING
# проверки в этом модуле, чтобы глобал не экспортировать (он станет глобалом другого модуля - другой переменной)
def _reset_output_path() -> None:
    global _svarg_output_path
    _svarg_output_path = _MISSING

def _is_output_path_missing() -> bool:
    global _svarg_output_path
    return _svarg_output_path is _MISSING

def _is_output_path_setted_by_user() -> bool:
    global _svarg_output_path
    return _svarg_output_path is not None

def _set_output_path(s: str | None) -> str:
    global _svarg_output_path
    if _svarg_output_path is not _MISSING:
        raise GeneratorError(f'output_path setted twice in template!')
    _svarg_output_path = s
    return "" # возвращаем пустоту чтобы не влиять на вывод генератора

def _get_output_path() -> str:
    global _svarg_output_path
    return str(_svarg_output_path)

# ---------------------------------------------------------------------------
# Реестр — что регистрируется в Jinja Environment
# ---------------------------------------------------------------------------

#: Фильтры, добавляемые в env.filters
FILTERS: dict[str, object] = {
    "snake_case": snake_case,
    "camel_case": camel_case,
    "pascal_case": pascal_case,
    "upper_case": upper_case,
    "indent": indent_filter,  # переопределяет встроенный jinja indent для единообразия
}

#: Глобальные функции, добавляемые в env.globals (доступны без | синтаксиса)
GLOBALS: dict[str, object] = {
    # специальный фильтр который каждый шаблон должен использовать один раз
    # выставляет путь относительно output директории куда писать результат рендера
    "set_output_path": _set_output_path,
    "snake_case": snake_case,
    "camel_case": camel_case,
    "pascal_case": pascal_case,
    "upper_case": upper_case,
}
