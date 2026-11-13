# An over-current trip on the clear input of the one-shot

Study, not the product. Written by `faults_wide.py` for the review of the study; do not edit.

Not in the study. The review's option: the window comparator of revision A (U6B, U6C behind R52 and C33) keeps its outputs on the net that becomes the
clear input of the one-shot. The run pulls CLR low through 60 ohm for the time named, 0.7 s into a run with the keep-alive toggling throughout,
as a comparator does while the current is above its trip level. The comparator and the current are not simulated here; `hardware/sim/isense.cir` has its 66 us.
What the run shows: the expander is reset and forgets the valve bit, and drive comes back with nothing switched on.
The results are printed every 0.05 ms, so a time of 0.05 ms means "by the next point".

| variant | CLR held low for | corner | DRV_EN low after the trip, ms | LED current under 0.5 mA after the trip, ms | DRV_EN high again after the trip, ms | highest LED current later than 20 ms after the trip, mA |
|---|---|---|---|---|---|---|
| V1 | 0.1 ms, pin low at the time | short, 3.3 V | 0.05 | 0.15 | 3.85 | 0.000 |
| V1 | 3 ms, across a rising edge | short, 3.3 V | 0.05 | 0.15 | 3.05 | 0.000 |
| V1 | 3 ms, pin high at the time | short, 3.3 V | 0.05 | 0.15 | 3.05 | 0.000 |
| V1 | 8 ms | short, 3.3 V | 0.05 | 0.15 | 8.05 | 0.000 |
| V1 | 0.1 ms, pin low at the time | long, 3.3 V | 0.05 | 0.15 | 3.85 | 0.000 |
| V1 | 3 ms, across a rising edge | long, 3.3 V | 0.05 | 0.15 | 3.05 | 0.000 |
| V1 | 3 ms, pin high at the time | long, 3.3 V | 0.05 | 0.15 | 3.05 | 0.000 |
| V1 | 8 ms | long, 3.3 V | 0.05 | 0.15 | 8.05 | 0.000 |
| V2 | 0.1 ms, pin low at the time | short, 3.3 V | 0.05 | 0.15 | 3.85 | 0.000 |
| V2 | 3 ms, across a rising edge | short, 3.3 V | 0.05 | 0.15 | 3.05 | 0.000 |
| V2 | 3 ms, pin high at the time | short, 3.3 V | 0.05 | 0.15 | 3.05 | 0.000 |
| V2 | 8 ms | short, 3.3 V | 0.05 | 0.15 | 8.05 | 0.000 |
| V2 | 0.1 ms, pin low at the time | long, 3.3 V | 0.05 | 0.15 | 3.85 | 0.000 |
| V2 | 3 ms, across a rising edge | long, 3.3 V | 0.05 | 0.15 | 3.05 | 0.000 |
| V2 | 3 ms, pin high at the time | long, 3.3 V | 0.05 | 0.15 | 3.05 | 0.000 |
| V2 | 8 ms | long, 3.3 V | 0.05 | 0.15 | 8.05 | 0.000 |
