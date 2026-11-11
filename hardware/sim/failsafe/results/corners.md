<!-- Written by hardware/sim/failsafe/failsafe.py corners. Do not edit. -->

## Every combination of extremes: 1440 runs, 0 did not simulate

- valve still on 3.5 s after the stop: 0 runs
- pin stops low: valve current ends after 0.017 to 0.837 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -1.5 mV/K, Schottky IS 6.6e-07, 20 ms high, 1 ms low, stops low; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops low) s; 0 of 480 runs later than 0.95 s
  - gate drive alone ends after 0.17 to 0.837 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 20/20 ms, stops low; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops low) s
  - relay alone may hold for 0.017 to 0.84 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -1.5 mV/K, Schottky IS 6.6e-07, 20 ms high, 1 ms low, stops low; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops low) s
  - earliest the relay can let go (coil below 16 mA): 0 to 0.839 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 1 ms high, 20 ms low, stops low; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops low) s
  - read-back times the firmware would measure: tA 0.171 to 0.838 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 20/20 ms, stops low; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops low) s, tB 0.005 to 1.45 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 1 ms high, 20 ms low, stops low; highest: 70 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 1.5e-08, 1/1 ms, stops low) s
- pin stops hiz: valve current ends after 0.017 to 0.837 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -1.5 mV/K, Schottky IS 6.6e-07, 20 ms high, 1 ms low, stops hiz; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops hiz) s; 0 of 480 runs later than 0.95 s
  - gate drive alone ends after 0.17 to 0.837 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 20/20 ms, stops hiz; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops hiz) s
  - relay alone may hold for 0.017 to 0.84 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -1.5 mV/K, Schottky IS 6.6e-07, 20 ms high, 1 ms low, stops hiz; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops hiz) s
  - earliest the relay can let go (coil below 16 mA): 0 to 0.839 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 1 ms high, 20 ms low, stops hiz; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops hiz) s
  - read-back times the firmware would measure: tA 0.171 to 0.838 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 20/20 ms, stops hiz; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops hiz) s, tB 0.005 to 1.45 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 1 ms high, 20 ms low, stops hiz; highest: 70 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 1.5e-08, 1/1 ms, stops hiz) s
- pin stops high: valve current ends after 0.029 to 1.25 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -1.5 mV/K, Schottky IS 6.6e-07, 20 ms high, 1 ms low, stops high; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops high) s; 0 of 480 runs later than 1.4 s
  - gate drive alone ends after 0.277 to 1.25 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -1.5 mV/K, Schottky IS 6.6e-07, 20 ms high, 1 ms low, stops high; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops high) s
  - relay alone may hold for 0.029 to 1.25 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -1.5 mV/K, Schottky IS 6.6e-07, 20 ms high, 1 ms low, stops high; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops high) s
  - earliest the relay can let go (coil below 16 mA): 0 to 1.25 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 1 ms high, 20 ms low, stops high; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops high) s
  - read-back times the firmware would measure: tA 0.278 to 1.25 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -1.5 mV/K, Schottky IS 6.6e-07, 20 ms high, 1 ms low, stops high; highest: 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops high) s, tB 0.01 to 2.18 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 20 ms high, 1 ms low, stops high; highest: 70 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 1.5e-08, 1/1 ms, stops high) s
- while toggling: lowest coil current -1.04e-05 to 57.5 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 1 ms high, 20 ms low, stops low; highest: -20 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -1.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops low) mA (the coil resistance moves this figure; the voltage across Q2, below, is the test)
- while toggling: lowest opto LED current 10.1 to 13.3 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 1 ms high, 20 ms low, stops low; highest: 70 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 1.5e-08, 1/1 ms, stops low) mA (needs 5.6 mA)
- while toggling: gate of Q2 above its threshold by -0.247 to 2.64 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 1 ms high, 20 ms low, stops low; highest: 70 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops low) V
- while toggling: reservoir of detector A at least 1.82 to 3.35 (lowest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -1.5 mV/K, Schottky IS 1.5e-08, 20/20 ms, stops low; highest: 70 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -1.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops low) V (releases below about 0.94 V)
- runs in which a read-back or the expander dropped out during the run: 39
- solver check: 0 runs repeated with a finer step, 0 still show the latch setting with no cause
- while toggling: highest voltage across Q2 0.00051 to 4.62 (lowest: 70 C, 3V3 3.6 V, small capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 1/1 ms, stops low; highest: -20 C, 3V3 3.0 V, small capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 1 ms high, 20 ms low, stops low) V (the coil loses this much)
- runs in which Q2 dropped more than 0.1 V while toggling, so that the relay was not fully driven: 48
  - 1 ms high, 1 ms low: gate of Q2 above its threshold by at least 0.42 V, relay not fully driven in 0 of 288 runs
  - 1 ms high, 20 ms low: gate of Q2 above its threshold by at least -0.25 V, relay not fully driven in 24 of 288 runs
  - 5 ms high, 5 ms low: gate of Q2 above its threshold by at least 0.38 V, relay not fully driven in 0 of 288 runs
  - 20 ms high, 1 ms low: gate of Q2 above its threshold by at least -0.22 V, relay not fully driven in 24 of 288 runs
  - 20 ms high, 20 ms low: gate of Q2 above its threshold by at least 0.08 V, relay not fully driven in 0 of 288 runs
- runs with LED current under 5.6 mA while toggling: 0
- at the firmware's 5 ms edges: gate-drive read-back high after 0.013 to 0.022 (lowest: 27 C, 3V3 3.6 V, small capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 6.6e-07, 5/5 ms, stops low; highest: -20 C, 3V3 3.0 V, small capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 1.5e-08, 5/5 ms, stops low) s, relay read-back low after 0.001 to 0.053 (lowest: 70 C, 3V3 3.0 V, small capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 1.5e-08, 5/5 ms, stops low; highest: -20 C, 3V3 3.0 V, large capacitors, Vth 1.45 V at -4.5 mV/K, Schottky IS 1.5e-08, 5/5 ms, stops low) s (limit 0.15 s); 0 of 288 runs over the limit
- at 5 ms edges, pin stops low: 0 of 96 runs outside the tA limits (0.15, 1.0), 0 outside the tB limits (0.05, 1.6)
- at 5 ms edges, pin stops hiz: 0 of 96 runs outside the tA limits (0.15, 1.0), 0 outside the tB limits (0.05, 1.6)

## Monte Carlo, seed 20261110: 400 runs, 0 did not simulate

- valve still on 3.5 s after the stop: 0 runs
- pin stops low: valve current ends after 0.0991 to 0.675 (lowest: sample 0; highest: sample 310) s; 0 of 133 runs later than 0.95 s
  - gate drive alone ends after 0.281 to 0.759 (lowest: sample 9; highest: sample 322) s
  - relay alone may hold for 0.0991 to 0.677 (lowest: sample 0; highest: sample 310) s
  - earliest the relay can let go (coil below 16 mA): 0.0791 to 0.676 (lowest: sample 0; highest: sample 310) s
  - read-back times the firmware would measure: tA 0.282 to 0.761 (lowest: sample 9; highest: sample 322) s, tB 0.145 to 0.923 (lowest: sample 0; highest: sample 220) s
- pin stops hiz: valve current ends after 0.116 to 0.679 (lowest: sample 15; highest: sample 146) s; 0 of 127 runs later than 0.95 s
  - gate drive alone ends after 0.284 to 0.797 (lowest: sample 126; highest: sample 27) s
  - relay alone may hold for 0.116 to 0.681 (lowest: sample 15; highest: sample 146) s
  - earliest the relay can let go (coil below 16 mA): 0.102 to 0.681 (lowest: sample 15; highest: sample 146) s
  - read-back times the firmware would measure: tA 0.285 to 0.799 (lowest: sample 126; highest: sample 27) s, tB 0.151 to 0.955 (lowest: sample 15; highest: sample 252) s
- pin stops high: valve current ends after 0.297 to 1.08 (lowest: sample 158; highest: sample 230) s; 0 of 140 runs later than 1.4 s
  - gate drive alone ends after 0.449 to 1.2 (lowest: sample 222; highest: sample 46) s
  - relay alone may hold for 0.297 to 1.08 (lowest: sample 158; highest: sample 230) s
  - earliest the relay can let go (coil below 16 mA): 0.275 to 1.02 (lowest: sample 158; highest: sample 230) s
  - read-back times the firmware would measure: tA 0.451 to 1.2 (lowest: sample 222; highest: sample 46) s, tB 0.35 to 1.26 (lowest: sample 158; highest: sample 349) s
- while toggling: lowest coil current 31.9 to 57 (lowest: sample 368; highest: sample 257) mA (the coil resistance moves this figure; the voltage across Q2, below, is the test)
- while toggling: lowest opto LED current 10.1 to 13.3 (lowest: sample 155; highest: sample 232) mA (needs 5.6 mA)
- while toggling: gate of Q2 above its threshold by 0.158 to 2.4 (lowest: sample 0; highest: sample 146) V
- while toggling: reservoir of detector A at least 2.17 to 3.31 (lowest: sample 270; highest: sample 294) V (releases below about 0.94 V)
- runs in which a read-back or the expander dropped out during the run: 0
- solver check: 0 runs repeated with a finer step, 0 still show the latch setting with no cause
- while toggling: highest voltage across Q2 0.000576 to 0.0161 (lowest: sample 294; highest: sample 0) V (the coil loses this much)
- runs in which Q2 dropped more than 0.1 V while toggling, so that the relay was not fully driven: 0
- runs with LED current under 5.6 mA while toggling: 0

## No toggling

| Keep-alive pin | Detector A enabled | Reservoir of A, highest (V) | Gate of Q2, highest (V) | Q2 above its lowest threshold |
|---|---|---|---|---|
| pin low from power-up | 0 of 24 runs | 0.02 | 0.00 | 0 of 24 runs |
| pin high from power-up | 0 of 24 runs | 1.63 | 1.58 | 12 of 24 runs |
| pin high-impedance from power-up | 0 of 24 runs | 0.02 | 0.00 | 0 of 24 runs |
| one rising edge, then high | 0 of 24 runs | 1.64 | 1.58 | 12 of 24 runs |
| one pulse of 60 us (a power-up glitch) | 0 of 24 runs | 0.14 | 0.03 | 0 of 24 runs |
| one pulse of 100 ms | 0 of 24 runs | 1.64 | 1.58 | 12 of 24 runs |
| 0.5 Hz | 0 of 24 runs | 1.64 | 1.58 | 12 of 24 runs |
| 1 Hz | 2 of 24 runs | 1.71 | 1.64 | 12 of 24 runs |
| 2 Hz | 9 of 24 runs | 1.98 | 1.85 | 12 of 24 runs |
| 3 Hz | 12 of 24 runs | 2.21 | 2.03 | 12 of 24 runs |
| 5 Hz | 13 of 24 runs | 2.45 | 2.29 | 16 of 24 runs |

