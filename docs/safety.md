# Safety

!!! danger "Engineering prototype"
    ZoneFlow has not been built, bench-tested or certified. Revision A boards
    are engineering prototypes until they have been bench-tested. Treat any
    board you build as an experiment. Do not rely on it where a stuck valve
    or a fault would cause damage or injury. Know where your water shut-off
    is.

The rules on this page come from the [system specification](design/system-spec.md)
and the [hardware design](design/hardware-design.md). They are design rules.
None has been verified on hardware.

## Class 2, low voltage only

ZoneFlow is designed for **Class 2 (low voltage, power-limited) 24 VAC
circuits only**. There is no mains voltage anywhere on the board.

- Power the board only from a Class 2 sprinkler transformer with a 24 VAC output. [What you need](getting-started/what-you-need.md#transformer) says how to choose one.
- **Never connect mains voltage to the board**: not to the 24V~ terminals, not to a zone terminal, not to a sensor terminal. The board has no mains isolation and no mains clearances.
- Never connect a DC supply to the 24V~ terminals.
- If a pump or valve needs mains power, switch it with a separate, correctly rated relay or contactor that has a 24 VAC coil, installed by someone qualified to do that work. The MV terminal drives only that 24 VAC coil.
- Connect the transformer to the mains in a way that meets your local electrical rules. That work is outside this project. If you are not qualified, use a plug-in transformer or hire an electrician.

## Indoor use only

The controller is for indoor or garage wall mounting. It is **not
weatherproof**.

- Mount it where it stays dry: no rain, no sprinkler spray, no condensation.
- Design operating range: **-20 to +50 °C** ambient, non-condensing. The parts are chosen for up to 70 °C inside the enclosure, which is the enclosure's own estimate of the rise. Keep the vents open.
- Bring valve and sensor cables in through the bottom cable entries with the strain relief, as the [assembly guide](enclosure/assembly.md) shows.

## The two fuses

The board has a 3 A time-lag fuse in each transformer wire: F4 behind 24V~
terminal 1 and F1 behind terminal 2. Every current the board draws passes
through both.

- They protect the board's copper and limit damage in a fault. They do not protect valves, wiring or a USB cable.
- On a fault either fuse can open. **Check both.**
- Replace only with a 3 A time-lag fuse of the same size: Littelfuse 452 series, 0452003.MRL. A fast fuse would open on every shorted valve wire.
- Take the 24 VAC plug off before you change a fuse. [Assembly, Later](enclosure/assembly.md#later) shows where they are.

Each output also has a resettable fuse (PTC) that limits current in a dead
short, and the 5 V sensor supply is current-limited. Those reset themselves.

## USB and earthing

Read this before you connect a computer to the USB-C socket.

The board's ground is one of the two transformer wires, the one on 24V~
terminal 2. A USB cable joins that ground to the computer's ground, and a
desktop computer's ground is earth. Depending on how your transformer and
valve wiring are earthed, plugging in USB can put the transformer's full
short-circuit current through the USB cable. The fault paths are in
[hardware design section 5.3](design/hardware-design.md#53-the-ground-question).

The design states four rules. They are repeated here as written:

1. Use a transformer whose 24 V output is not connected to earth on either side. If you are not sure, measure for continuity between each output terminal and the earth pin with the transformer unplugged. If either terminal is earthed, never connect a mains-powered computer by USB while the 24 VAC plug is in, whichever way round the plug is wired.
2. Never connect a computer by USB while the controller shares its transformer with another device, unless you have checked that both devices have the same leg on the same terminal.
3. For firmware work on an installed controller, prefer a laptop on battery, or unplug the 24 VAC plug block J1 first. The board runs from USB alone.
4. Sensor grounds and the Qwiic ground are the same net as transformer terminal 2. Do not join them to any other equipment that has its own connection to the transformer.

And from the same section: the fuses "protect the board traces and limit the
damage, but they do not make the unsafe cases safe for the cable or the
computer". A 3 A time-lag fuse can take between 0.2 s and 60 s to open at
8 A, and a USB cable's ground wire is thin.

A valve wire or a splice that leaks to earth outdoors also makes a path
across the transformer when an earthed computer is attached. That is one more
reason to pull the 24 VAC plug before connecting USB.

## Outdoor wiring and surges

Valve wires usually run tens of metres outdoors and pick up surges, for
example from nearby lightning.

- The design has a surge clamp on the 24 VAC input, on the valve common and on every output and sensor terminal, and a resettable fuse on every output.
- **None of this has been tested.** Surge immunity is a bench test the project has not done ([risk 6](design/hardware-design.md#20-design-risk-register)). Do not assume the protection is enough.
- Keep valve wires away from mains cables.
- Use cable made for irrigation or low-voltage burial, and waterproof splice connectors.
- A damaged board can fail with a valve on. The relay in the valve common is there for that case, and it has not been tested either.

## What the design does to limit harm

- **Outputs off by default.** With the processor in reset, unpowered, crashed or being flashed, no valve output has drive. This is enforced in hardware.
- **Keep-alive.** Firmware must keep sending pulses for any valve to be on. When they stop, drive goes within about a second and the valve-common relay opens. Two detectors with no shared part do this. See [how the fail-safe works](hardware/fail-safe.md).
- **Valve-common relay.** If an output switch fails short, the relay removes power from every valve.
- **Over-current cut-off.** A hardware latch removes drive within about 70 microseconds of a short.
- **Maximum run time.** Firmware keeps each zone under its maximum run time, counted across back-to-back runs, and then keeps it closed for a rest time (30 minutes by default). No schedule, app or automation can exceed it.
- **Self-tests.** Firmware tests both keep-alive detectors at start-up, before and after every run, and every 10 minutes between runs, and refuses to water when a test fails.

## Radio

The specification requires a radio module with its own FCC and CE modular
approval, and the design uses the ESP32-S3-WROOM-1 for that. A module
approval covers the module only. The finished board has not been tested or
certified for radio emissions or for any safety standard.

## Verification limits

Before any release, the project will state which checks were done (ERC, DRC,
simulation, calculation, firmware host tests) and which need a bench:
thermal behaviour, EMC, surge immunity and RF performance. The current list
is on the [status page](status.md).

## Reporting a safety problem

Open an issue at
[github.com/syntroniks/zoneflow/issues](https://github.com/syntroniks/zoneflow/issues).
