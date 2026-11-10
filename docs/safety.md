# Safety

!!! danger "Engineering prototype"
    ZoneFlow is in early design. It has not been built, bench-tested or
    certified. Revision A boards will be engineering prototypes until they have
    been bench-tested. Treat any board you build as an experiment. Do not rely
    on it for anything where a fault would cause damage or injury.

## Low voltage only

ZoneFlow is designed for **Class 2 (low voltage, power-limited) 24 VAC
circuits only**.

- Power the board only from a Class 2 sprinkler transformer rated 24 VAC.
- **Never connect mains voltage to the board.** Not to the AC input, not to a
  zone terminal, not to a sensor terminal. The board has no mains section, no
  mains isolation and no mains clearances.
- Never connect a DC supply to the 24 VAC input.
- If your valves or pump relay need mains power, switch them with a separate,
  correctly rated relay or contactor installed by someone qualified to do that
  work. Use the board's master output only to drive a Class 2 coil.
- Wire the transformer to the mains with a method that meets your local
  electrical rules. That work is outside this project. If you are not
  qualified, use a plug-in transformer or hire an electrician.

## Transformer selection

The design requires 24 VAC nominal, 50/60 Hz, from a Class 2 transformer.

1. Look up the running (holding) current of each valve on its datasheet.
2. Add up the current of the valves that run at the same time, plus the master
   valve or pump relay coil.
3. Choose a transformer rated above that total. Typical sprinkler transformers
   are rated 750 mA to 1 A. Larger ones are available. If your total is higher
   than the rating, use a bigger transformer.
4. Do not size the transformer for the board alone. The board's logic load is
   small next to the valves.

Board limits from the [specification](design/system-spec.md):

| Item | Limit |
|---|---|
| Load per output | 0.5 A continuous, 1.5 A inrush for 100 ms |
| Outputs on at once | Up to 2 zones plus master |
| Total continuous | 1.5 A |
| Input voltage | Must survive 32 VAC RMS continuous |

An unloaded transformer can output more than its rated 24 V. The 32 VAC RMS
figure covers that case. Check the open-circuit voltage of the transformer you
choose. These limits are design requirements, not measured results.

## Outdoor wiring and surges

Valve wires usually run tens of metres outdoors. Long outdoor cables pick up
voltage surges, for example from nearby lightning.

- The design specifies a surge clamp on the AC input, a clamp or snubber on
  every output, and ESD/TVS protection on every field terminal.
- **None of this has been tested.** Surge immunity needs a bench test that the
  project has not done. Do not assume the protection is adequate.
- Keep valve wires away from mains cables and from overhead power lines.
- Use cable made for irrigation or low-voltage burial.
- Consider unplugging the transformer during storms.
- A damaged board can fail with a valve stuck on. Know where your water shutoff
  is.

## Indoor use only

The enclosure and board are for indoor or garage wall mounting. They are **not
weatherproof**.

- Do not mount the controller outdoors or where it can get wet.
- Do not leave it where condensation or sprinklers can reach it.
- Run valve wires in through the bottom cable entry with strain relief, as the
  enclosure design intends. The enclosure has not been built yet.

## What the design does to limit harm

These are design rules from the specification. None has been verified on
hardware.

- Outputs are off when the processor is in reset, unpowered, crashed or being
  flashed. This is meant to be enforced in hardware.
- A hardware output-enable gates all output drivers. A stuck firmware loses it.
- Firmware enforces a maximum run time per zone that no schedule or API call
  can exceed.
- The board is meant to detect an open valve, a shorted valve and loss of 24 VAC.

## Radio

The specification requires an ESP32-S3 module that carries FCC and CE modular
approval. That covers the module only. The finished board has not been tested
or certified for radio emissions or for any safety standard.

## Verification limits

Before any release, the project will state which checks were done (ERC, DRC,
simulation, calculation, firmware host tests) and which need a bench: thermal
behaviour, EMC, surge immunity and RF performance.

## Reporting a safety problem

Open an issue at
[github.com/syntroniks/zoneflow/issues](https://github.com/syntroniks/zoneflow/issues).
