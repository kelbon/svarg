"""
CLI-интерфейс генератора.

Использование:

    python -m codegen.cli \\
        --ir          path/to/ir.yaml \\
        --templates   path/to/templates/ \\
        --output      path/to/output/ \\
        [--var key=value ...]   # extra_context для шаблонов
        [--verbose]

Или через точку входа (если установлен через pip):

    codegen --ir ... --templates ... --output ...
"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

import yaml  # PyYAML

from .generator import Generator, GeneratorError


def _parse_var(s: str) -> tuple[str, str]:
    """Разбирает 'key=value' в кортеж."""
    if "=" not in s:
        raise argparse.ArgumentTypeError(
            f"--var must be in 'key=value' format, got: {s!r}"
        )
    key, _, value = s.partition("=")
    return key.strip(), value


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="codegen",
        description="svarg jinja2 backend",
    )
    p.add_argument(
        "--ir",
        required=True,
        metavar="PATH",
        help="Path to the IR YAML/JSON file (IrObject)",
    )
    p.add_argument(
        "--templates",
        required=True,
        metavar="DIR",
        help="Path to the templates directory (must contain operation/, schema/, ...)",
    )
    p.add_argument(
        "--output",
        required=True,
        metavar="DIR",
        help="Path to the output directory (will be created if missing)",
    )
    p.add_argument(
        "--var",
        dest="vars",
        metavar="KEY=VALUE",
        action="append",
        default=[],
        type=_parse_var,
        help=(
            "Extra variable to pass to all templates. "
            "Can be specified multiple times: --var ns=my_api --var version=2"
        ),
    )
    p.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Enable debug logging",
    )
    return p


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s: %(message)s",
    )

    ir_path = Path(args.ir)
    templates_dir = Path(args.templates)
    output_dir = Path(args.output)

    # --- Валидация путей ---
    if not ir_path.exists():
        logging.error("IR file not found: %s", ir_path)
        return 1
    if not templates_dir.is_dir():
        logging.error("Templates directory not found: %s", templates_dir)
        return 1

    # --- Загрузка IR ---
    try:
        raw = yaml.safe_load(ir_path.read_text(encoding="utf-8"))
    except Exception as e:
        logging.error("Failed to load IR file '%s': %s", ir_path, e)
        return 1

    # Импортируем IrObject здесь, чтобы codegen.cli работал независимо от
    # конкретного размещения ir_types в проекте пользователя.
    # Пользователь должен убедиться, что ir_types в PYTHONPATH.
    try:
        from ir_types.ir_object import IrObject  # type: ignore[import]
    except ImportError as e:
        logging.error(
            "Could not import IrObject. Make sure 'ir_types' is in PYTHONPATH.\n%s", e
        )
        return 1

    try:
        ir = IrObject.from_dict(raw)
    except Exception as e:
        logging.error("Failed to deserialize IR: %s", e)
        return 1

    # --- Запуск генератора ---
    extra_context = dict(args.vars)

    try:
        generator = Generator(
            templates_dir=templates_dir,
            output_dir=output_dir,
            extra_context=extra_context,
        )
        results = generator.generate(ir)
    except GeneratorError as e:
        logging.error("%s", e)
        return 1
    except Exception as e:
        logging.exception("Unexpected error during generation: %s", e)
        return 1

    print(f"Done: {len(results)} file(s) written to '{output_dir}'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
