# Single faults of the new parts

Study, not the product. Written by `study.py`; do not edit.

Long corner, 3.3 V. The fault is present from power-up. "Stays on" means the signal was still present at the end of the run, 3 s after the stop.
The read-back column is what the idle test of the firmware would read: high with the keep-alive stopped is a failed test.

| fault | pin stops | valve ran | DRV_EN low after, ms | LED current under 0.5 mA after, ms | K1 coil under 3.6 mA after, ms | GPIO42 read-back 3 s after the stop |
|---|---|---|---|---|---|---|
| none | low | yes | 281 | 281 | 284 | low |
| none | high | yes | 291 | 291 | 294 | low |
| one timing resistor open | low | yes | 590 | 590 | 593 | low |
| one timing resistor open | high | yes | 601 | 601 | 603 | low |
| both timing resistors open | low | yes | **stays on** | **stays on** | **stays on** | high |
| both timing resistors open | high | yes | **stays on** | **stays on** | **stays on** | high |
| timing capacitor shorted | low | yes | **stays on** | **stays on** | **stays on** | high |
| timing capacitor shorted | high | yes | **stays on** | **stays on** | **stays on** | high |
| timing capacitor open | low | no | never on | never on | never on | low |
| timing capacitor open | high | no | never on | never on | never on | low |
| one-shot output stuck high | low | yes | **stays on** | **stays on** | **stays on** | high |
| one-shot output stuck high | high | yes | **stays on** | **stays on** | **stays on** | high |
| inverter transistor shorted drain to source | low | yes | 281 | 281 | 284 | high |
| inverter transistor shorted drain to source | high | yes | 291 | 291 | 294 | high |
| Q3 shorted source to drain | low | yes | 281 | 281 | 284 | high |
| Q3 shorted source to drain | high | yes | 291 | 291 | 294 | high |
| R25 open | low | yes | 281 | 281 | 284 | high |
| R25 open | high | yes | 291 | 291 | 294 | high |
| expander ignores its reset input | low | yes | 281 | 281 | **stays on** | low |
| expander ignores its reset input | high | yes | 291 | 291 | **stays on** | low |
| series resistor R_S open | low | no | never on | never on | never on | low |
| series resistor R_S open | high | no | never on | never on | never on | low |
| second supervisor output stuck low | low | no | never on | never on | never on | low |
| second supervisor output stuck low | high | no | never on | never on | never on | low |
