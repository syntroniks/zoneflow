"""The serial layer: esptool, run as a subprocess so its output reaches the terminal as it is."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path
from typing import Protocol


class FlashError(RuntimeError):
    pass


class Flasher(Protocol):
    def write(self, port: str, baud: int, chip: str, parts: list[tuple[int, Path]]) -> None: ...

    def erase_region(self, port: str, baud: int, chip: str, offset: int, size: int) -> None: ...


class EsptoolFlasher:
    def _run(self, args: list[str]) -> None:
        cmd = [sys.executable, "-m", "esptool", *args]
        try:
            rc = subprocess.run(cmd, check=False).returncode
        except OSError as e:
            raise FlashError(f"could not run esptool: {e}") from e
        if rc != 0:
            raise FlashError(f"esptool failed with exit code {rc}")

    def write(self, port, baud, chip, parts):
        args = ["--chip", chip, "--port", port, "--baud", str(baud), "write-flash"]
        # The images carry their own flash parameters; keep them.
        args += ["--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep"]
        for offset, path in parts:
            args += [hex(offset), str(path)]
        self._run(args)

    def erase_region(self, port, baud, chip, offset, size):
        self._run(["--chip", chip, "--port", port, "--baud", str(baud), "erase-region", hex(offset), hex(size)])
