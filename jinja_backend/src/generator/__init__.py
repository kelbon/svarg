"""
codegen — OpenAPI IR → source code generator.

Основные точки входа:

    from codegen.generator import Generator
    from codegen.helpers import cpp_id, cpp_type_id
"""

from .generator import Generator, GeneratorError, MissingOutputPathError, RenderedFile

__all__ = [
    "Generator",
    "GeneratorError",
    "MissingOutputPathError",
    "RenderedFile",
]
