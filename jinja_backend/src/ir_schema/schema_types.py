# полностью сгенерировано claude на основе С++ кода ir_object

"""
Python-представление схемных типов из C++ (schema_ref, schema, ...),
на generic-движке сериализации (см. serde.py).

Только SchemaType.data (variant<fundamental_e, enum_schema, ...>) требует
ручной to_dict/from_dict - всё остальное наследуется от Serializable
и сериализуется по type hints автоматически.
"""

from __future__ import annotations

from dataclasses import field
from enum import Enum
from typing import Any, Optional, Union

from .serde import Serializable, field_with_key, kw_only_dataclass

# ---------------------------------------------------------------------------
# fundamental_e
# ---------------------------------------------------------------------------


class FundamentalE(str, Enum):
    NULL = "null"
    BOOLEAN = "boolean"
    INTEGER = "integer"
    NUMBER = "number"
    STRING = "string"
    ANY = "any"
    NEVER = "never"


_FUNDAMENTAL_TO_SPEC = {
    FundamentalE.NULL: "@null@",
    FundamentalE.BOOLEAN: "@boolean@",
    FundamentalE.INTEGER: "@integer@",
    FundamentalE.NUMBER: "@number@",
    FundamentalE.STRING: "@string@",
    FundamentalE.ANY: "@any@",
    FundamentalE.NEVER: "@never@",
}
_SPEC_TO_FUNDAMENTAL = {v: k for k, v in _FUNDAMENTAL_TO_SPEC.items()}


def make_fundamental_ref(e: FundamentalE) -> "SchemaRef":
    """Аналог make_fundamental_ref из C++."""
    return SchemaRef(spec=_FUNDAMENTAL_TO_SPEC[e])


# ---------------------------------------------------------------------------
# schema_ref
# ---------------------------------------------------------------------------


@kw_only_dataclass
class SchemaRef(Serializable):
    spec: str
    jsonptr: Optional[str] = None
    name: Optional[str] = None

    def refers_to_fundamental(self) -> Optional[FundamentalE]:
        """Если ref ссылается на фундаментальный тип (@string@ и т.д.) — вернуть его."""
        return _SPEC_TO_FUNDAMENTAL.get(self.spec)


# ---------------------------------------------------------------------------
# enum_schema / oneof_schema / anyof_schema / allof_schema /
# object_field_info / object_schema / array_schema / not_schema /
# discriminator_desc
#
# Варианты schema_type.data; в C++ это отдельные struct, здесь — обычные
# Serializable dataclass-ы без единой строчки ручного кода.
# ---------------------------------------------------------------------------


@kw_only_dataclass
class EnumSchema(Serializable):
    values: list[Any]
    type: Optional["SchemaRef"] = None


@kw_only_dataclass
class DiscriminatorDesc(Serializable):
    property_name: str = field_with_key(key="propertyName")
    mapping: dict[str, "SchemaRef"] = field(default_factory=dict)


@kw_only_dataclass
class OneofSchema(Serializable):
    schemas: list["SchemaRef"]
    discriminator: Optional[DiscriminatorDesc] = None


@kw_only_dataclass
class AnyofSchema(Serializable):
    schemas: list["SchemaRef"]
    discriminator: Optional[DiscriminatorDesc] = None


@kw_only_dataclass
class AllofSchema(Serializable):
    schemas: list["SchemaRef"]


@kw_only_dataclass
class ObjectFieldInfo(Serializable):
    type: "SchemaRef"
    name: str
    # НЕ optional в C++ (bool required = false;) -> сериализуется всегда,
    # поэтому это обычное (не Optional) поле с default
    required: bool = False


@kw_only_dataclass
class ObjectSchema(Serializable):
    fields: list[ObjectFieldInfo] = field(default_factory=list)
    additional_properties: Optional["SchemaRef"] = None


@kw_only_dataclass
class ArraySchema(Serializable):
    element: "SchemaRef"


@kw_only_dataclass
class NotSchema(Serializable):
    type: "SchemaRef"


# variant<fundamental_e, enum_schema, oneof_schema, anyof_schema, allof_schema,
#         object_schema, array_schema, not_schema>
SchemaTypeData = Union[
    FundamentalE,
    EnumSchema,
    OneofSchema,
    AnyofSchema,
    AllofSchema,
    ObjectSchema,
    ArraySchema,
    NotSchema,
]

# discriminator -> python-тип значения (для FundamentalE значение - сам Enum,
# для остальных - Serializable-классы)
_SCHEMA_TYPE_DISCRIMINATORS: dict[str, type] = {
    "fundamental": FundamentalE,
    "enum": EnumSchema,
    "oneOf": OneofSchema,
    "anyOf": AnyofSchema,
    "allOf": AllofSchema,
    "object": ObjectSchema,
    "array": ArraySchema,
    "not": NotSchema,
}
_TYPE_TO_DISCRIMINATOR = {v: k for k, v in _SCHEMA_TYPE_DISCRIMINATORS.items()}


@kw_only_dataclass
class SchemaType(Serializable):
    """
    Единственное место (вместе с ResponseStatus) с ручной to_dict/from_dict:
    variant сериализуется как {"<дискриминатор>": <значение>}, аналог
    schema_type::discriminator() / set_type_from_discriminator() в C++.
    Generic-движок Serializable.to_dict/from_dict сюда не подходит, поэтому
    переопределяем полностью.
    """

    data: SchemaTypeData

    def discriminator(self) -> str:
        return _TYPE_TO_DISCRIMINATOR[type(self.data)]

    def to_dict(self) -> dict[str, Any]:
        discriminator = self.discriminator()
        value = self.data.value if isinstance(self.data, FundamentalE) else self.data.to_dict()
        return {discriminator: value}

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "SchemaType":
        if len(d) != 1:
            raise ValueError(f"schema_type discriminator dict must have exactly 1 key, got {d!r}")
        (discriminator, value), = d.items()
        data_cls = _SCHEMA_TYPE_DISCRIMINATORS.get(discriminator)
        if data_cls is None:
            raise ValueError(f"invalid discriminator `{discriminator}`")
        data = FundamentalE(value) if data_cls is FundamentalE else data_cls.from_dict(value)
        return cls(data=data)


# ---------------------------------------------------------------------------
# schema
# ---------------------------------------------------------------------------


@kw_only_dataclass
class Schema(Serializable):
    spec: str
    jsonptr: str
    type: SchemaType
    name: Optional[str] = None
    # схемы которые не влияют на представление, только на валидацию
    # (совпадение имени с object_field_info.required чисто случайное)
    required: Optional[list[SchemaRef]] = None

    def scope(self) -> str:
        return f"{self.spec}#{self.jsonptr}"

    def add_requirement(self, s: SchemaRef) -> None:
        if self.required is None:
            self.required = []
        self.required.append(s)

    def ref(self) -> SchemaRef:
        return SchemaRef(spec=self.spec, jsonptr=self.jsonptr, name=self.name)
