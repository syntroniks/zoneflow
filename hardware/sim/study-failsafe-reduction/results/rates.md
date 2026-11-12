# Keep-alive rate

Study, not the product. Written by `study.py`; do not edit.

The pin toggles from 50 ms with the phases named and never stops. `DRV_EN` is judged from 0.35 s to the end of the run.
A gap in `DRV_EN` resets the expander, so a valve bit does not survive it.

| keep-alive | corner | tW by formula, ms | DRV_EN |
|---|---|---|---|
| 1 Hz | short, 3.3 V | 147 | pulses: high 12 % of the time, 3 resets of the expander in 3.6 s |
| 1 Hz | nominal, 3.3 V | 235 | pulses: high 20 % of the time, 3 resets of the expander in 3.6 s |
| 1 Hz | long, 3.3 V | 289 | pulses: high 24 % of the time, 3 resets of the expander in 3.6 s |
| 2 Hz | short, 3.3 V | 147 | pulses: high 29 % of the time, 7 resets of the expander in 3.6 s |
| 2 Hz | nominal, 3.3 V | 235 | pulses: high 46 % of the time, 7 resets of the expander in 3.6 s |
| 2 Hz | long, 3.3 V | 289 | pulses: high 56 % of the time, 7 resets of the expander in 3.6 s |
| 3 Hz | short, 3.3 V | 147 | pulses: high 45 % of the time, 11 resets of the expander in 3.6 s |
| 3 Hz | nominal, 3.3 V | 235 | pulses: high 72 % of the time, 11 resets of the expander in 3.6 s |
| 3 Hz | long, 3.3 V | 289 | pulses: high 88 % of the time, 10 resets of the expander in 3.6 s |
| 5 Hz | short, 3.3 V | 147 | pulses: high 75 % of the time, 19 resets of the expander in 3.6 s |
| 5 Hz | nominal, 3.3 V | 235 | held without a gap |
| 5 Hz | long, 3.3 V | 289 | held without a gap |
| 10 Hz | short, 3.3 V | 147 | held without a gap |
| 10 Hz | nominal, 3.3 V | 235 | held without a gap |
| 10 Hz | long, 3.3 V | 289 | held without a gap |
| 25 Hz | short, 3.3 V | 147 | held without a gap |
| 25 Hz | nominal, 3.3 V | 235 | held without a gap |
| 25 Hz | long, 3.3 V | 289 | held without a gap |
| one edge per 20 ms | short, 3.3 V | 147 | held without a gap |
| one edge per 20 ms | nominal, 3.3 V | 235 | held without a gap |
| one edge per 20 ms | long, 3.3 V | 289 | held without a gap |
| 100 Hz, the firmware | short, 3.3 V | 147 | held without a gap |
| 100 Hz, the firmware | nominal, 3.3 V | 235 | held without a gap |
| 100 Hz, the firmware | long, 3.3 V | 289 | held without a gap |
| 1 ms high, 20 ms low | short, 3.3 V | 147 | held without a gap |
| 1 ms high, 20 ms low | nominal, 3.3 V | 235 | held without a gap |
| 1 ms high, 20 ms low | long, 3.3 V | 289 | held without a gap |
| 20 ms high, 1 ms low | short, 3.3 V | 147 | held without a gap |
| 20 ms high, 1 ms low | nominal, 3.3 V | 235 | held without a gap |
| 20 ms high, 1 ms low | long, 3.3 V | 289 | held without a gap |
| 1 kHz | short, 3.3 V | 147 | held without a gap |
| 1 kHz | nominal, 3.3 V | 235 | held without a gap |
| 1 kHz | long, 3.3 V | 289 | held without a gap |
| 100 kHz | short, 3.3 V | 147 | held without a gap |
| 100 kHz | nominal, 3.3 V | 235 | held without a gap |
| 100 kHz | long, 3.3 V | 289 | held without a gap |
