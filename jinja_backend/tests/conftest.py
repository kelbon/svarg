"""
pytest подхватывает conftest.py

    project/
    ├── ir_schema/
    └── tests/
        └── conftest.py
"""
# запуск тестов: python -m pytest ./test_serde.py -v
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
