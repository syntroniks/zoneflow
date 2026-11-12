# SPDX-License-Identifier: Apache-2.0
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[3]
TOOL = REPO / "tools" / "reviewwalk"
# KiCad's Python module lives in the system interpreter, not in this environment.
KICAD_PY = "/usr/bin/python3"


def _has_pcbnew() -> bool:
    if not Path(KICAD_PY).exists():
        return False
    return subprocess.run([KICAD_PY, "-c", "import pcbnew"], capture_output=True).returncode == 0


needs_kicad = pytest.mark.skipif(not _has_pcbnew(), reason="KiCad's pcbnew module is not installed")
needs_node = pytest.mark.skipif(shutil.which("node") is None, reason="node is not installed")


def run_tool(*args: str, python: str = sys.executable) -> subprocess.CompletedProcess:
    env = {"PYTHONPATH": str(TOOL), "PYTHONDONTWRITEBYTECODE": "1", "PATH": "/usr/bin:/bin"}
    return subprocess.run([python, "-m", "reviewwalk", *args], capture_output=True, text=True, env=env, cwd=REPO)
