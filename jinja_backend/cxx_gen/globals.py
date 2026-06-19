"""
Дополнительные глобалы для jinja2-окружения кодогенератора C++.

Регистрация:

    from codegen import globals as g
    env.globals.update(g.GLOBALS)

Здесь — то, что удобно звать в шаблоне как функцию/константу без `|`:
маппинг-конфиг, тип для `any`, и хелперы для сборки целого .hpp-файла
(баннер / include-и / открытие-закрытие namespace) поверх отдельных
определений, которые рендерят schema/operation шаблоны.
"""

from __future__ import annotations

from .filters import CONFIG


def any_type() -> str:
    """Тип C++ для schema `any` / additionalProperties без схемы / meta."""
    return CONFIG["any_type"]


def cpp_config() -> dict:
    return CONFIG


def generated_banner() -> str:
    return (
        "// =====================================================\n"
        "// Сгенерировано автоматически. Не редактировать вручную.\n"
        "// ====================================================="
    )


def default_includes() -> list[str]:
    """Базовый набор include-ов для сгенерированных типов."""
    return [
        "<cstdint>",
        "<map>",
        "<optional>",
        "<string>",
        "<variant>",
        "<vector>",
        "<nlohmann/json.hpp>",
    ]


def render_includes(includes: list[str] | None = None) -> str:
    incs = includes if includes is not None else default_includes()
    return "\n".join(f"#include {i}" for i in incs)


def open_namespace(ns: str) -> str:
    parts = [p for p in ns.split("::") if p]
    return "\n".join(f"namespace {p} {{" for p in parts)


def close_namespace(ns: str) -> str:
    parts = [p for p in ns.split("::") if p]
    return "\n".join(f"}}  // namespace {p}" for p in reversed(parts))


GLOBALS = {
    "any_type": any_type,
    "cpp_config": cpp_config,
    "generated_banner": generated_banner,
    "default_includes": default_includes,
    "render_includes": render_includes,
    "open_namespace": open_namespace,
    "close_namespace": close_namespace,
}
