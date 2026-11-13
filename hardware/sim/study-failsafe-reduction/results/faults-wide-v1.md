# Wider fault injection on V1

Study, not the product. Written by `faults_wide.py` for the review of the study; do not edit.

Variant V1 (the expander reset only: no LED supply switch, no relay). 88 faults: 82 simulated (574 ngspice runs), 6 by reading only.
Each fault is present from power-up, except the pins that "open during a run", which open at 0.6 s. Long corner, 3.3 V, unless the row says otherwise;
the stop test is also run at the short corner. A valve bit is written at 0.35 s and the keep-alive stops at 1.0 s.

- **30 simulated faults leave the valve on** with nothing asking for it: after the keep-alive stops in at least one of the three ways, or from power-up. 21 of them are single faults of a component, a pin or a net. The other 9 are the push-pull bridges onto `KA` and the double faults.
- "Valve on" means opto LED current above 0.5 mA, the study's own limit. The MOC3023 fires for certain at 5 mA and its sheet gives no current below which it does not.
- Basis "model": the result depends on the behavioural model of the one-shot. "circuit": it does not. "reading": no run, the cell says why.
- "Found by" names tests a firmware could make with the read-back on GPIO42 and the expander's acknowledge. The expander is taken to answer while its reset input, as it sees it, is above 0.7 x VCC.
  idle: the read-back is high or the expander answers, with the pin driven low and no edge since power-up.
  float: the same with the pin high-impedance, or the pin reads high. pin-high: either is still high 0.5 s after the pin was driven high and left there.
  start: 150 ms after the first edge the read-back is low or the expander does not answer, or no valve current flows. stop: after a run, the read-back falls earlier than 0.1 s, or it is still high or the expander still answers 0.4 s after the last edge.
  The idle test here starts from power-up. A fault that sticks the one-shot only after its first trigger passes it; the stop test finds it at the end of the first run.
  None of these tests measures valve current. A valve that is on at rest is found by the current measurement with every output off, which the model does not run.
- "**on**" in a time column: still on at the end of the run, 3 s after the stop.

| id | group | fault | basis | result | valve current ends after the last edge: pin low / high / high-impedance, s | LED current at rest, mA | a valve ran | found by | note |
|---|---|---|---|---|---|---|---|---|---|
| none | reference | no fault | circuit | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found |  |
| wd_p1_high | U_WD one-shot, pins | pin 1 A open and leaking high, or stuck high | model | no valve runs | - / - / - | 0.00 | no | start | function table: A high inhibits the trigger |
| wd_p1_low | U_WD one-shot, pins | pin 1 A open and leaking low | reading | No effect: A is at ground in V2 anyway. If the open pin picks up a signal, each falling edge on A while KA is high is a trigger. Not established how a floating Schmitt input behaves here. | - | - | - | - |  |
| wd_p2_low | U_WD one-shot, pins | pin 2 B open and leaking low | model | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p2_high | U_WD one-shot, pins | pin 2 B open and leaking high | model | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p2_holds | U_WD one-shot, pins | pin 2 B opens during a run and holds its level | model | the valve drops during the run | - / - / - | 0.00 | yes | stop |  |
| wd_p3_low | U_WD one-shot, pins | pin 3 CLR open and leaking low | model | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p3_high | U_WD one-shot, pins | pin 3 CLR open and leaking high | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | U13 can no longer clear the one-shot |
| wd_p4_open | U_WD one-shot, pins | pin 4 GND open | reading | Not established. The chip has no ground and its output cannot be relied on to go low. The TI sheet says pin 6 'connects only to the external capacitor', so the board gives the chip no second ground. Count it as DRV_EN stuck high: the valve stays on, and the read-back shows it at rest. | - | - | - | - |  |
| wd_p5_high | U_WD one-shot, pins | pin 5 Q stuck high | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | idle, float, pin-high, stop |  |
| wd_p5_low | U_WD one-shot, pins | pin 5 Q stuck low | model | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p5_open | U_WD one-shot, pins | pin 5 Q open | circuit | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p6_open | U_WD one-shot, pins | pin 6 CEXT open | model | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p7_open_holds | U_WD one-shot, pins | pin 7 REXT/CEXT open, the internal node holds or leaks low | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | pin-high, stop | the TI sheet allows +-0.25 uA into this pin in either direction |
| wd_p7_open_high | U_WD one-shot, pins | pin 7 REXT/CEXT open, the internal node leaks high (0.25 uA) | model | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p8_open | U_WD one-shot, pins | pin 8 VCC open | reading | Not established. The inputs have no diode to VCC (they accept 5.5 V with VCC at 0 V). The timing pin may feed the chip through the timing resistors, 66 uA at most. Expected: Q never goes high, drive never enables, found at the start test. | - | - | - | - |  |
| wd_p12_bridge | U_WD one-shot, bridges between adjacent pins | pins 1 and 2 bridged (A to B: KA to ground) | circuit | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p23_bridge | U_WD one-shot, bridges between adjacent pins | pins 2 and 3 bridged (B to CLR: KA to the clear net) | model | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p34_bridge | U_WD one-shot, bridges between adjacent pins | pins 3 and 4 bridged (CLR to ground) | model | no valve runs | - / - / - | 0.00 | no | start |  |
| wd_p56_bridge | U_WD one-shot, bridges between adjacent pins | pins 5 and 6 bridged (Q to CEXT) | model | no valve runs | - / - / - | 0.00 | no | start | taken as Q to ground: both sheets draw CEXT as the grounded side of the capacitor |
| wd_p67_bridge | U_WD one-shot, bridges between adjacent pins | pins 6 and 7 bridged (the timing capacitor shorted) | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | pin-high, stop |  |
| wd_p78_bridge | U_WD one-shot, bridges between adjacent pins | pins 7 and 8 bridged (REXT/CEXT to VCC) | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | pin-high, stop | the model cannot end a pulse before the capacitor has been discharged |
| rt1_open | timing parts | R_T1 open | model | valve off | 0.59 / 0.60 / 0.59 | 0.00 | yes | pin-high, stop |  |
| rt1_short | timing parts | R_T1 shorted (a solder bridge across it: REXT/CEXT to 3V3) | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | pin-high, stop | the same node as pins 7 and 8 bridged |
| rt_open | timing parts | R_T1 and R_T2 both open, a double fault | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | pin-high, stop |  |
| ct_open | timing parts | C_T open | model | no valve runs | - / - / - | 0.00 | no | start |  |
| ct_short | timing parts | C_T shorted | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | pin-high, stop |  |
| ct_leak_22k | timing parts | leak of 22 kohm across C_T (a cracked capacitor, residue from REXT/CEXT to ground) | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | pin-high, stop |  |
| ct_leak_47k | timing parts | leak of 47 kohm across C_T (a cracked capacitor, residue from REXT/CEXT to ground) | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | pin-high, stop |  |
| ct_leak_100k | timing parts | leak of 100 kohm across C_T (a cracked capacitor, residue from REXT/CEXT to ground) | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | pin-high, stop |  |
| ct_leak_150k | timing parts | leak of 150 kohm across C_T (a cracked capacitor, residue from REXT/CEXT to ground) | model | valve off | 0.58 / 0.59 / 0.58 | 0.00 | yes | pin-high, stop |  |
| ct_leak_220k | timing parts | leak of 220 kohm across C_T (a cracked capacitor, residue from REXT/CEXT to ground) | model | valve off | 0.41 / 0.42 / 0.41 | 0.00 | yes | stop |  |
| ct_leak_470k | timing parts | leak of 470 kohm across C_T (a cracked capacitor, residue from REXT/CEXT to ground) | model | valve off | 0.32 / 0.33 / 0.32 | 0.00 | yes | not found |  |
| ct_leak_1meg | timing parts | leak of 1 Mohm across C_T (a cracked capacitor, residue from REXT/CEXT to ground) | model | valve off | 0.30 / 0.31 / 0.30 | 0.00 | yes | not found |  |
| rc_leak_47k_3v3 | timing parts | leak of 47 kohm from 3V3 to REXT/CEXT | model | valve off | 0.13 / 0.14 / 0.13 | 0.00 | yes | stop |  |
| rs_open | DRV_EN network and U_SV | R_S open | circuit | no valve runs | - / - / - | 0.00 | no | start |  |
| rs_short | DRV_EN network and U_SV | R_S shorted | circuit | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | U_SV then pulls against the output of the one-shot directly |
| rpd_open | DRV_EN network and U_SV | R_PD open | circuit | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found |  |
| rpd_short | DRV_EN network and U_SV | R_PD shorted | circuit | no valve runs | - / - / - | 0.00 | no | start |  |
| sv_low | DRV_EN network and U_SV | U_SV output stuck low, or its pins 1 and 2 bridged (DRV_EN to ground) | circuit | no valve runs | - / - / - | 0.00 | no | start |  |
| sv_never | DRV_EN network and U_SV | U_SV output open or never pulling, or U_SV without supply | circuit | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | with a good rail. The brownout run of study.py shows the same circuit on a low rail |
| sv_p23_bridge | DRV_EN network and U_SV | U_SV pins 2 and 3 joined (DRV_EN to 3V3; across the body, not adjacent) | circuit | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | idle, float, pin-high, stop |  |
| cdec_short | DRV_EN network and U_SV | C_DEC shorted | reading | 3V3 is shorted. The board does not start. | - | - | - | - |  |
| cdec_open | DRV_EN network and U_SV | C_DEC open | reading | No effect that the model shows. The discharge of 4.7 uF at each trigger then draws its current from the rail without local decoupling. | - | - | - | - |  |
| u13_low | U13 and the clear input | U13 output stuck low | model | no valve runs | - / - / - | 0.00 | no | start |  |
| u13_never | U13 and the clear input | U13 output open or never pulling | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | over-voltage protection lost |
| r53_open_low | U13 and the clear input | R53 open, the clear net leaks low | model | no valve runs | - / - / - | 0.00 | no | start |  |
| r53_open_high | U13 and the clear input | R53 open, the clear net leaks high | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found |  |
| r53_short | U13 and the clear input | R53 shorted (the clear net to 3V3) | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | U13 can no longer clear the one-shot |
| r13_open | U13 and the clear input | R13 open | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | over-voltage protection lost |
| r13_short | U13 and the clear input | R13 shorted | model | no valve runs | - / - / - | 0.00 | no | start |  |
| r100_open | U13 and the clear input | R100 open | model | no valve runs | - / - / - | 0.00 | no | start |  |
| r100_short | U13 and the clear input | R100 or C55 shorted | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | over-voltage protection lost |
| r92_open | read-back | R92 open | circuit | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | start | the read-back reads low always |
| r92_short | read-back | R92 shorted | circuit | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | a mis-driven GPIO42 is no longer limited |
| gpio42_high | read-back | GPIO42 driven high by mistake (a firmware fault, not a component) | circuit | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | idle, float, pin-high, stop | through R92 onto DRV_EN, against the 1 kohm of R_S |
| u5_rst_low | U5 expander | U5 pin 3 RESET open and leaking low | circuit | no valve runs | - / - / - | 0.00 | no | start |  |
| u5_rst_high | U5 expander | U5 pin 3 RESET open and leaking high | circuit | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | idle, float, pin-high, stop |  |
| u5_rst_holds | U5 expander | U5 pin 3 RESET opens during a run and holds its level | circuit | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | stop |  |
| u5_rst_ignored | U5 expander | U5 ignores its reset input | circuit | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | idle, float, pin-high, stop |  |
| u5_p00_low | U5 expander | U5 valve pin stuck low, or a valve net shorted to ground | circuit | **VALVE ON WITH NOTHING ASKED FOR** | **on** / **on** / **on** | 13.85 | yes | not found |  |
| zd_leak_220 | U5 expander | leak of 220 ohm from a valve net to ground | circuit | **VALVE ON WITH NOTHING ASKED FOR** | **on** / **on** / **on** | 5.72 | yes | not found | LED current, not valve current: the MOC3023 fires for certain at 5 mA and the sheet gives no current below which it does not |
| zd_leak_1k | U5 expander | leak of 1 kohm from a valve net to ground | circuit | **VALVE ON WITH NOTHING ASKED FOR** | **on** / **on** / **on** | 1.92 | yes | not found | LED current, not valve current: the MOC3023 fires for certain at 5 mA and the sheet gives no current below which it does not |
| zd_leak_2.2k | U5 expander | leak of 2.2 kohm from a valve net to ground | circuit | **VALVE ON WITH NOTHING ASKED FOR** | **on** / **on** / **on** | 0.95 | yes | not found | LED current, not valve current: the MOC3023 fires for certain at 5 mA and the sheet gives no current below which it does not |
| zd_leak_4.7k | U5 expander | leak of 4.7 kohm from a valve net to ground | circuit | valve off | 0.28 / 0.29 / 0.28 | 0.46 | yes | not found | LED current, not valve current: the MOC3023 fires for certain at 5 mA and the sheet gives no current below which it does not |
| u5_p34_bridge | U5 expander | U5 pins 3 and 4 bridged (RESET to P00: DRV_EN to the valve net of zone 1) | circuit | **VALVE ON WITH NOTHING ASKED FOR** | **on** / **on** / **on** | 1.92 | no | start |  |
| u5_vcc_open | U5 expander | U5 pin 24 VCC or pin 12 GND open | reading | Not established what the part does. In V1 nothing else stands between the valve pins and the opto LEDs: count it as able to leave a valve on. | - | - | - | - |  |
| ka_gnd | nets shorted to a rail | KA shorted to ground, or R33 shorted | circuit | no valve runs | - / - / - | 0.00 | no | start |  |
| ka_3v3 | nets shorted to a rail | KA shorted to 3V3 | model | no valve runs | - / - / - | 0.00 | no | float, start |  |
| r33_open | nets shorted to a rail | R33 open | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | the net floats when the pin does |
| clr_3v3 | nets shorted to a rail | the clear net shorted to 3V3 | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found | as R53 shorted |
| qo_3v3 | nets shorted to a rail | the output of U_WD shorted to 3V3 | model | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | idle, float, pin-high, stop |  |
| drven_3v3 | nets shorted to a rail | DRV_EN shorted to 3V3 | circuit | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | idle, float, pin-high, stop |  |
| drven_gnd | nets shorted to a rail | DRV_EN shorted to ground | circuit | no valve runs | - / - / - | 0.00 | no | start |  |
| drven_leak_1k | nets shorted to a rail | leak of 1 kohm from 3V3 to DRV_EN | circuit | **VALVE STAYS ON** | **on** / **on** / **on** | 0.00 | yes | stop |  |
| drven_leak_4k7 | nets shorted to a rail | leak of 4.7 kohm from 3V3 to DRV_EN | circuit | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found |  |
| ka_leak_1k | nets shorted to a rail | leak of 1 kohm from 3V3 to KA | model | valve off | 0.28 / 0.29 / 0.29 | 0.00 | yes | not found |  |
| ka_encb | bridges onto KA | KA bridged to ENC_B or BTN_BACK (module pins 9 and 11): a contact behind 3.3 kohm and 10 kohm, closing at 20 Hz | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found |  |
| ka_back_no_r72 | bridges onto KA | the same with the 10 kohm series resistor shorted as well, a double fault | model | **VALVE STAYS ON** | 0.28 / 0.29 / **on** | 0.00 | yes | float |  |
| ka_pp_0 | bridges onto KA | KA bridged through 0 ohm to a push-pull 3.3 V output at 1 kHz (30 ohm source), easiest thresholds | model | **VALVE STAYS ON** | **on** / 0.32 / **on** | 0.00 | yes | idle, float, stop |  |
| ka_pp_100 | bridges onto KA | KA bridged through 100 ohm to a push-pull 3.3 V output at 1 kHz (30 ohm source), easiest thresholds | model | **VALVE STAYS ON** | 0.28 / 0.29 / **on** | 0.00 | yes | float |  |
| ka_pp_470 | bridges onto KA | KA bridged through 470 ohm to a push-pull 3.3 V output at 1 kHz (30 ohm source), easiest thresholds | model | **VALVE STAYS ON** | 0.28 / 0.29 / **on** | 0.00 | yes | float |  |
| ka_pp_1000 | bridges onto KA | KA bridged through 1000 ohm to a push-pull 3.3 V output at 1 kHz (30 ohm source), easiest thresholds | model | **VALVE STAYS ON** | 0.28 / 0.29 / **on** | 0.00 | yes | float |  |
| ka_pp_2200 | bridges onto KA | KA bridged through 2200 ohm to a push-pull 3.3 V output at 1 kHz (30 ohm source), easiest thresholds | model | **VALVE STAYS ON** | 0.28 / 0.29 / **on** | 0.00 | yes | float |  |
| ka_pp_3300 | bridges onto KA | KA bridged through 3300 ohm to a push-pull 3.3 V output at 1 kHz (30 ohm source), easiest thresholds | model | **VALVE STAYS ON** | 0.28 / 0.29 / **on** | 0.00 | yes | float |  |
| ka_pp_4700 | bridges onto KA | KA bridged through 4700 ohm to a push-pull 3.3 V output at 1 kHz (30 ohm source), easiest thresholds | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found |  |
| ka_pp_10000 | bridges onto KA | KA bridged through 10000 ohm to a push-pull 3.3 V output at 1 kHz (30 ohm source), easiest thresholds | model | valve off | 0.28 / 0.29 / 0.28 | 0.00 | yes | not found |  |
| ka_pp_0_hard | bridges onto KA | the same through 0 ohm, hardest thresholds (1.90 V rising on a 3.2 V rail) | model | **VALVE STAYS ON** | 0.32 / **on** / **on** | 0.00 | yes | float, pin-high |  |
