# Study: a reduced fail-safe

**This directory is a study. It is not the product.** The circuit it
simulates is not in `hardware/netlist.yaml`, no board has it, and nothing
here has been built or measured. It belongs to
[`docs/design/failsafe-reduction-study.md`](../../../docs/design/failsafe-reduction-study.md),
which asks what a smaller fail-safe than that of revision A would do.

The product's own fail-safe analysis is in `../failsafe/`. That one builds
its decks from the netlist. This one cannot, because its circuit is a
proposal.

## Run

    make          # everything below
    make sim      # the ngspice runs, 349 of them, about 5 minutes on a 16-core machine
    make cost     # parts, landed cost and area of the variants
    make area     # pads and courtyard areas from the board file, needs KiCad 10

`make sim` needs `ngspice` (47 was used) and `uv`. `make cost` needs `uv`
and uses the cost model of `tools/cost`. Results go to `results/` and are
committed.

    uv run study.py deck stop-high long    # print one deck, to read it

## What is simulated

Variant V2 of the study, the one it recommends.

- **The detector.** One retriggerable one-shot of the 74LVC1G123 type. The keep-alive pin, with its 2.2 kohm pull-down R33, goes to the rising-edge input. Two 100 kohm resistors in parallel and 4.7 uF set the pulse. The output, behind 1 kohm with 100 kohm to ground, is `DRV_EN`.
- **A second supply supervisor** of the type of U12, open drain, on `DRV_EN`. It holds `DRV_EN` low while `3V3` is under its threshold.
- **What `DRV_EN` does.** It is the reset input of the TCA9539. Through an N-MOSFET inverter it switches Q3, the opto LED supply. K1 follows the expander bit `RLY_ON` through Q1, so it opens when the expander resets.
- **U13**, the over-voltage detector of revision A, on the clear input of the one-shot.
- **One valve channel**: the opto LED, its indicator and their resistors.

## The models

Every model is behavioural. None is a vendor model.

| Part | Model | Figures it rests on |
|---|---|---|
| One-shot | A Schmitt input, an edge detector, a flip-flop and a timing capacitor that a trigger discharges and the resistors charge. The pulse ends when the capacitor reaches the level that makes the width K x R x C. | Nexperia 74LVC1G123 rev 8, 14 August 2023. Table 9 note 3: `tW = K x REXT x CEXT`, K = 1 above 10 nF. At 10 kohm and 0.1 uF: 1.0 ms typical, 1.05 ms at most, no minimum given. Table 8: VT+ 1.20 / 1.58 / 1.90 V and VT- 1.08 / 1.40 / 1.70 V at 3.0 to 3.6 V. Function table: a rising edge of CLR with B high also triggers. |
| One-shot below its supply range | Two cases. "Low below 1.0 V": the output is low until the supply passes 1.0 V. "Taken as high below 1.65 V": the bad case. | The sheet covers 1.65 to 5.5 V and lists "power-on reset on outputs". It does not say what the output does under 1.65 V. |
| Timing parts | 49.5 to 50.5 kohm, 3.3 to 5.2 uF, K 0.9 to 1.1 | The capacitor range is the one the design document uses for the same part number as C22 and C51. The range of K is this study's allowance, because the sheet has no minimum. |
| Supervisors | A switch that closes below the threshold, valid from 0.7 V. The 200 ms of delay after recovery is left out for the second one. For U12 the processor does not come back within a run. | TI SLVSES2J: option 30 is 3.08 V, accuracy -2 to +2 %, hysteresis 1.2 % typical, output defined from 0.7 V. |
| Expander | One memory cell for a valve bit and `RLY_ON`. A write sets it if reset is above 0.7 x VCC. Reset below 0.3 x VCC clears it, and so does a supply under 0.75 V. | TI SCPS202C, sections 6.3, 6.5 and 8.3.2. 0.3 and 0.75 V are the choices that keep the bits longest. |
| MOSFETs | Level 1. AO3400A 0.65 / 1.05 / 1.45 V, AO3401A 0.5 / 0.9 / 1.3 V. | As `../failsafe/model.py`. |
| Opto LED, relay, pin | The LED model, 125 ohm and an assumed 0.1 to 0.4 H for the coil, a 30 ohm pin. | As `../failsafe/model.py`. Omron gives no inductance. |
| U13 | Divider, 10 nF, a 0.4 V comparator with an open-drain output, off below 1.7 V of supply. | As `../failsafe/model.py`, from TI SBVS240C. |

What the readings mean:

- **LED dark**: the opto LED current is under 0.5 mA and stays there. The MOC3023 fires for certain at 5 mA and its sheet gives no current below which it does not, so 0.5 mA is this study's choice. The triac then conducts to the end of its half-cycle, up to 10 ms at 50 Hz.
- **K1 released**: the coil current is under 3.6 mA, the lowest current that may still hold the relay. The contacts part within the 10 ms release time after that.

## The runs

| Run | Question | Result file |
|---|---|---|
| `release` | How long after the last edge is drive gone, with the pin stopped low, stopped high and high-impedance, over 27 corners? | `results/release.md`, `release-summary.md` |
| `rates` | Which keep-alive rates hold drive, and what does a slow one do? | `results/rates.md` |
| `static` | Does anything enable with no keep-alive: supply ramps of 1 ms to 10 s, a pin tied high, one edge, one pulse? | `results/static.md` |
| `brownout` | A valve runs and `3V3` dips or decays. When is drive gone, and does it come back? With and without the second supervisor. | `results/brownout.md` |
| `overvoltage` | `3V3` goes to 4.66 or 5.07 V and the pin keeps toggling. | `results/overvoltage.md` |
| `bridges` | A net that toggles by itself, bridged onto the keep-alive net through 0 to 10 kohm while the pin is high-impedance. | `results/bridges.md` |
| `faults` | Eleven faults of the new parts, with a valve running and the keep-alive then stopping. | `results/faults.md` |

`cost.py` writes `results/variants.md`: parts removed and added, the landed
cost per controller at 5, 25 and 100 by the model of `tools/cost`, and the
courtyard area. `new-parts.csv` holds the JLCPCB rows of 2026-11-12 for the
part numbers revision A does not have. `variants.py` lists the parts of each
variant. `area.py` reads pads and courtyards from the board file.

## What this does not show

- **Nothing about a real part.** The one-shot's K at 50 kohm and 4.7 uF, its output under 1.65 V and its behaviour with a slow supply are not in its data sheet in the detail the model needs.
- **No fault injection of the depth of review A2.** That review injected 376 faults into the netlist of revision A. This study runs eleven faults of the parts it adds.
- **Variants V1 and V3 are not simulated.** V1 is V2 without Q3 and K1. V3 adds the relay detector of revision A, which `../failsafe/` covers.
- **No temperature.** The corners move the part values. The models have no temperature of their own.
