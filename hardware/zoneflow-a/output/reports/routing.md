# Routing of the board, as built

Written by `hwflow.routereport` from the board file. Lengths in millimetres. "By hand" is copper
written in `design/layout.py` or `design/board.toml`; "patch router" is the `[[patch]]` tracks that
`hwflow.patchroute` wrote into `design/patches.toml`; the rest is the autorouter's.

| Layer | By hand | Patch router | Autorouter |
|---|---|---|---|
| B.Cu | 1648 | 215 | 686 |
| F.Cu | 1541 | 82 | 1645 |
| In2.Cu | 459 | 60 | 2356 |

98 nets are routed by hand alone, 102 have autorouted or patched copper. 334 vias were placed by hand or by the plane fan-out, 159 by the autorouter and 25 by the patch router.

## Copper laid by the patch router

A maze router closed these after the autorouter. It keeps the clearances and knows nothing else:
look at each one.

- `/PGOOD` (Default): 10.1 mm, 2 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/VIN_MON` (Default): 119.8 mm, 10 vias, narrowest 0.2 mm (class width 0.2 mm)
- `3V3` (Power): 188.5 mm, 11 vias, narrowest 0.2 mm (class width 0.4 mm)
- `V5` (Power): 18.7 mm, 1 vias, narrowest 0.4 mm (class width 0.4 mm)
- `V_DRV` (Power): 20.3 mm, 1 vias, narrowest 0.4 mm (class width 0.4 mm)

## Tracks narrower than their net class asks

- `/BOOT` (Default): 2.8 mm of track, narrowest 0.15 mm, class width 0.2 mm
- `/EN` (Default): 0.5 mm of track, narrowest 0.12 mm, class width 0.2 mm
- `/FAULT` (Default): 1.8 mm of track, narrowest 0.15 mm, class width 0.2 mm
- `/I2C0_SCL` (Default): 0.2 mm of track, narrowest 0.12 mm, class width 0.2 mm
- `/T_FLOW` (Field): 8.1 mm of track, narrowest 0.24 mm, class width 0.4 mm
- `/ZD_Z1_N` (Default): 0.3 mm of track, narrowest 0.12 mm, class width 0.2 mm
- `/ZD_Z5_N` (Default): 0.2 mm of track, narrowest 0.12 mm, class width 0.2 mm
- `3V3` (Power): 21.7 mm of track, narrowest 0.2 mm, class width 0.4 mm
- `V_DRV` (Power): 2.6 mm of track, narrowest 0.3 mm, class width 0.4 mm

Total: 38.2 mm in 9 nets.

## Autorouted vias inside a surface-mount pad

None.

## Nets more than 2.5 times as long as their pins are apart

The distance is half the perimeter of the box round the pins of the net.

None.

## Autorouted tracks on In2.Cu

Each one is a slot in the pour of that layer.

- `/BOOT`: 61.6 mm
- `/BTN_BACK`: 108.2 mm
- `/BTN_HOME`: 90.6 mm
- `/CurrentSense/SA_P`: 7.4 mm
- `/CurrentSense/ZC_DIV`: 115.3 mm
- `/Display/BL_G`: 3.4 mm
- `/EN`: 33.9 mm
- `/ENC_A`: 9.8 mm
- `/Expander/RLY_A`: 37.2 mm
- `/Expander/RLY_ON`: 14 mm
- `/FAULT`: 4.4 mm
- `/Failsafe/FAULT_N`: 8.9 mm
- `/I2C0_SCL`: 70.9 mm
- `/I2C0_SDA`: 57.7 mm
- `/ISENSE_HI`: 11.6 mm
- `/ISENSE_LO`: 55.8 mm
- `/KA_OK`: 65.3 mm
- `/MCU/STATUS_LED`: 93.6 mm
- `/OC_FAULT_N`: 105.5 mm
- `/OC_TRIP_N`: 92.9 mm
- `/QW_SDA`: 4.6 mm
- `/RLY_B_RB`: 29.9 mm
- `/RTC/OSCO`: 4.7 mm
- `/SENS_AUX`: 22.1 mm
- `/SENS_PWR_EN`: 54 mm
- `/SENS_RAIN`: 37.5 mm
- `/Sensors/N_AUX`: 127.9 mm
- `/Sensors/N_RAIN`: 123.7 mm
- `/Sensors/SP_A`: 6.7 mm
- `/T_AUX`: 28.8 mm
- `/T_FLOW`: 71.5 mm
- `/T_RAIN`: 56.6 mm
- `/U0RXD`: 42.6 mm
- `/U0TXD_MCU`: 33 mm
- `/USB/CC1`: 3.5 mm
- `/USB/USB_SHLD`: 10.2 mm
- `/VB`: 50.2 mm
- `/VH`: 8.3 mm
- `/V_DRV_RB`: 99.5 mm
- `/ZD_Z1_N`: 7.4 mm
- `/ZD_Z2_N`: 17 mm
- `/ZD_Z3_N`: 26.3 mm
- `/ZD_Z4_N`: 40.4 mm
- `/ZD_Z5_N`: 31.6 mm
- `/ZD_Z6_N`: 44 mm
- `/ZD_Z7_N`: 44.3 mm
- `/ZD_Z8_N`: 33.7 mm
- `V5`: 158.3 mm
- `VBUS`: 19 mm
- `V_BUCK`: 53.4 mm
- `V_DRV`: 17.5 mm

Total: 2356 mm in 51 nets.

## Every net with autorouted copper

| Net | Class | Length | Pins apart | Vias | Layers | Narrowest |
|---|---|---|---|---|---|---|
| `/AC_ZC` | Default | 36.3 | 32.3 | 0 | F.Cu | 0.2 |
| `/BOOT` | Default | 94.7 | 89.8 | 2 | F.Cu, In2.Cu | 0.15 |
| `/BTN_BACK` | Default | 118.2 | 115.5 | 1 | F.Cu, In2.Cu | 0.2 |
| `/BTN_HOME` | Default | 104.3 | 96 | 2 | F.Cu, In2.Cu | 0.2 |
| `/Connectors/QW_3V3` | Power | 23.2 | 16.7 | 0 | F.Cu | 0.4 |
| `/Connectors/U0TXD` | Default | 13.7 | 16.2 | 0 | F.Cu | 0.2 |
| `/CurrentSense/SA_N` | Default | 20.5 | 10.9 | 2 | B.Cu, F.Cu | 0.2 |
| `/CurrentSense/SA_P` | Default | 13.3 | 9.5 | 2 | F.Cu, In2.Cu | 0.2 |
| `/CurrentSense/SB_N` | Default | 11.2 | 8.2 | 0 | F.Cu | 0.2 |
| `/CurrentSense/SB_P` | Default | 12.5 | 10.8 | 0 | F.Cu | 0.2 |
| `/CurrentSense/ZC_CLAMP` | Default | 5.9 | 6.4 | 0 | F.Cu | 0.2 |
| `/CurrentSense/ZC_DIV` | Default | 132.2 | 150.4 | 2 | F.Cu, In2.Cu | 0.2 |
| `/DRV_EN` | Default | 21.3 | 19.7 | 1 | B.Cu, F.Cu | 0.2 |
| `/Display/BL_G` | Default | 16.8 | 7.7 | 2 | F.Cu, In2.Cu | 0.2 |
| `/Display/BTN_BACK_SW` | Default | 14.7 | 15.2 | 0 | F.Cu | 0.2 |
| `/Display/BTN_HOME_SW` | Default | 13.5 | 11.7 | 0 | F.Cu | 0.2 |
| `/Display/ENC_A_SW` | Default | 5.8 | 6.9 | 0 | F.Cu | 0.2 |
| `/Display/ENC_B_SW` | Default | 8.8 | 6.9 | 0 | F.Cu | 0.2 |
| `/Display/ENC_SW_SW` | Default | 10.4 | 10.9 | 0 | F.Cu | 0.2 |
| `/EN` | Default | 78.4 | 73.3 | 1 | F.Cu, In2.Cu | 0.12 |
| `/ENC_A` | Default | 43.2 | 35.9 | 2 | F.Cu, In2.Cu | 0.2 |
| `/ENC_SW` | Default | 39.7 | 38.9 | 0 | F.Cu | 0.2 |
| `/EXP_INT_N` | Default | 6.4 | 7.5 | 0 | F.Cu | 0.2 |
| `/Expander/KB_N0` | Default | 1.9 | 1.9 | 0 | F.Cu | 0.2 |
| `/Expander/KB_N1` | HiZ | 8 | 4.5 | 0 | F.Cu | 0.25 |
| `/Expander/RLY_A` | Power | 71.8 | 80.2 | 4 | B.Cu, F.Cu, In2.Cu | 0.4 |
| `/Expander/RLY_G1` | Default | 9.4 | 6 | 0 | F.Cu | 0.2 |
| `/Expander/RLY_G2` | HiZ | 12.8 | 10.8 | 0 | F.Cu | 0.25 |
| `/Expander/RLY_ON` | Default | 16.5 | 13.3 | 1 | F.Cu, In2.Cu | 0.2 |
| `/Expander/VARIANT_N` | Default | 13.5 | 13.6 | 0 | F.Cu | 0.2 |
| `/FAULT` | Default | 28.4 | 18.8 | 2 | F.Cu, In2.Cu | 0.15 |
| `/Failsafe/DRV_EN_N` | Default | 22.3 | 17.1 | 0 | F.Cu | 0.2 |
| `/Failsafe/FAULT_N` | Default | 21.9 | 18.5 | 5 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/Failsafe/KA_REF` | Default | 3.5 | 3.7 | 0 | F.Cu | 0.2 |
| `/Failsafe/LED_FLT` | Default | 2.3 | 2.3 | 0 | F.Cu | 0.2 |
| `/Failsafe/OC_CMP` | Default | 20.5 | 19.3 | 0 | F.Cu | 0.2 |
| `/I2C0_SCL` | Default | 91 | 80.1 | 3 | F.Cu, In2.Cu | 0.12 |
| `/I2C0_SDA` | Default | 116.9 | 80.8 | 5 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/ISENSE_HI` | Default | 23.7 | 23.3 | 2 | F.Cu, In2.Cu | 0.2 |
| `/ISENSE_LO` | Default | 88.6 | 83.3 | 5 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/KA_OK` | Default | 133.3 | 108.2 | 8 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/MCU/LED_ST` | Default | 1.5 | 1.5 | 0 | F.Cu | 0.2 |
| `/MCU/STATUS_LED` | Default | 125.3 | 130.1 | 5 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/OC_FAULT_N` | Default | 114.1 | 125.3 | 4 | F.Cu, In2.Cu | 0.2 |
| `/OC_TRIP_N` | Default | 117.1 | 103.7 | 4 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/P06` | Default | 17.1 | 15.5 | 0 | F.Cu | 0.2 |
| `/P07` | Default | 16.6 | 16.8 | 0 | F.Cu | 0.2 |
| `/P16` | Default | 12.9 | 10.7 | 1 | B.Cu, F.Cu | 0.2 |
| `/P17` | Default | 14.4 | 12.2 | 1 | B.Cu, F.Cu | 0.2 |
| `/PGOOD` | Default | 10.1 | 10.8 | 0 | F.Cu, In2.Cu | 0.2 |
| `/QW_SCL` | Default | 101.8 | 92.1 | 4 | B.Cu, F.Cu | 0.2 |
| `/QW_SDA` | Default | 104.6 | 90.1 | 4 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/RLY_B` | Power | 19.2 | 12.1 | 0 | F.Cu | 0.4 |
| `/RLY_B_RB` | Default | 78.4 | 70.9 | 3 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/RTC/OSCI` | Default | 8.6 | 8.3 | 0 | F.Cu | 0.2 |
| `/RTC/OSCO` | Default | 9.8 | 4.3 | 2 | F.Cu, In2.Cu | 0.2 |
| `/RTC/VBAT` | Default | 13.7 | 15.1 | 2 | B.Cu, F.Cu | 0.2 |
| `/RTC/VBAT_R` | Default | 2.1 | 2.1 | 0 | F.Cu | 0.2 |
| `/RTC/VDD_RTC` | Power | 14.1 | 12 | 0 | F.Cu | 0.4 |
| `/RTC_INT_N` | Default | 7.3 | 6.2 | 0 | F.Cu | 0.2 |
| `/SENS_5V` | Field | 124.2 | 91.2 | 2 | B.Cu, F.Cu | 0.4 |
| `/SENS_AUX` | Default | 28 | 26.4 | 2 | F.Cu, In2.Cu | 0.2 |
| `/SENS_FLT_N` | Default | 33.1 | 33.8 | 3 | B.Cu, F.Cu | 0.2 |
| `/SENS_PWR_EN` | Default | 89.8 | 87 | 6 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/SENS_RAIN` | Default | 39.9 | 37 | 2 | F.Cu, In2.Cu | 0.2 |
| `/Sensors/N_AUX` | Default | 138.4 | 121.1 | 2 | F.Cu, In2.Cu | 0.2 |
| `/Sensors/N_FLOW` | Default | 29.9 | 25.4 | 0 | F.Cu | 0.2 |
| `/Sensors/N_RAIN` | Default | 145.7 | 124.5 | 2 | F.Cu, In2.Cu | 0.2 |
| `/Sensors/SP_A` | Field | 21.7 | 19.1 | 2 | F.Cu, In2.Cu | 0.4 |
| `/Sensors/SP_B` | Field | 10 | 6.6 | 0 | F.Cu | 0.4 |
| `/Sensors/SP_ILIM` | Default | 6.1 | 7.4 | 0 | F.Cu | 0.2 |
| `/Sensors/SP_OUT` | Power | 20.1 | 18 | 0 | F.Cu | 0.4 |
| `/T_AUX` | Field | 59.9 | 53.3 | 2 | B.Cu, F.Cu, In2.Cu | 0.4 |
| `/T_FLOW` | Field | 79.3 | 65.5 | 2 | F.Cu, In2.Cu | 0.24 |
| `/T_RAIN` | Field | 63.9 | 55.4 | 2 | F.Cu, In2.Cu | 0.4 |
| `/U0RXD` | Default | 45.6 | 55.1 | 1 | F.Cu, In2.Cu | 0.2 |
| `/U0TXD_MCU` | Default | 36.8 | 39.3 | 2 | F.Cu, In2.Cu | 0.2 |
| `/USB/CC1` | Default | 7.8 | 10 | 2 | F.Cu, In2.Cu | 0.2 |
| `/USB/CC2` | Default | 9.5 | 9.9 | 0 | F.Cu | 0.2 |
| `/USB/USB_DM` | USB | 29.3 | 19.2 | 0 | F.Cu | 0.25 |
| `/USB/USB_DP` | USB | 21.4 | 20.3 | 0 | F.Cu | 0.25 |
| `/USB/USB_SHLD` | Default | 22.1 | 14.7 | 0 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/VB` | Default | 128.8 | 96.1 | 4 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/VH` | Default | 12.7 | 13.9 | 2 | F.Cu, In2.Cu | 0.2 |
| `/VIN_MON` | Default | 123.8 | 115.8 | 0 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/VL` | Default | 9.4 | 8 | 0 | F.Cu | 0.2 |
| `/V_DRV_RB` | Default | 115.9 | 130.7 | 8 | F.Cu, In2.Cu | 0.2 |
| `/ZD_MV_N` | Default | 42.8 | 36.5 | 2 | B.Cu, F.Cu | 0.2 |
| `/ZD_Z1_N` | Default | 44.5 | 41.5 | 1 | B.Cu, F.Cu, In2.Cu | 0.12 |
| `/ZD_Z2_N` | Default | 65.3 | 50.2 | 1 | F.Cu, In2.Cu | 0.2 |
| `/ZD_Z3_N` | Default | 61 | 58.9 | 2 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/ZD_Z4_N` | Default | 68.3 | 67.6 | 2 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/ZD_Z5_N` | Default | 71.7 | 69.3 | 3 | B.Cu, F.Cu, In2.Cu | 0.12 |
| `/ZD_Z6_N` | Default | 80.9 | 78 | 3 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/ZD_Z7_N` | Default | 81.4 | 86.7 | 2 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/ZD_Z8_N` | Default | 89.2 | 95.4 | 2 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `3V3` | Power | 264.6 | 203.4 | 0 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `V5` | Power | 244.5 | 158.3 | 3 | F.Cu, In2.Cu | 0.4 |
| `VBUS` | Power | 72.5 | 54.5 | 4 | B.Cu, F.Cu, In2.Cu | 0.4 |
| `V_BUCK` | Power | 134.7 | 95.9 | 3 | F.Cu, In2.Cu | 0.4 |
| `V_DRV` | Power | 230 | 134.5 | 3 | B.Cu, F.Cu, In2.Cu | 0.3 |
| `unconnected-(J6-MountPin-PadMP)` | Default | 5.6 | 5.6 | 0 | F.Cu | 0.2 |
