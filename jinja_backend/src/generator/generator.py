"""
Ядро кодогенератора.

Читает IrObject (уже десериализованный), обходит все сущности и для каждой
запускает все шаблоны из соответствующей папки. Каждый шаблон обязан
выставить переменную output_path (или none для пропуска).

Структура папки шаблонов:

    templates_dir/
        operation/      # шаблоны для каждой Operation и (при fallback) webhook
            header.j2
            source.j2
        schema/         # шаблоны для каждой Schema
            types.j2
        webhook/        # опционально: если папка есть и не пуста,
            handler.j2  # webhooks рендерятся здесь; иначе fallback на operation/

Каждый .j2-файл:
1. Обязан установить {% set output_path = "<relative/path>" %}
   или {% set output_path = none %} чтобы пропустить генерацию файла.
2. Тело шаблона целиком становится содержимым выходного файла.
3. output_path интерпретируется относительно output_dir.
4. Несколько рендеров с одинаковым output_path конкатенируются в один файл
   в порядке рендера; перед первой записью существующий файл удаляется.

При ошибке (рендер или запись) все файлы, к которым прикоснулся этот запуск,
удаляются (если debug=False). Это гарантирует атомарность на уровне запуска:
либо всё успешно, либо output_dir не содержит частичного результата.
"""

from __future__ import annotations

import logging
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Generator as TypingGenerator, Iterator

from jinja2 import (
    Environment,
    StrictUndefined,
    TemplateError,
    FileSystemLoader,
)

from .helpers import FILTERS, GLOBALS

logger = logging.getLogger(__name__)

# Sentinel: атрибут output_path вообще не был выставлен в шаблоне
_MISSING = object()


# ---------------------------------------------------------------------------
# Исключения
# ---------------------------------------------------------------------------


class GeneratorError(Exception):
    """Базовый класс ошибок генератора."""


class MissingOutputPathError(GeneratorError):
    """Шаблон не выставил переменную output_path."""

    def __init__(self, template_file: Path, entity_repr: str) -> None:
        super().__init__(
            f"Template '{template_file}' did not set 'output_path' "
            f"while processing: {entity_repr}\n"
            f"Add {{% set output_path = ... %}} at the top of the template.\n"
            f"Use {{% set output_path = none %}} to skip file generation."
        )
        self.template_file = template_file
        self.entity_repr = entity_repr


# ---------------------------------------------------------------------------
# Результат одного рендера (публичный)
# ---------------------------------------------------------------------------


@dataclass
class RenderedFile:
    """Один выходной файл, записанный генератором."""

    output_path: Path    # абсолютный путь (output_dir / relative)
    content: str
    template_file: Path  # шаблон, из которого получен файл
    entity_repr: str     # читабельное описание сущности


# ---------------------------------------------------------------------------
# Внутренний результат рендера (может быть skip)
# ---------------------------------------------------------------------------


@dataclass
class _RenderResult:
    output_path: Path | None  # None → {% set output_path = none %}
    content: str
    template_file: Path
    entity_repr: str

    @property
    def is_skip(self) -> bool:
        return self.output_path is None


# ---------------------------------------------------------------------------
# Jinja Environment
# ---------------------------------------------------------------------------


def _make_jinja_env(templates_dir: Path) -> Environment:
    env = Environment(
        loader=FileSystemLoader(str(templates_dir), followlinks=True),
        undefined=StrictUndefined,
        keep_trailing_newline=True,
        autoescape=False,
        trim_blocks=True,
        lstrip_blocks=True,
    )
    env.filters.update(FILTERS)
    env.globals.update(GLOBALS)
    return env


# ---------------------------------------------------------------------------
# Рендер одного шаблона
# ---------------------------------------------------------------------------


def _render_one(
    env: Environment,
    *,
    template_name: str,
    template_file: Path,
    output_dir: Path,
    context: dict[str, Any],
    entity_repr: str,
) -> _RenderResult:
    template = env.get_template(template_name)

    try:
        content = template.render(**context)
    except TemplateError as e:
        raise GeneratorError(
            f"Template error in '{template_file}' "
            f"while processing {entity_repr}: {e}"
        ) from e

    module = template.make_module(context)
    raw: Any = getattr(module, "output_path", _MISSING)

    if raw is _MISSING:
        raise MissingOutputPathError(template_file, entity_repr)

    if raw is None:
        logger.debug(
            "skipped (output_path=none): %s via '%s'", entity_repr, template_file.name
        )
        return _RenderResult(
            output_path=None,
            content=content,
            template_file=template_file,
            entity_repr=entity_repr,
        )

    relative = Path(str(raw))
    if relative.is_absolute():
        raise GeneratorError(
            f"output_path must be relative to output_dir, "
            f"got absolute path: '{relative}' "
            f"(template '{template_file}', entity {entity_repr})"
        )

    return _RenderResult(
        output_path=output_dir / relative,
        content=content,
        template_file=template_file,
        entity_repr=entity_repr,
    )


# ---------------------------------------------------------------------------
# Cleanup при ошибке
# ---------------------------------------------------------------------------


@contextmanager
def _cleanup_on_error(touched: set[Path], debug: bool) -> Iterator[None]:
    """
    При исключении удаляет все пути из touched (если debug=False).
    touched заполняется извне по мере того, как файлы создаются/изменяются.
    """
    try:
        yield
    except Exception:
        if debug:
            logger.debug(
                "debug=True: leaving %d touched file(s) on disk after error",
                len(touched),
            )
        else:
            for path in touched:
                try:
                    path.unlink(missing_ok=True)
                    logger.debug("cleanup: removed '%s'", path)
                except OSError as e:
                    logger.warning("cleanup: could not remove '%s': %s", path, e)
        raise


# ---------------------------------------------------------------------------
# Запись одного результата на диск (вызывается сразу после рендера)
# ---------------------------------------------------------------------------


def _write_one(
    result: _RenderResult,
    *,
    touched: set[Path],
    written_in_run: set[Path],
) -> RenderedFile | None:
    """
    Записывает или дописывает один _RenderResult на диск.

    touched        — множество путей этого запуска для cleanup; пополняется здесь.
    written_in_run — пути, куда уже начали писать в этом запуске (для append).

    Возвращает RenderedFile или None если skip.
    """
    if result.is_skip:
        return None

    path = result.output_path  # type: ignore[assignment]
    path.parent.mkdir(parents=True, exist_ok=True)

    if path not in written_in_run:
        # Первая запись в этот путь: удаляем старый файл чтобы не дописывать к нему.
        if path.exists():
            path.unlink()
            logger.debug("removed existing file before overwrite: '%s'", path)
        touched.add(path)
        path.write_text(result.content, encoding="utf-8")
        written_in_run.add(path)
        logger.info("wrote '%s'  ← %s", path, result.entity_repr)
    else:
        with path.open("a", encoding="utf-8") as f:
            f.write(result.content)
        logger.info("appended '%s'  ← %s", path, result.entity_repr)

    return RenderedFile(
        output_path=path,
        content=result.content,
        template_file=result.template_file,
        entity_repr=result.entity_repr,
    )


# ---------------------------------------------------------------------------
# Главный класс генератора
# ---------------------------------------------------------------------------


@dataclass
class Generator:
    """
    Основной объект кодогенератора.

    Параметры:
        templates_dir  — корень папки шаблонов (содержит operation/, schema/, ...)
        output_dir     — куда писать результаты
        extra_context  — дополнительные переменные для всех шаблонов
                         (например, {"namespace": "my_api"})
        debug          — если True, при ошибке файлы НЕ удаляются;
                         удобно для анализа частично сгенерированного вывода
    """

    templates_dir: Path
    output_dir: Path
    extra_context: dict[str, Any] = field(default_factory=dict)
    debug: bool = False

    def __post_init__(self) -> None:
        self.templates_dir = Path(self.templates_dir)
        self.output_dir = Path(self.output_dir)
        self._env = _make_jinja_env(self.templates_dir)

    # ------------------------------------------------------------------
    # Публичный API
    # ------------------------------------------------------------------

    def generate(self, ir: Any) -> list[RenderedFile]:
        """
        Запускает генерацию по IrObject.

        Рендеринг и запись чередуются: каждый шаблон рендерится и сразу
        записывается на диск. Это гарантирует, что touched актуален в момент
        любой ошибки — как рендера, так и записи.

        При ошибке (если debug=False) все затронутые файлы удаляются.
        """
        touched: set[Path] = set()           # файлы этого запуска для cleanup
        written_in_run: set[Path] = set()    # для append-семантики
        written: list[RenderedFile] = []

        with _cleanup_on_error(touched, self.debug):
            for result in self._render_all(ir):
                rf = _write_one(result, touched=touched, written_in_run=written_in_run)
                if rf is not None:
                    written.append(rf)

        logger.info(
            "generation complete: %d file(s) written to '%s'",
            len(written),
            self.output_dir,
        )
        return written

    # ------------------------------------------------------------------
    # Внутренние методы
    # ------------------------------------------------------------------

    def _render_all(self, ir: Any) -> TypingGenerator[_RenderResult, None, None]:
        """Генератор: последовательно выдаёт _RenderResult для всех сущностей."""
        yield from self._render_list("schema",    ir.schemas    or [])
        yield from self._render_list("operation", ir.operations or [])
        yield from self._render_webhooks(ir.webhooks or [])

    def _template_files_for(self, kind: str) -> list[Path]:
        """Отсортированные .j2-файлы из templates_dir/<kind>/. Пусто если нет папки."""
        kind_dir = self.templates_dir / kind
        if not kind_dir.is_dir():
            return []
        return sorted(kind_dir.glob("*.j2"))

    def _render_webhooks(
        self, webhooks: list[Any]
    ) -> TypingGenerator[_RenderResult, None, None]:
        """
        Рендерит webhooks.

        Если templates_dir/webhook/ есть и не пуста — использует её
        (context_key="webhook").

        Иначе fallback на templates_dir/operation/ (context_key="operation");
        ir_kind остаётся "webhook" в обоих случаях, так что шаблон может
        различить их через {{ ir_kind }}.
        """
        if not webhooks:
            return

        webhook_templates = self._template_files_for("webhook")
        if webhook_templates:
            yield from self._render_entities(
                template_files=webhook_templates,
                entities=webhooks,
                context_key="webhook",
                ir_kind="webhook",
            )
            return

        # Fallback на operation/
        operation_templates = self._template_files_for("operation")
        if not operation_templates:
            logger.debug(
                "no templates for webhooks (neither webhook/ nor operation/) — "
                "skipping %d webhook(s)", len(webhooks),
            )
            return

        logger.debug(
            "webhook/ templates not found, falling back to operation/ "
            "for %d webhook(s)", len(webhooks),
        )
        yield from self._render_entities(
            template_files=operation_templates,
            entities=webhooks,
            context_key="operation",
            ir_kind="webhook",
        )

    def _render_list(
        self, kind: str, entities: list[Any]
    ) -> TypingGenerator[_RenderResult, None, None]:
        """Рендерит список сущностей через шаблоны из папки kind/."""
        template_files = self._template_files_for(kind)
        if not template_files:
            if entities:
                logger.debug(
                    "no templates in '%s/%s/' — skipping %d entity(ies)",
                    self.templates_dir, kind, len(entities),
                )
            return
        yield from self._render_entities(
            template_files=template_files,
            entities=entities,
            context_key=kind,
            ir_kind=kind,
        )

    def _render_entities(
        self,
        *,
        template_files: list[Path],
        entities: list[Any],
        context_key: str,
        ir_kind: str,
    ) -> TypingGenerator[_RenderResult, None, None]:
        """Для каждой сущности запускает каждый шаблон и выдаёт _RenderResult."""
        for entity in entities:
            entity_repr = _entity_repr(ir_kind, entity)
            for tmpl_file in template_files:
                template_name = tmpl_file.relative_to(self.templates_dir).as_posix()
                context: dict[str, Any] = {
                    context_key: entity,
                    "ir_kind": ir_kind,
                    **self.extra_context,
                }
                result = _render_one(
                    self._env,
                    template_name=template_name,
                    template_file=tmpl_file,
                    output_dir=self.output_dir,
                    context=context,
                    entity_repr=entity_repr,
                )
                if not result.is_skip:
                    logger.debug(
                        "rendered %s -> '%s'", entity_repr, result.output_path
                    )
                yield result


# ---------------------------------------------------------------------------
# Утилиты
# ---------------------------------------------------------------------------


def _entity_repr(kind: str, entity: Any) -> str:
    for attr in ("id", "name", "spec"):
        val = getattr(entity, attr, None)
        if val:
            return f"{kind}({attr}={val!r})"
    return f"{kind}(<unknown>)"
