# полностью сгенерировано claude на основе С++ кода ir_object

"""
Generic-движок to_dict/from_dict для dataclass-ов, аналог C++-подхода
с template specialization (serialize<T>/parse<T> по типу), но реализован
через единый mixin-класс Serializable, от которого наследуются обычные
("aggregate") dataclass-ы и получают сериализацию бесплатно.

Поддерживается из коробки (без единой строчки кода в самом классе):
- Optional[T]      -> ключ опускается, если значение None
- list[T]          -> список сериализованных элементов
- dict[str, T]     -> dict с сериализованными значениями
- вложенный Serializable -> рекурсивно .to_dict() / T.from_dict()
- Enum (включая str-Enum) -> .value / T(value)
- str / int / float / bool -> как есть

Variant-подобные поля (где "одно поле -> одно значение" не работает,
например schema_type.data или response_status.status) НЕ поддерживаются
автоматически - для них пишется явный override to_dict()/from_dict() в
самом классе, ровно как в C++ для variant'ов нужна не общий шаблон,
а явная специализация. Остальной механизм (для всех прочих полей того же
класса) продолжает работать через super().to_dict()/обычные dataclass-поля.

Все dataclass-ы используют kw_only=True (через _kw_only_dataclass ниже),
чтобы порядок required/optional полей не имел значения - это требование
было явно снято с "порядок как в C++ struct".
"""

from __future__ import annotations

import dataclasses
import enum
import types
from dataclasses import fields
from typing import Any, Optional, Union, get_args, get_origin, get_type_hints

# dataclass(kw_only=True) - единая точка для возможной донастройки в будущем
kw_only_dataclass = dataclasses.dataclass(kw_only=True)


def field_with_key(*, key: str, default: Any = dataclasses.MISSING,
                    default_factory: Any = dataclasses.MISSING) -> dataclasses.Field:
    """
    Поле dataclass с явным сериализационным ключом, отличным от python-имени
    (например python `content_type` -> ключ "contentType").
    """
    kwargs: dict[str, Any] = {"metadata": {"key": key}}
    if default is not dataclasses.MISSING:
        kwargs["default"] = default
    if default_factory is not dataclasses.MISSING:
        kwargs["default_factory"] = default_factory
    return dataclasses.field(**kwargs)


def _is_optional(tp: Any) -> tuple[bool, Any]:
    """Optional[T] это Union[T, None]. Возвращает (is_optional, T)."""
    origin = get_origin(tp)
    if origin is Union or origin is types.UnionType:
        args = get_args(tp)
        non_none = [a for a in args if a is not type(None)]
        if len(non_none) == 1 and type(None) in args:
            return True, non_none[0]
    return False, tp


def _field_key(f: dataclasses.Field) -> str:
    return f.metadata.get("key", f.name)


def _serialize_value(value: Any, tp: Any) -> Any:
    if value is None:
        return None
    if isinstance(value, Serializable):
        return value.to_dict()
    if isinstance(value, enum.Enum):
        return value.value
    origin = get_origin(tp)
    if origin is list:
        (elem_tp,) = get_args(tp) or (Any,)
        return [_serialize_value(v, elem_tp) for v in value]
    if origin is dict:
        _, val_tp = get_args(tp) or (Any, Any)
        return {k: _serialize_value(v, val_tp) for k, v in value.items()}
    return value  # str/int/float/bool как есть


def _deserialize_value(value: Any, tp: Any) -> Any:
    if value is None:
        return None
    if isinstance(tp, type) and issubclass(tp, Serializable):
        return tp.from_dict(value)
    if isinstance(tp, type) and issubclass(tp, enum.Enum):
        return tp(value)
    origin = get_origin(tp)
    if origin is list:
        (elem_tp,) = get_args(tp) or (Any,)
        return [_deserialize_value(v, elem_tp) for v in value]
    if origin is dict:
        _, val_tp = get_args(tp) or (Any, Any)
        return {k: _deserialize_value(v, val_tp) for k, v in value.items()}
    return value


class Serializable:
    """
    Маркерный mixin: даёт to_dict/from_dict "по умолчанию" — обходом полей
    dataclass через type hints. Аналог дефолтной (не-variant) специализации
    serialize<T>/parse<T> в C++.

    Классам с variant-полями (SchemaType, ResponseStatus) нужно
    переопределить to_dict/from_dict целиком — это единственное место,
    где требуется ручной код, по аналогии с явной специализацией шаблона.
    """

    def to_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {}
        hints = get_type_hints(type(self))
        for f in fields(self):  # type: ignore[arg-type]
            value = getattr(self, f.name)
            tp = hints[f.name]
            is_opt, inner_tp = _is_optional(tp)
            if is_opt and value is None:
                continue
            result[_field_key(f)] = _serialize_value(value, inner_tp if is_opt else tp)
        return result

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Any:
        hints = get_type_hints(cls)
        kwargs: dict[str, Any] = {}
        for f in fields(cls):  # type: ignore[arg-type]
            key = _field_key(f)
            if key not in d:
                continue  # нет в dict -> используем default самого dataclass
            tp = hints[f.name]
            is_opt, inner_tp = _is_optional(tp)
            kwargs[f.name] = _deserialize_value(d[key], inner_tp if is_opt else tp)
        return cls(**kwargs)
