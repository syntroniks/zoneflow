<!-- Written by hardware/sim/failsafe/failsafe.py compare. Do not edit. -->

| Fault | Design document: stuck on | Simulation: stuck on | Design document: found by | Simulation: found by | |
|---|---|---|---|---|---|
| R29 open | none | none | 3 start | 9 stop; 3 start |  |
| C22 open | none | none | 3 start | 9 stop; 3 start |  |
| D7 pins 1 A and 3 common bridged (GND to KA_N1) | none | none | 3 start | 9 stop; 3 start |  |
| D7 pins 2 K and 3 common bridged (KA_N2 to KA_N1) | none | none | 3 start | 8 end; 3 start |  |
| D7 pin 1 A (GND) open | none | none | 3 start | 3 start |  |
| D7 pin 2 K (KA_N2) open | none | none | 3 start | 9 stop; 3 start |  |
| D7 pin 3 common (KA_N1) open | none | none | 3 start | 9 stop; 3 start |  |
| C23 short | none | none | 3 start | 7 run; 3 start |  |
| C24 short | none | none | 3 start | 7 run; 3 start |  |
| R35 open | none | none | 3 start | 7 run; 3 start |  |
| C23 open | none | none | 3 start | 7 run; 4 expander |  |
| R32 open | none | none | not found | not found |  |
| R33 open | none | none | not found | not found |  |
| R36 open | none | none | not found | not found |  |
| R30 open | none | none | 9 stop | 9 stop |  |
| R91 open | none | none | 9 stop | 9 stop |  |
| R31 open | gate drive (detector A) stuck on | gate drive stays, Q1 stays on | 9 stop | 9 stop; 3 start |  |
| R34 open | gate drive (detector A) stuck on | gate drive stays, Q1 stays on | 1 idle | 9 stop; 1 idle |  |
| C22 short | gate drive (detector A) stuck on | gate drive stays, Q1 stays on | 2 pin-high | 2 pin-high |  |
| U6 output 1 (KA_OK) never pulls low | gate drive (detector A) stuck on | gate drive stays, Q1 stays on | 1 idle | 9 stop; 1 idle |  |
| U6 pin 3 VCC (V5) open | gate drive (detector A) stuck on | gate drive stays, Q1 stays on | 1 idle | 9 stop; 1 idle |  |
| U6 pin 12 GND (GND) open | gate drive (detector A) stuck on | none | 1 idle | not found | **differs** |
| U7 output 3 (DRV_EN_N) stuck low | gate drive (detector A) stuck on | gate drive stays, Q1 stays on | 1 idle | 9 stop; 1 idle |  |
| U7 output 4 (DRV_EN) stuck high | gate drive (detector A) stuck on | Q1 stays on | 1 idle | 9 stop; 1 idle | **differs** |
| U7 output 3 (DRV_EN_N) stuck high | none | none | 3 start | 7 run; 3 start |  |
| U7 output 4 (DRV_EN) stuck low | none | none | 3 start | 7 run; 3 start |  |
| U7 output 2 (FAULT_N) stuck high | none | none | not found | not found |  |
| Q3 pins S and D bridged (3V3 to V_DRV) | none | none | not found | not found |  |
| R25 open | none | none | not found | not found |  |
| R26 open | none | none | not found | not found |  |
| C21 open | none | none | not found | not found |  |
| U5 RESET input ignored, bits kept | relay path (detector B, Q1, Q2 or K1) stuck on | Q1 stays on | 1 idle | 1 idle |  |
| U5 pin P00 (Z1) stuck low | none | none | 7 run | 4 expander | **differs** |
| U5 pin P05 (RLY_ON) stuck high | relay path (detector B, Q1, Q2 or K1) stuck on | Q1 stays on | 5 weld check | 4 expander | **differs** |
| R12 open | relay path (detector B, Q1, Q2 or K1) stuck on | Q1 stays on | 5 weld check | not found | **differs** |
| RN1 element 1-8 (ZD_Z1_N) open | none | none | not found | not found |  |
| R96 open | none | none | not found | not found |  |
| R89 open | none | none | 3 start | 3 start |  |
| C51 open | none | none | 3 start | 3 start |  |
| D22 pin 1 A (GND) open | none | none | 3 start | 3 start |  |
| D22 pin 2 K (RLY_G2) open | none | none | 3 start | 3 start |  |
| D22 pin 3 common (KB_N1) open | none | none | 3 start | 3 start |  |
| D22 pins 1 A and 3 common bridged (GND to KB_N1) | none | none | 3 start | 3 start |  |
| C50 short | none | none | 3 start | 7 run; 3 start |  |
| D22 pins 2 K and 3 common bridged (RLY_G2 to KB_N1) | none | none | 3 start | 9 stop; 3 start |  |
| C50 open | none | none | 3 start | 7 run | **differs** |
| R14 open | none | none | 9 stop | 9 stop |  |
| R90 open | none | none | 9 stop | 9 stop |  |
| C51 short | relay path (detector B, Q1, Q2 or K1) stuck on | Q2 stays on | 2 pin-high | 2 pin-high |  |
| Q2 pins S and D bridged (GND to RLY_B) | relay path (detector B, Q1, Q2 or K1) stuck on | Q2 stays on | 1 idle | 9 stop; 1 idle |  |
| Q2 pins G and D bridged (RLY_G2 to RLY_B) | relay path (detector B, Q1, Q2 or K1) stuck on | Q2 stays on | 1 idle | 9 stop; 1 idle |  |
| Q2 pins G and S bridged (RLY_G2 to GND) | none | none | 3 start | 7 run; 3 start |  |
| Q2 pin G (RLY_G2) open, holds its last level | none | Q2 stays on | 3 start | 9 stop; 3 start | **differs** |
| Q2 pin G (RLY_G2) open, leaks high | none | Q2 stays on | 3 start | 9 stop; 1 idle | **differs** |
| Q2 pin G (RLY_G2) open, leaks low | none | none | 3 start | 7 run; 3 start |  |
| Q1 pins S and D bridged (RLY_B to RLY_A) | relay path (detector B, Q1, Q2 or K1) stuck on | none | 5 weld check | 5 weld check | **differs** |
| Q1 pin D (RLY_A) open | none | none | valve current | valve current |  |
| Q1 pin S (RLY_B) open | none | none | valve current | valve current |  |
| R11 open | none | none | valve current | valve current; 4 expander |  |
| K1 contact welded, or JP1 bridged | relay path (detector B, Q1, Q2 or K1) stuck on | relay contact closed | 5 weld check | 5 weld check |  |
| K1 coil open | none | none | valve current | valve current |  |
| D5 open | none | none | not found | not found |  |
| D5 short | none | none | by its symptom | valve current |  |
| R93 open | none | none | 1 idle | 9 stop; 1 idle |  |
| R94 open | none | none | 1 idle | 9 stop; 1 idle |  |
| R95 short | none | none | 1 idle | 9 stop; 1 idle |  |
| R95 open | none | none | not found | not found |  |
| R92 open | none | none | 3 start | 7 run; 3 start |  |
| R29 short | none | none | not found | not found |  |
| R89 short | none | none | not found | not found |  |
| R31 short | none | none | not found | not found |  |
| R92 short | none | none | not found | not found |  |
| R94 short | none | none | not found | not found |  |
| R30 short | none | none | 3 start | 7 run; 3 start |  |
| R91 short | none | none | 3 start | 7 run; 3 start |  |
| R14 short | none | none | 3 start | 7 run; 3 start |  |
| R90 short | none | none | 3 start | 7 run; 3 start |  |
| R35 short | none | gate drive stays, Q1 stays on | 3 start | 9 stop; 1 idle | **differs** |
| R33 short | none | none | 3 start | 9 stop; 3 start |  |
| R34 short | none | none | 3 start | 7 run; 3 start |  |
| R32 short | gate drive (detector A) stuck on | gate drive stays, Q1 stays on | 1 idle | 9 stop; 1 idle |  |
| R36 short | gate drive (detector A) stuck on | gate drive stays, Q1 stays on | 1 idle | 9 stop; 1 idle |  |
| R93 short | none | none | by its symptom | 7 run; 5 weld check |  |
| R12 short | none | none | by its symptom | valve current |  |
| R11 short | none | none | not found | not found |  |
| GPIO of the gate-drive read-back driven high as an output by mistake | none | none | (no claim) | 9 stop; 1 idle |  |
| GPIO of the gate-drive read-back driven low as an output by mistake | none | none | (no claim) | 7 run; 3 start |  |
| GPIO of the relay read-back driven high as an output by mistake | none | none | (no claim) | 7 run; 3 start |  |
| GPIO of the relay read-back driven low as an output by mistake | none | none | (no claim) | 9 stop; 1 idle |  |
| GPIO of the over-current flag driven high as an output by mistake | none | none | (no claim) | not found |  |
| GPIO of the over-current flag driven low as an output by mistake | none | none | (no claim) | 7 run |  |
| KA bridged to AC_ZC anywhere on the board | relay path (detector B, Q1, Q2 or K1) stuck on | gate drive stays, Q1 stays on, Q2 stays on | by its symptom | not found | **differs** |
| KA bridged to TFT_BL (backlight PWM, keeps running), 0.01 ohm | both | gate drive stays, relay stays closed, **valve stays on** | not found | 9 stop; 1 idle | **differs** |
| KA bridged to TFT_BL (backlight PWM, keeps running), 1000 ohm | both | gate drive stays, relay stays closed, **valve stays on** | not found | not found |  |
| U3 shorted input to output: 3V3 at V5, 4.95 V. The processor is taken to keep running until the stop | none | none | (no claim) | 9 stop |  |
| V_BUCK falls to 0 V on 24 VAC alone: every rail goes | none | none | (no claim) | 7 run; 1 idle |  |
| triac, TVS, opto-triac output or snubber capacitor shorted | none | none | 6 relay | 6 relay |  |

97 rows of the design document, 12 differ.
