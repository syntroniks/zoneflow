<!-- Written by hardware/sim/failsafe/failsafe.py faults. Do not edit. -->

The two columns on the right are for the self-test sequence of design section 19.7 as review A2 leaves it.

| Fault | Valve current ends after (s): pin low / high / high-impedance | Layer lost | Found while the valve runs | Found at the next start |
|---|---|---|---|---|
| **reference** | | | | |
| no fault | 0.50 / 0.76 / 0.50 |  |  |  |
| **module** | | | | |
| KA bridged to module pin 9 IO16 (ENC_B, contact open) | 0.50 / 0.76 / 0.50 |  |  |  |
| KA bridged to module pin 9 IO16 (ENC_B, contact worked at 20 Hz) | 0.50 / 0.76 / 0.50 |  |  |  |
| KA bridged to module pin 11 IO18 (BTN_BACK, contact open) | 0.50 / 0.76 / 0.50 |  |  |  |
| KA bridged to module pin 11 IO18 (BTN_BACK, contact worked at 20 Hz) | 0.47 / **stays on** / 0.52 | gate drive stays, relay stays closed |  | 2 pin-high: gate-drive read-back high with the pin held high |
