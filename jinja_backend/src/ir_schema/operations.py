# полностью сгенерировано claude на основе С++ кода ir_object

"""
Python-представление операций (operation_parameter, header, body,
response_desc, operation) и верхнеуровневого ir_object,
на generic-движке сериализации (см. serde.py).

Только ResponseStatus (variant<default_response_t, int64_t, status_range_e>)
требует ручной to_dict/from_dict, т.к. сериализуется ОДНОЙ строкой, а не
{"discriminator": value} как SchemaType — остальные классы наследуются от
Serializable без единой дополнительной строчки.
"""

from __future__ import annotations

from enum import Enum
from typing import Any, Optional, Union

from .schema_types import Schema, SchemaRef
from .serde import Serializable, field_with_key, kw_only_dataclass


# meta_t = string_map<Any> в C++, в python — обычный dict
MetaT = dict[str, Any]


# ---------------------------------------------------------------------------
# parameter_location
#
# Не показан в C++-сэмпле явно, но судя по "location": "query" в примере и
# использованию parameter_location::path как default — это OpenAPI-набор
# локаций параметра. При необходимости поправить значения.
# ---------------------------------------------------------------------------


class ParameterLocation(str, Enum):
    PATH = "path"
    QUERY = "query"
    HEADER = "header"
    COOKIE = "cookie"


# ---------------------------------------------------------------------------
# operation_parameter
# ---------------------------------------------------------------------------


@kw_only_dataclass
class OperationParameter(Serializable):
    name: str
    schema: SchemaRef
    location: ParameterLocation = ParameterLocation.PATH
    required: Optional[bool] = None
    encoding: Optional[str] = None
    default_value: Optional[Any] = None
    meta: Optional[MetaT] = None


# ---------------------------------------------------------------------------
# header
# ---------------------------------------------------------------------------


@kw_only_dataclass
class Header(Serializable):
    name: str
    required: Optional[bool] = None
    schema: Optional[SchemaRef] = None
    default_value: Optional[Any] = None


# ---------------------------------------------------------------------------
# body
# ---------------------------------------------------------------------------


@kw_only_dataclass
class Body(Serializable):
    content_type: str = field_with_key(key="contentType")
    schema: SchemaRef
    members_encoding: Optional[dict[str, str]] = field_with_key(key="membersEncoding", default=None)
    members_headers: Optional[dict[str, list[Header]]] = field_with_key(key="membersHeaders", default=None)
    streaming: Optional[bool] = None


# ---------------------------------------------------------------------------
# response_status / status_range_e / default_response_t
# ---------------------------------------------------------------------------


class StatusRangeE(str, Enum):
    _1XX = "1XX"
    _2XX = "2XX"
    _3XX = "3XX"
    _4XX = "4XX"
    _5XX = "5XX"


_STATUS_RANGES = {e.value for e in StatusRangeE}


@kw_only_dataclass
class ResponseStatus(Serializable):
    """
    variant<default_response_t, int64_t, status_range_e>.

    Единственное (вместе с SchemaType) место с ручной to_dict/from_dict:
    сериализуется ОДНОЙ строкой (как response_status::to_string()/from_string()
    в C++), а не словарём с дискриминатором — поэтому generic Serializable-
    движок (который всегда возвращает dict) сюда не подходит.
    """

    status: Union[str, int, StatusRangeE]  # "default" | конкретный код | диапазон

    @classmethod
    def default(cls) -> "ResponseStatus":
        return cls(status="default")

    def is_default(self) -> bool:
        return self.status == "default"

    def to_string(self) -> str:
        if isinstance(self.status, StatusRangeE):
            return self.status.value
        if isinstance(self.status, int):
            return str(self.status)
        return self.status  # "default"

    @classmethod
    def from_string(cls, s: str) -> "ResponseStatus":
        if s == "default":
            return cls(status="default")
        if s in _STATUS_RANGES:
            return cls(status=StatusRangeE(s))
        return cls(status=int(s))

    def to_dict(self) -> str:  # type: ignore[override]
        return self.to_string()

    @classmethod
    def from_dict(cls, d: str) -> "ResponseStatus":  # type: ignore[override]
        return cls.from_string(d)


# ---------------------------------------------------------------------------
# response_desc
# ---------------------------------------------------------------------------


@kw_only_dataclass
class ResponseDesc(Serializable):
    status: ResponseStatus
    headers: Optional[list[Header]] = None
    bodies: Optional[list[Body]] = None

    def is_default(self) -> bool:
        return self.status.is_default()

    def make_default(self) -> None:
        self.status = ResponseStatus.default()

    def set_status(self, s: str) -> None:
        self.status = ResponseStatus.from_string(s)


# ---------------------------------------------------------------------------
# operation
# ---------------------------------------------------------------------------


@kw_only_dataclass
class Operation(Serializable):
    spec: str
    path: str
    method: str
    id: Optional[str] = None
    parameters: Optional[list[OperationParameter]] = None
    bodies: Optional[list[Body]] = None
    responses: Optional[list[ResponseDesc]] = None
    default_response: Optional[ResponseDesc] = field_with_key(key="defaultResponse", default=None)
    meta: Optional[MetaT] = None


# ---------------------------------------------------------------------------
# ir_object
# ---------------------------------------------------------------------------


@kw_only_dataclass
class IrObject(Serializable):
    schemas: Optional[list[Schema]] = None
    operations: Optional[list[Operation]] = None
    webhooks: Optional[list[Operation]] = None

    def merge(self, obj: "IrObject") -> None:
        """Аналог ir_object::merge в C++: объединяет obj в self."""

        def merge_list(a: Optional[list], b: Optional[list]) -> Optional[list]:
            if a is None and b is None:
                return None
            return list(a or []) + list(b or [])

        self.schemas = merge_list(self.schemas, obj.schemas)
        self.operations = merge_list(self.operations, obj.operations)
        self.webhooks = merge_list(self.webhooks, obj.webhooks)
