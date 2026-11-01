"""
Тесты для codegen.generator.

Используют простые mock-объекты вместо реальных IrObject,
чтобы тесты не зависели от ir_types.
"""

from __future__ import annotations

import textwrap
from dataclasses import dataclass, field
from pathlib import Path

import pytest

from generator import (
    Generator,
    GeneratorError,
    MissingOutputPathError,
    RenderedFile,
)


# ---------------------------------------------------------------------------
# Mock-объекты
# ---------------------------------------------------------------------------


@dataclass
class MockSchema:
    name: str
    spec: str = "test.yaml"


@dataclass
class MockOperation:
    id: str
    spec: str = "test.yaml"
    method: str = "get"
    path: str = "/test"


@dataclass
class MockIr:
    schemas: list = field(default_factory=list)
    operations: list = field(default_factory=list)
    webhooks: list = field(default_factory=list)


# ---------------------------------------------------------------------------
# Фикстуры
# ---------------------------------------------------------------------------


@pytest.fixture
def templates_dir(tmp_path: Path) -> Path:
    d = tmp_path / "templates"
    d.mkdir()
    (d / "operation").mkdir()
    (d / "schema").mkdir()
    (d / "webhook").mkdir()
    return d


@pytest.fixture
def output_dir(tmp_path: Path) -> Path:
    d = tmp_path / "output"
    d.mkdir()
    return d


@pytest.fixture
def generator(templates_dir: Path, output_dir: Path) -> Generator:
    return Generator(templates_dir=templates_dir, output_dir=output_dir)


def write_template(templates_dir: Path, kind: str, filename: str, content: str) -> None:
    (templates_dir / kind / filename).write_text(textwrap.dedent(content), encoding="utf-8")


# ---------------------------------------------------------------------------
# Базовая генерация
# ---------------------------------------------------------------------------


class TestBasicGeneration:
    def test_schema_template_renders(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "schema", "type.j2", """\
            {{ set_output_path(schema.name ~ ".hpp") }}
            // schema: {{ schema.name }}
        """)
        ir = MockIr(schemas=[MockSchema(name="UserProfile")])
        results = generator.generate(ir)

        assert len(results) == 1
        out_file = output_dir / "UserProfile.hpp"
        assert out_file.exists()
        assert "schema: UserProfile" in out_file.read_text()

    def test_operation_template_renders(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "operation", "handler.j2", """\
            {{ set_output_path(operation.id ~ ".cpp") }}
            // operation: {{ operation.id }}
        """)
        ir = MockIr(operations=[MockOperation(id="getUser")])
        results = generator.generate(ir)

        assert len(results) == 1
        assert (output_dir / "getUser.cpp").exists()

    def test_multiple_entities(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "schema", "type.j2", """\
            {{ set_output_path(schema.name ~ ".hpp") }}
            struct {{ schema.name }};
        """)
        ir = MockIr(schemas=[MockSchema("Foo"), MockSchema("Bar"), MockSchema("Baz")])
        results = generator.generate(ir)

        assert len(results) == 3
        for name in ("Foo", "Bar", "Baz"):
            assert (output_dir / f"{name}.hpp").exists()

    def test_multiple_templates_per_entity(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "operation", "header.j2", """\
            {{ set_output_path(operation.id ~ ".hpp") }}
            // header for {{ operation.id }}
        """)
        write_template(templates_dir, "operation", "source.j2", """\
            {{ set_output_path(operation.id ~ ".cpp") }}
            // source for {{ operation.id }}
        """)
        ir = MockIr(operations=[MockOperation(id="createOrder")])
        results = generator.generate(ir)

        assert len(results) == 2
        assert (output_dir / "createOrder.hpp").exists()
        assert (output_dir / "createOrder.cpp").exists()

    def test_nested_output_path_creates_dirs(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "schema", "type.j2", """\
            {{ set_output_path("include/models/" ~ schema.name ~ ".hpp") }}
            struct {{ schema.name }} {};
        """)
        ir = MockIr(schemas=[MockSchema("Order")])
        generator.generate(ir)

        assert (output_dir / "include" / "models" / "Order.hpp").exists()


# ---------------------------------------------------------------------------
# Append-семантика: несколько сущностей → один файл
# ---------------------------------------------------------------------------


class TestAppendSemantics:
    def test_same_output_path_concatenates(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        # Две схемы пишут в один файл — контент должен быть конкатенирован
        write_template(templates_dir, "schema", "all.j2", """\
            {{ set_output_path("all_types.hpp") }}
            struct {{ schema.name }};
        """)
        ir = MockIr(schemas=[MockSchema("Foo"), MockSchema("Bar")])
        results = generator.generate(ir)

        # только один файл написан (оба в all_types.hpp)
        assert len(results) == 1
        content = (output_dir / "all_types.hpp").read_text()
        assert "Foo" in content
        assert "Bar" in content

    def test_overwrites_existing_file_before_first_write(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        # Если файл уже существовал до запуска — он должен быть перезаписан,
        # а не дополнен старым содержимым
        existing = output_dir / "types.hpp"
        existing.write_text("// OLD CONTENT\n", encoding="utf-8")

        write_template(templates_dir, "schema", "t.j2", """\
            {{ set_output_path("types.hpp") }}
            struct {{ schema.name }};
        """)
        ir = MockIr(schemas=[MockSchema("New")])
        generator.generate(ir)

        content = existing.read_text()
        assert "OLD CONTENT" not in content
        assert "New" in content

    def test_append_order_matches_render_order(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "schema", "all.j2", """\
            {{ set_output_path("out.hpp") }}
            // {{ schema.name }}
        """)
        ir = MockIr(schemas=[MockSchema("Alpha"), MockSchema("Beta"), MockSchema("Gamma")])
        generator.generate(ir)

        content = (output_dir / "out.hpp").read_text()
        pos_alpha = content.index("Alpha")
        pos_beta = content.index("Beta")
        pos_gamma = content.index("Gamma")
        assert pos_alpha < pos_beta < pos_gamma


# ---------------------------------------------------------------------------
# Skip через output_path = none
# ---------------------------------------------------------------------------


class TestSkip:
    def test_none_output_path_skips_file(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "schema", "type.j2", """\
            {{ set_output_path(none) }}
            // этот контент не должен появиться нигде
        """)
        ir = MockIr(schemas=[MockSchema("Foo")])
        results = generator.generate(ir)

        assert results == []
        # В output_dir не должно появиться ничего
        assert list(output_dir.iterdir()) == []

    def test_conditional_skip(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        # Пропускаем схему с именем "Skip", генерируем остальные
        write_template(templates_dir, "schema", "type.j2", """\
            {% if schema.name == "Skip" %}
            {{ set_output_path(none) }}
            {% else %}
            {{ set_output_path(schema.name ~ ".hpp") }}
            {% endif %}
            struct {{ schema.name }};
        """)
        ir = MockIr(schemas=[MockSchema("Keep"), MockSchema("Skip")])
        results = generator.generate(ir)

        assert len(results) == 1
        assert (output_dir / "Keep.hpp").exists()
        assert not (output_dir / "Skip.hpp").exists()


# ---------------------------------------------------------------------------
# Webhook fallback
# ---------------------------------------------------------------------------


class TestWebhookFallback:
    def test_webhook_uses_own_templates_if_present(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "webhook", "hook.j2", """\
            {{ set_output_path(webhook.id ~ ".hpp") }}
            // webhook-specific: {{ webhook.id }}
        """)
        ir = MockIr(webhooks=[MockOperation(id="onOrderCreated")])
        generator.generate(ir)

        content = (output_dir / "onOrderCreated.hpp").read_text()
        assert "webhook-specific" in content

    def test_webhook_fallback_to_operation_templates(
        self, tmp_path: Path, output_dir: Path
    ) -> None:
        # templates/webhook/ не существует → fallback на operation/
        templates_dir = tmp_path / "templates"
        templates_dir.mkdir()
        (templates_dir / "operation").mkdir()
        # webhook/ намеренно не создаём

        write_template(templates_dir, "operation", "op.j2", """\
            {{ set_output_path(operation.id ~ ".hpp") }}
            // ir_kind={{ ir_kind }}, id={{ operation.id }}
        """)
        gen = Generator(templates_dir=templates_dir, output_dir=output_dir)
        ir = MockIr(webhooks=[MockOperation(id="onPayment")])
        gen.generate(ir)

        content = (output_dir / "onPayment.hpp").read_text()
        # ir_kind сообщает что это webhook, но переменная называется operation
        assert "ir_kind=webhook" in content
        assert "onPayment" in content

    def test_webhook_fallback_when_webhook_dir_empty(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        # templates/webhook/ существует, но пуста → тоже fallback
        write_template(templates_dir, "operation", "op.j2", """\
            {{ set_output_path(operation.id ~ ".hpp") }}
            // fallback: {{ operation.id }}
        """)
        # webhook/ уже создана фикстурой, но пуста
        ir = MockIr(webhooks=[MockOperation(id="onShipped")])
        generator.generate(ir)

        assert (output_dir / "onShipped.hpp").exists()

    def test_webhook_ir_kind_is_webhook_during_fallback(
        self, tmp_path: Path, output_dir: Path
    ) -> None:
        templates_dir = tmp_path / "templates"
        templates_dir.mkdir()
        (templates_dir / "operation").mkdir()

        write_template(templates_dir, "operation", "op.j2", """\
            {{ set_output_path("out.txt") }}
            {{ ir_kind }}
        """)
        gen = Generator(templates_dir=templates_dir, output_dir=output_dir)
        # И операция, и webhook — ir_kind должен различаться
        ir = MockIr(
            operations=[MockOperation(id="regularOp")],
            webhooks=[MockOperation(id="hookOp")],
        )
        # Два рендера с разным ir_kind пишут в один файл
        gen.generate(ir)

        content = (output_dir / "out.txt").read_text()
        assert "operation" in content
        assert "webhook" in content


# ---------------------------------------------------------------------------
# Cleanup при ошибке
# ---------------------------------------------------------------------------


class TestCleanupOnError:
    def test_cleanup_removes_written_files_on_error(
        self, templates_dir: Path, output_dir: Path
    ) -> None:
        # Первый шаблон успешно создаёт файл, второй падает
        write_template(templates_dir, "schema", "a_good.j2", """\
            {{ set_output_path("good.hpp") }}
            // ok
        """)
        write_template(templates_dir, "schema", "b_bad.j2", """\
            // нет output_path — упадёт с MissingOutputPathError
        """)
        ir = MockIr(schemas=[MockSchema("Foo")])
        gen = Generator(templates_dir=templates_dir, output_dir=output_dir)

        with pytest.raises(MissingOutputPathError):
            gen.generate(ir)

        # После cleanup файл не должен остаться
        assert not (output_dir / "good.hpp").exists()

    def test_cleanup_removes_preexisting_file_we_started_overwriting(
        self, templates_dir: Path, output_dir: Path
    ) -> None:
        # Файл существовал до запуска, мы начали его перезаписывать, потом упали
        existing = output_dir / "types.hpp"
        existing.write_text("// OLD\n")

        write_template(templates_dir, "schema", "a_good.j2", """\
            {{ set_output_path("types.hpp") }}
            // new content
        """)
        write_template(templates_dir, "schema", "b_bad.j2", """\
            // нет output_path
        """)
        ir = MockIr(schemas=[MockSchema("Foo")])
        gen = Generator(templates_dir=templates_dir, output_dir=output_dir)

        with pytest.raises(MissingOutputPathError):
            gen.generate(ir)

        # Файл должен быть удалён (не оставаться ни со старым, ни с новым содержимым)
        assert not existing.exists()

    def test_debug_mode_keeps_files_on_error(
        self, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "schema", "a_good.j2", """\
            {{ set_output_path("good.hpp") }}
            // partial output
        """)
        write_template(templates_dir, "schema", "b_bad.j2", """\
            // нет output_path
        """)
        ir = MockIr(schemas=[MockSchema("Foo")])
        gen = Generator(templates_dir=templates_dir, output_dir=output_dir, debug=True)

        with pytest.raises(MissingOutputPathError):
            gen.generate(ir)

        # В debug-режиме файл должен остаться для анализа
        assert (output_dir / "good.hpp").exists()


# ---------------------------------------------------------------------------
# Хелперы в шаблонах
# ---------------------------------------------------------------------------


class TestHelpersInTemplates:
    def test_naming_filters(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "operation", "op.j2", """\
            {{ set_output_path("out.txt") }}
            {{ operation.id | snake_case }} {{ operation.id | pascal_case }}
        """)
        ir = MockIr(operations=[MockOperation(id="getUserById")])
        generator.generate(ir)

        assert "get_user_by_id GetUserById" in (output_dir / "out.txt").read_text()

    def test_extra_context_available(
        self, templates_dir: Path, output_dir: Path
    ) -> None:
        gen = Generator(
            templates_dir=templates_dir,
            output_dir=output_dir,
            extra_context={"namespace": "my_api"},
        )
        write_template(templates_dir, "schema", "type.j2", """\
            {{ set_output_path("out.hpp") }}
            namespace {{ namespace }} {}
        """)
        ir = MockIr(schemas=[MockSchema(name="Foo")])
        gen.generate(ir)

        assert "namespace my_api" in (output_dir / "out.hpp").read_text()


# ---------------------------------------------------------------------------
# Обработка ошибок
# ---------------------------------------------------------------------------

def absolute_path_example() -> str:
    # crossplatform
    return str(Path.cwd() / "some_file.txt")

class TestErrorHandling:
    def test_missing_output_path_raises(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "schema", "bad.j2", """\
            // no output_path set
            struct {{ schema.name }} {};
        """)
        ir = MockIr(schemas=[MockSchema(name="Foo")])
        with pytest.raises(MissingOutputPathError) as exc_info:
            generator.generate(ir)

        assert "bad.j2" in str(exc_info.value)
        assert "output_path" in str(exc_info.value)

    def test_absolute_output_path_raises(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "schema", "bad.j2", f"""\
            {{{{ set_output_path("{absolute_path_example()}") }}}}
            x
        """)
        ir = MockIr(schemas=[MockSchema(name="Foo")])
        with pytest.raises(GeneratorError, match="relative"):
            generator.generate(ir)

    def test_undefined_variable_raises(
        self, generator: Generator, templates_dir: Path, output_dir: Path
    ) -> None:
        write_template(templates_dir, "schema", "bad.j2", """\
            {{ set_output_path("out.hpp") }}
            {{ schema.nonexistent_field }}
        """)
        ir = MockIr(schemas=[MockSchema(name="Foo")])
        with pytest.raises(GeneratorError):
            generator.generate(ir)

    def test_no_templates_no_error(
        self, tmp_path: Path, output_dir: Path
    ) -> None:
        # Папка operation/ не существует → тихий пропуск
        templates_dir = tmp_path / "templates"
        templates_dir.mkdir()
        gen = Generator(templates_dir=templates_dir, output_dir=output_dir)
        ir = MockIr(operations=[MockOperation(id="getUser")])
        assert gen.generate(ir) == []

    def test_empty_ir_no_error(
        self, generator: Generator
    ) -> None:
        ir = MockIr()
        assert generator.generate(ir) == []
