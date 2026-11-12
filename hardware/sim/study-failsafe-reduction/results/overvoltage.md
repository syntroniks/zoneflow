# The 3.3 V regulator shorts while a valve runs

Study, not the product. Written by `study.py`; do not edit.

U13 (TPS3701, threshold 4.04 V, 1 ms filter, as in revision A) holds the CLR input of the one-shot low. The pin keeps toggling: a processor
above its ratings may do that. Revision A removes drive through the over-current latch in this fault; here the same detector acts on CLR.

| case | corner | DRV_EN low after the fault, ms | LED current under 0.5 mA, ms | K1 coil under 3.6 mA, ms | highest DRV_EN later than 0.3 s after, V | highest LED current later than 0.3 s after, mA |
|---|---|---|---|---|---|---|
| 3V3 to 4.66 V, pin keeps toggling at 100 Hz | short, 3.3 V | 0.85 | 1.00 | 3.7 | 0.00 | 0.000 |
| 3V3 to 4.66 V, pin keeps toggling at 100 Hz | long, 3.3 V | 0.85 | 1.00 | 3.7 | 0.00 | 0.000 |
| 3V3 to 5.07 V, pin keeps toggling at 100 Hz | short, 3.3 V | 0.60 | 0.75 | 3.5 | 0.00 | 0.000 |
| 3V3 to 5.07 V, pin keeps toggling at 100 Hz | long, 3.3 V | 0.60 | 0.75 | 3.5 | 0.00 | 0.000 |
| 3V3 to 4.66 V, pin toggles at 1 kHz | short, 3.3 V | 0.85 | 1.00 | 3.7 | 0.00 | 0.000 |
| 3V3 to 4.66 V, pin toggles at 1 kHz | long, 3.3 V | 0.85 | 1.00 | 3.7 | 0.00 | 0.000 |
| 3V3 to 5.07 V, pin toggles at 1 kHz | short, 3.3 V | 0.60 | 0.75 | 3.5 | 0.00 | 0.000 |
| 3V3 to 5.07 V, pin toggles at 1 kHz | long, 3.3 V | 0.60 | 0.75 | 3.5 | 0.00 | 0.000 |
| 3V3 to 4.66 V, pin toggles at 25 Hz | short, 3.3 V | 0.85 | 1.00 | 3.7 | 0.00 | 0.000 |
| 3V3 to 4.66 V, pin toggles at 25 Hz | long, 3.3 V | 0.85 | 1.00 | 3.7 | 0.00 | 0.000 |
| 3V3 to 5.07 V, pin toggles at 25 Hz | short, 3.3 V | 0.60 | 0.75 | 3.5 | 0.00 | 0.000 |
| 3V3 to 5.07 V, pin toggles at 25 Hz | long, 3.3 V | 0.60 | 0.75 | 3.5 | 0.00 | 0.000 |
