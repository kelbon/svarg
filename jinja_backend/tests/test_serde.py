# полностью сгенерировано claude на основе С++ кода ir_object

"""
Тесты сериализации ir_schema.

Структура:
- TestRoundTrip — для каждого класса проверяем from_dict(to_dict(x)) == x
  и (где есть готовый пример) to_dict(from_dict(d)) == d, параметризованно.
- TestOmitsNoneOptionalFields / TestKeepsFalsyExplicitValues — поведение
  optional-полей: None опускается, явные False/0/"" сериализуются.
- TestVariantDiscriminators — SchemaType и ResponseStatus: ручная
  variant-логика, не покрытая generic-движком.
- TestIrObjectMerge — семантика merge() поверх None/непустых списков.
- test_example_ir_roundtrip — сквозная проверка на реальном примере IR.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest
import yaml

from ir_schema import (
    AllofSchema,
    Body,
    DiscriminatorDesc,
    EnumSchema,
    FundamentalE,
    Header,
    IrObject,
    NotSchema,
    ObjectFieldInfo,
    ObjectSchema,
    OneofSchema,
    Operation,
    OperationParameter,
    ParameterLocation,
    ResponseDesc,
    ResponseStatus,
    Schema,
    SchemaRef,
    SchemaType,
    StatusRangeE,
    make_fundamental_ref,
)

EXAMPLE_IR_PATH = Path(__file__).parent / "example_ir.yaml"


def assert_roundtrips(obj):
    """from_dict(to_dict(obj)) должен быть эквивалентен obj (по dict-представлению)."""
    d = obj.to_dict()
    restored = type(obj).from_dict(d)
    assert restored.to_dict() == d, (
        f"round-trip изменил сериализованный вид:\n"
        f"  было:  {json.dumps(d, indent=2, ensure_ascii=False)}\n"
        f"  стало: {json.dumps(restored.to_dict(), indent=2, ensure_ascii=False)}"
    )
    return d


# ---------------------------------------------------------------------------
# Round-trip: один объект на класс, без variant-полей и без "тонкой" семантики
# (та проверяется отдельно ниже) — здесь просто "ничего не потерялось".
# ---------------------------------------------------------------------------


def _sample_objects():
    string_ref = SchemaRef(spec="@string@")
    int_ref = SchemaRef(spec="@integer@")
    named_ref = SchemaRef(spec="api.yaml", jsonptr="/components/schemas/User", name="User")

    return [
        pytest.param(string_ref, id="schema_ref/fundamental"),
        pytest.param(named_ref, id="schema_ref/named"),
        pytest.param(
            EnumSchema(type=string_ref, values=["a", "b"]),
            id="enum_schema",
        ),
        pytest.param(
            ObjectFieldInfo(type=int_ref, name="count", required=True),
            id="object_field_info",
        ),
        pytest.param(
            ObjectSchema(
                fields=[ObjectFieldInfo(type=int_ref, name="count", required=True)],
                additional_properties=string_ref,
            ),
            id="object_schema",
        ),
        pytest.param(
            Schema(
                spec="api.yaml",
                jsonptr="/components/schemas/User",
                name="User",
                type=SchemaType(
                    data=ObjectSchema(
                        fields=[ObjectFieldInfo(type=string_ref, name="id", required=True)]
                    )
                ),
            ),
            id="schema/object",
        ),
        pytest.param(
            OperationParameter(
                name="expand",
                schema=string_ref,
                location=ParameterLocation.QUERY,
                required=True,
            ),
            id="operation_parameter",
        ),
        pytest.param(
            Header(name="X-Rate-Limit", required=True, schema=int_ref),
            id="header/full",
        ),
        pytest.param(
            Body(content_type="application/json", schema=named_ref),
            id="body/minimal",
        ),
        pytest.param(
            ResponseDesc(
                status=ResponseStatus.from_string("200"),
                headers=[Header(name="X-Rate-Limit", schema=int_ref)],
                bodies=[Body(content_type="application/json", schema=named_ref)],
            ),
            id="response_desc",
        ),
        pytest.param(
            Operation(
                id="getUser",
                spec="api.yaml",
                path="/users/{userId}",
                method="get",
                parameters=[OperationParameter(name="expand", schema=string_ref)],
                responses=[ResponseDesc(status=ResponseStatus.from_string("200"))],
            ),
            id="operation",
        ),
    ]


@pytest.mark.parametrize("obj", _sample_objects())
def test_roundtrip(obj):
    assert_roundtrips(obj)


# ---------------------------------------------------------------------------
# Optional-поля: None опускается из сериализованного dict, а не пишется как
# null/None — это явное требование к формату (см. example_ir.yaml: jsonptr
# отсутствует у ссылок на фундаментальные типы, а не равен null).
# ---------------------------------------------------------------------------


class TestOmitsNoneOptionalFields:
    def test_schema_ref_without_jsonptr_or_name(self):
        d = SchemaRef(spec="@string@").to_dict()
        assert d == {"spec": "@string@"}
        assert "jsonptr" not in d
        assert "name" not in d

    def test_header_without_required_or_schema(self):
        d = Header(name="X-Trace-Id").to_dict()
        assert d == {"name": "X-Trace-Id"}

    def test_body_without_members_encoding_or_streaming(self):
        d = Body(content_type="application/json", schema=SchemaRef(spec="@string@")).to_dict()
        assert "membersEncoding" not in d
        assert "streaming" not in d

    def test_operation_parameter_without_required(self):
        d = OperationParameter(name="filter", schema=SchemaRef(spec="@string@")).to_dict()
        assert "required" not in d

    def test_object_schema_without_additional_fields(self):
        d = ObjectSchema(fields=[]).to_dict()
        assert "additional_properties" not in d

    def test_operation_without_optional_collections(self):
        d = Operation(spec="api.yaml", path="/x", method="get").to_dict()
        for key in ("id", "parameters", "bodies", "responses", "defaultResponse", "meta"):
            assert key not in d, f"{key!r} должен быть опущен, а не None"

    def test_ir_object_all_none_is_empty_dict(self):
        assert IrObject().to_dict() == {}


# ---------------------------------------------------------------------------
# Falsy, но явно заданные значения (False, 0, "") — это НЕ то же самое, что
# None, и должны сериализоваться как есть, а не выпадать по `if value:`.
# ---------------------------------------------------------------------------


class TestKeepsFalsyExplicitValues:
    def test_body_streaming_false_is_serialized(self):
        d = Body(
            content_type="application/json",
            schema=SchemaRef(spec="@string@"),
            streaming=False,
        ).to_dict()
        assert d["streaming"] is False

    def test_operation_parameter_required_false_is_serialized(self):
        d = OperationParameter(
            name="filter", schema=SchemaRef(spec="@string@"), required=False
        ).to_dict()
        assert d["required"] is False

    def test_object_field_info_required_false_is_serialized(self):
        # required не Optional в C++ (bool required = false;) — пишем всегда
        d = ObjectFieldInfo(type=SchemaRef(spec="@string@"), name="x", required=False).to_dict()
        assert d["required"] is False


# ---------------------------------------------------------------------------
# Variant-логика: SchemaType.data и ResponseStatus.status — единственные два
# места с ручной to_dict/from_dict (см. serde.py docstring).
# ---------------------------------------------------------------------------


class TestSchemaTypeDiscriminator:
    def test_fundamental_uses_string_value_not_nested_dict(self):
        d = SchemaType(data=FundamentalE.INTEGER).to_dict()
        assert d == {"fundamental": "integer"}

    def test_object_nests_full_dict_under_discriminator(self):
        d = SchemaType(data=ObjectSchema(fields=[])).to_dict()
        assert set(d.keys()) == {"object"}
        assert d["object"] == {"fields": []}

    def test_not_schema_discriminator(self):
        d = SchemaType(data=NotSchema(type=SchemaRef(spec="@string@"))).to_dict()
        assert d == {"not": {"type": {"spec": "@string@"}}}

    def test_allof_discriminator(self):
        d = SchemaType(data=AllofSchema(schemas=[SchemaRef(spec="@string@")])).to_dict()
        assert d == {"allOf": {"schemas": [{"spec": "@string@"}]}}

    def test_oneof_with_discriminator_desc_roundtrips(self):
        oneof = OneofSchema(
            schemas=[SchemaRef(spec="api.yaml", jsonptr="/Cat", name="Cat")],
            discriminator=DiscriminatorDesc(
                property_name="petType",
                mapping={"cat": SchemaRef(spec="api.yaml", jsonptr="/Cat", name="Cat")},
            ),
        )
        d = assert_roundtrips(SchemaType(data=oneof))
        assert d["oneOf"]["discriminator"]["propertyName"] == "petType"

    def test_unknown_discriminator_raises(self):
        with pytest.raises(ValueError):
            SchemaType.from_dict({"totallyUnknown": {}})

    def test_multi_key_dict_raises(self):
        with pytest.raises(ValueError):
            SchemaType.from_dict({"fundamental": "integer", "enum": {}})


class TestResponseStatus:
    @pytest.mark.parametrize(
        ("status", "expected"),
        [
            (ResponseStatus.default(), "default"),
            (ResponseStatus(status=200), "200"),
            (ResponseStatus(status=StatusRangeE._4XX), "4XX"),
        ],
    )
    def test_serializes_as_plain_string_not_dict(self, status, expected):
        # ВАЖНО: в отличие от SchemaType, здесь нет дискриминатора-ключа —
        # значение variant'а это сама строка.
        assert status.to_dict() == expected

    def test_is_default(self):
        assert ResponseStatus.default().is_default()
        assert not ResponseStatus(status=200).is_default()

    @pytest.mark.parametrize("raw", ["default", "1XX", "200", "404"])
    def test_roundtrips_via_string(self, raw):
        assert ResponseStatus.from_string(raw).to_string() == raw


# ---------------------------------------------------------------------------
# make_fundamental_ref / refers_to_fundamental
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("fundamental", list(FundamentalE))
def test_make_fundamental_ref_roundtrips_to_same_enum(fundamental):
    ref = make_fundamental_ref(fundamental)
    assert ref.jsonptr is None
    assert ref.refers_to_fundamental() == fundamental


def test_named_ref_does_not_refer_to_fundamental():
    ref = SchemaRef(spec="api.yaml", jsonptr="/components/schemas/User", name="User")
    assert ref.refers_to_fundamental() is None


# ---------------------------------------------------------------------------
# ir_object.merge
# ---------------------------------------------------------------------------


class TestIrObjectMerge:
    def test_merges_non_empty_lists_by_concatenation(self):
        s1 = Schema(spec="a.yaml", jsonptr="/A", type=SchemaType(data=FundamentalE.STRING))
        s2 = Schema(spec="b.yaml", jsonptr="/B", type=SchemaType(data=FundamentalE.INTEGER))
        ir1 = IrObject(schemas=[s1])
        ir2 = IrObject(schemas=[s2])

        ir1.merge(ir2)

        assert ir1.schemas == [s1, s2]

    def test_none_plus_none_stays_none(self):
        ir1, ir2 = IrObject(), IrObject()
        ir1.merge(ir2)
        assert ir1.schemas is None
        assert ir1.operations is None
        assert ir1.webhooks is None

    def test_none_plus_list_takes_the_list(self):
        op = Operation(spec="a.yaml", path="/x", method="get")
        ir1 = IrObject(webhooks=None)
        ir2 = IrObject(webhooks=[op])

        ir1.merge(ir2)

        assert ir1.webhooks == [op]

    def test_does_not_mutate_the_merged_in_object(self):
        op = Operation(spec="a.yaml", path="/x", method="get")
        ir1 = IrObject(operations=[op])
        ir2 = IrObject(operations=[op])

        ir1.merge(ir2)

        assert ir2.operations == [op], "merge не должен менять переданный obj"


# ---------------------------------------------------------------------------
# Сквозная проверка на реальном примере (example_ir.yaml)
# ---------------------------------------------------------------------------


def test_example_ir_roundtrip():
    original = yaml.safe_load(EXAMPLE_IR_PATH.read_text())

    restored_dict = IrObject.from_dict(original).to_dict()

    assert restored_dict == original
