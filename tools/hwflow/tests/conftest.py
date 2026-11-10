# SPDX-License-Identifier: Apache-2.0
import os
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[3]
TEST_BOARD = REPO / "hardware" / "toolchain-test"

needs_kicad = pytest.mark.skipif(shutil.which("kicad-cli") is None, reason="kicad-cli is not installed")


def kicad_cli(*args: str, cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run kicad-cli with a private configuration directory, as the Makefile does."""
    env = dict(os.environ)
    env["KICAD_CONFIG_HOME"] = str((cwd or Path.cwd()) / "kicad-config")
    return subprocess.run(["kicad-cli", *args], cwd=cwd, env=env, capture_output=True, text=True)
