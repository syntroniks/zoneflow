"""Find the serial port of a ZoneFlow board."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass


class PortError(RuntimeError):
    pass


@dataclass(frozen=True)
class Port:
    device: str
    description: str
    vid: int | None
    pid: int | None

    def __str__(self) -> str:
        ids = f" [{self.vid:04x}:{self.pid:04x}]" if self.vid is not None and self.pid is not None else ""
        return f"{self.device}  {self.description}{ids}"


# Espressif's own USB Serial/JTAG controller is what the board's USB-C port is wired to (hardware design 12.4).
ESPRESSIF_VID = 0x303A
# USB-UART bridges seen on ESP32 boards and on factory jigs that use header J8.
BRIDGES = {(0x10C4, 0xEA60), (0x1A86, 0x7523), (0x1A86, 0x55D4), (0x0403, 0x6001), (0x0403, 0x6010), (0x0403, 0x6015)}


def system_ports() -> list[Port]:
    from serial.tools import list_ports

    return [Port(p.device, p.description or "", p.vid, p.pid) for p in list_ports.comports()]


def candidates(ports: list[Port]) -> list[Port]:
    native = [p for p in ports if p.vid == ESPRESSIF_VID]
    if native:
        return native
    return [p for p in ports if (p.vid, p.pid) in BRIDGES]


def choose(requested: str | None, lister: Callable[[], list[Port]] = system_ports) -> str:
    if requested:
        return requested
    ports = lister()
    found = candidates(ports)
    if len(found) == 1:
        return found[0].device
    if not found:
        listing = "\n".join(f"  {p}" for p in ports) or "  (no serial ports at all)"
        raise PortError(
            "no ESP32-S3 serial port found. Plug the board in with a USB data cable, or give --port.\n"
            f"Ports seen:\n{listing}"
        )
    listing = "\n".join(f"  {p}" for p in found)
    raise PortError(f"more than one possible port; choose one with --port:\n{listing}")
