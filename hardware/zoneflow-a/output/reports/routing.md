# Routing of the board, as built

Written by `hwflow.routereport` from the board file. Lengths in millimetres. "By hand" is copper
written in `design/layout.py` or `design/board.toml`; "patch router" is the `[[patch]]` tracks that
`hwflow.patchroute` wrote into `design/patches.toml`; the rest is the autorouter's.

| Layer | By hand | Patch router | Autorouter |
|---|---|---|---|
| B.Cu | 1903 | 480 | 1650 |
| F.Cu | 1808 | 343 | 2720 |
| In2.Cu | 444 | 113 | 0 |

104 nets are routed by hand alone, 95 have autorouted or patched copper. 384 vias were placed by hand or by the plane fan-out, 164 by the autorouter and 91 by the patch router.

## Copper laid by the patch router

A maze router closed these after the autorouter. It keeps the clearances and knows nothing else:
look at each one.

- `/BTN_HOME` (Default): 1.6 mm, 0 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/Connectors/U0TXD` (Default): 15.8 mm, 3 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/CurrentSense/SA_N` (Default): 11.8 mm, 3 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/CurrentSense/SA_P` (Default): 2 mm, 0 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/ENC_SW` (Default): 7.3 mm, 0 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/Expander/RLY_A` (Power): 70.8 mm, 10 vias, narrowest 0.4 mm (class width 0.4 mm)
- `/Expander/RLY_ON` (Default): 3 mm, 1 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/I2C0_SDA` (Default): 58.1 mm, 8 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/ISENSE_LO` (Default): 25.9 mm, 1 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/MCU/STATUS_LED` (Default): 161.1 mm, 7 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/PGOOD` (Default): 12.3 mm, 2 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/SENS_5V` (Field): 61.8 mm, 4 vias, narrowest 0.4 mm (class width 0.4 mm)
- `/SENS_PWR_EN` (Default): 126 mm, 12 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/Sensors/N_RAIN` (Default): 155.9 mm, 13 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/T_RAIN` (Field): 22 mm, 1 vias, narrowest 0.4 mm (class width 0.4 mm)
- `/VB` (Default): 16.8 mm, 6 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/ZD_MV_N` (Default): 32.9 mm, 5 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/ZD_Z1_N` (Default): 2.2 mm, 0 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/ZD_Z2_N` (Default): 59.1 mm, 6 vias, narrowest 0.2 mm (class width 0.2 mm)
- `/ZD_Z8_N` (Default): 87.1 mm, 9 vias, narrowest 0.2 mm (class width 0.2 mm)
- `V_DRV` (Power): 2.4 mm, 0 vias, narrowest 0.4 mm (class width 0.4 mm)

## Tracks narrower than their net class asks

- `/I2C0_SDA` (Default): 0.2 mm of track, narrowest 0.12 mm, class width 0.2 mm
- `/ISENSE_LO` (Default): 0.2 mm of track, narrowest 0.15 mm, class width 0.2 mm
- `/USB/USB_DP` (USB): 0.2 mm of track, narrowest 0.1874 mm, class width 0.25 mm
- `/ZD_Z5_N` (Default): 0.4 mm of track, narrowest 0.1 mm, class width 0.2 mm

Total: 1.0 mm in 4 nets.

## Autorouted vias inside a surface-mount pad

None.

## Nets more than 2.5 times as long as their pins are apart

The distance is half the perimeter of the box round the pins of the net.

- `/Sensors/SP_B`: 39.1 mm of track for 6.6 mm
- `/ZD_Z1_N`: 105.2 mm of track for 41.5 mm

## Autorouted and patched tracks on In2.Cu

Each one is a slot in the pour of that layer.

- `/Connectors/U0TXD`: 3.8 mm
- `/CurrentSense/SA_N`: 7.1 mm
- `/Expander/RLY_A`: 12.4 mm
- `/I2C0_SDA`: 13.5 mm
- `/MCU/STATUS_LED`: 8.5 mm
- `/SENS_5V`: 18.5 mm
- `/Sensors/N_RAIN`: 20.7 mm
- `/VB`: 6.4 mm
- `/ZD_MV_N`: 11.1 mm
- `/ZD_Z2_N`: 1.6 mm
- `/ZD_Z8_N`: 9.3 mm

Total: 113 mm in 11 nets.

## Every net with autorouted copper

| Net | Class | Length | Pins apart | Vias | Layers | Narrowest |
|---|---|---|---|---|---|---|
| `/AC_ZC` | Default | 36.4 | 32.3 | 0 | F.Cu | 0.2 |
| `/BOOT` | Default | 140.7 | 89.8 | 2 | B.Cu, F.Cu | 0.2 |
| `/BTN_BACK` | Default | 149.7 | 112.3 | 1 | B.Cu, F.Cu | 0.2 |
| `/BTN_HOME` | Default | 111.6 | 103 | 2 | B.Cu, F.Cu | 0.2 |
| `/Connectors/QW_3V3` | Power | 27.7 | 16.7 | 2 | B.Cu, F.Cu | 0.4 |
| `/Connectors/U0TXD` | Default | 15.8 | 16.2 | 0 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/CurrentSense/SA_N` | Default | 15.1 | 10.9 | 0 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/CurrentSense/SA_P` | Default | 11.8 | 9.5 | 0 | F.Cu | 0.2 |
| `/CurrentSense/SB_N` | Default | 8.9 | 8.2 | 0 | F.Cu | 0.2 |
| `/CurrentSense/SB_P` | Default | 12.7 | 10.8 | 2 | B.Cu, F.Cu | 0.2 |
| `/CurrentSense/ZC_CLAMP` | Default | 10.9 | 6.4 | 0 | F.Cu | 0.2 |
| `/CurrentSense/ZC_DIV` | Default | 151.8 | 150.4 | 2 | B.Cu, F.Cu | 0.2 |
| `/DRV_EN` | Default | 27 | 19.7 | 1 | B.Cu, F.Cu | 0.2 |
| `/Display/BL_G` | Default | 9.7 | 7.7 | 0 | F.Cu | 0.2 |
| `/Display/BTN_BACK_SW` | Default | 14.7 | 15.2 | 0 | F.Cu | 0.2 |
| `/Display/ENC_A_SW` | Default | 5.8 | 6.9 | 0 | F.Cu | 0.2 |
| `/Display/ENC_B_SW` | Default | 8.8 | 6.9 | 0 | F.Cu | 0.2 |
| `/EN` | Default | 83.5 | 73.3 | 1 | B.Cu, F.Cu | 0.2 |
| `/ENC_A` | Default | 56.7 | 35.9 | 4 | B.Cu, F.Cu | 0.2 |
| `/ENC_SW` | Default | 92.6 | 38.7 | 6 | B.Cu, F.Cu | 0.2 |
| `/EXP_INT_N` | Default | 6.4 | 7.5 | 0 | F.Cu | 0.2 |
| `/Expander/KB_N0` | Default | 1.9 | 1.9 | 0 | F.Cu | 0.2 |
| `/Expander/KB_N1` | HiZ | 8 | 4.5 | 0 | F.Cu | 0.25 |
| `/Expander/RLY_A` | Power | 73.2 | 80.2 | 0 | B.Cu, F.Cu, In2.Cu | 0.4 |
| `/Expander/RLY_G1` | Default | 8.4 | 6 | 0 | F.Cu | 0.2 |
| `/Expander/RLY_G2` | HiZ | 12.8 | 10.8 | 0 | F.Cu | 0.25 |
| `/Expander/RLY_ON` | Default | 19.4 | 13.3 | 1 | B.Cu, F.Cu | 0.2 |
| `/Expander/VARIANT_N` | Default | 13.3 | 13.6 | 0 | F.Cu | 0.2 |
| `/FAULT` | Default | 26.3 | 18.8 | 2 | B.Cu, F.Cu | 0.2 |
| `/Failsafe/DRV_EN_N` | Default | 25.1 | 17.1 | 0 | F.Cu | 0.2 |
| `/Failsafe/FAULT_N` | Default | 32.4 | 18.5 | 3 | B.Cu, F.Cu | 0.2 |
| `/Failsafe/KA_REF` | Default | 3.5 | 3.7 | 0 | F.Cu | 0.2 |
| `/Failsafe/LED_FLT` | Default | 2.6 | 2.3 | 0 | F.Cu | 0.2 |
| `/Failsafe/OC_CMP` | Default | 17.4 | 19.3 | 0 | F.Cu | 0.2 |
| `/I2C0_SCL` | Default | 107 | 80.1 | 3 | B.Cu, F.Cu | 0.2 |
| `/I2C0_SDA` | Default | 135.3 | 80.8 | 2 | B.Cu, F.Cu, In2.Cu | 0.12 |
| `/ISENSE_HI` | Default | 32.6 | 23.3 | 2 | B.Cu, F.Cu | 0.2 |
| `/ISENSE_LO` | Default | 120 | 83.3 | 4 | B.Cu, F.Cu | 0.15 |
| `/KA_OK` | Default | 179.7 | 108.2 | 6 | B.Cu, F.Cu | 0.2 |
| `/MCU/LED_ST` | Default | 1.5 | 1.5 | 0 | F.Cu | 0.2 |
| `/MCU/STATUS_LED` | Default | 161.1 | 130.1 | 0 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/OC_FAULT_N` | Default | 124.1 | 125.3 | 8 | B.Cu, F.Cu | 0.2 |
| `/OC_TRIP_N` | Default | 141 | 109.5 | 2 | B.Cu, F.Cu | 0.2 |
| `/P06` | Default | 17.1 | 15.5 | 0 | F.Cu | 0.2 |
| `/P07` | Default | 15.4 | 16.8 | 0 | F.Cu | 0.2 |
| `/P16` | Default | 14 | 10.7 | 1 | B.Cu, F.Cu | 0.2 |
| `/P17` | Default | 14.5 | 12.2 | 1 | B.Cu, F.Cu | 0.2 |
| `/PGOOD` | Default | 12.3 | 10.8 | 0 | B.Cu, F.Cu | 0.2 |
| `/QW_SCL` | Default | 139.1 | 92.1 | 7 | B.Cu, F.Cu | 0.2 |
| `/QW_SDA` | Default | 116.9 | 90.1 | 7 | B.Cu, F.Cu | 0.2 |
| `/RLY_B` | Power | 12.3 | 12.1 | 0 | F.Cu | 0.4 |
| `/RTC/OSCI` | Default | 9.3 | 8.3 | 0 | F.Cu | 0.2 |
| `/RTC/OSCO` | Default | 8.3 | 4.3 | 0 | F.Cu | 0.2 |
| `/RTC/VBAT` | Default | 14.5 | 15.1 | 2 | B.Cu, F.Cu | 0.2 |
| `/RTC/VBAT_R` | Default | 2.1 | 2.1 | 0 | F.Cu | 0.2 |
| `/RTC/VDD_RTC` | Power | 14.1 | 12 | 0 | F.Cu | 0.4 |
| `/RTC_INT_N` | Default | 9.3 | 6.2 | 0 | F.Cu | 0.2 |
| `/SENS_5V` | Field | 123.9 | 91.2 | 2 | B.Cu, F.Cu, In2.Cu | 0.4 |
| `/SENS_AUX` | Default | 52.9 | 26.4 | 4 | B.Cu, F.Cu | 0.2 |
| `/SENS_FLT_N` | Default | 36.5 | 33.8 | 5 | B.Cu, F.Cu | 0.2 |
| `/SENS_PWR_EN` | Default | 131.9 | 87 | 0 | B.Cu, F.Cu | 0.2 |
| `/SENS_RAIN` | Default | 42.7 | 37 | 2 | B.Cu, F.Cu | 0.2 |
| `/Sensors/N_AUX` | Default | 132.6 | 121.1 | 10 | B.Cu, F.Cu | 0.2 |
| `/Sensors/N_FLOW` | Default | 25.9 | 25.4 | 2 | B.Cu, F.Cu | 0.2 |
| `/Sensors/N_RAIN` | Default | 164.7 | 124.5 | 0 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/Sensors/SP_A` | Field | 20.4 | 19.1 | 2 | B.Cu, F.Cu | 0.4 |
| `/Sensors/SP_B` | Field | 39.1 | 6.6 | 2 | B.Cu, F.Cu | 0.4 |
| `/Sensors/SP_ILIM` | Default | 9.8 | 7.4 | 2 | B.Cu, F.Cu | 0.2 |
| `/Sensors/SP_OUT` | Power | 19.9 | 18 | 0 | F.Cu | 0.4 |
| `/T_AUX` | Field | 59.8 | 53.3 | 3 | B.Cu, F.Cu | 0.4 |
| `/T_FLOW` | Field | 70.6 | 65.5 | 2 | B.Cu, F.Cu | 0.4 |
| `/T_RAIN` | Field | 52.7 | 55.4 | 0 | B.Cu, F.Cu | 0.4 |
| `/U0RXD` | Default | 74.8 | 55.1 | 3 | B.Cu, F.Cu | 0.2 |
| `/U0TXD_MCU` | Default | 57.6 | 39.3 | 4 | B.Cu, F.Cu | 0.2 |
| `/USB/CC1` | Default | 8.7 | 10 | 2 | B.Cu, F.Cu | 0.2 |
| `/USB/CC2` | Default | 9.6 | 9.9 | 0 | F.Cu | 0.2 |
| `/USB/USB_DM` | USB | 30.1 | 19.5 | 0 | F.Cu | 0.25 |
| `/USB/USB_DP` | USB | 23.5 | 20.3 | 0 | F.Cu | 0.1874 |
| `/USB/USB_SHLD` | Default | 21.9 | 14.7 | 0 | B.Cu, F.Cu | 0.2 |
| `/VB` | Default | 152.7 | 96.1 | 2 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/VH` | Default | 26.8 | 13.9 | 4 | B.Cu, F.Cu | 0.2 |
| `/VL` | Default | 13.7 | 8 | 2 | B.Cu, F.Cu | 0.2 |
| `/V_DRV_RB` | Default | 165.4 | 130.7 | 8 | B.Cu, F.Cu | 0.2 |
| `/ZD_MV_N` | Default | 43.5 | 36.5 | 0 | F.Cu, In2.Cu | 0.2 |
| `/ZD_Z1_N` | Default | 105.2 | 41.5 | 3 | B.Cu, F.Cu | 0.2 |
| `/ZD_Z2_N` | Default | 70.3 | 50.2 | 0 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `/ZD_Z3_N` | Default | 121.6 | 58.9 | 2 | B.Cu, F.Cu | 0.2 |
| `/ZD_Z4_N` | Default | 75.9 | 67.6 | 3 | B.Cu, F.Cu | 0.2 |
| `/ZD_Z5_N` | Default | 103.3 | 69.3 | 6 | B.Cu, F.Cu | 0.1 |
| `/ZD_Z6_N` | Default | 109 | 78 | 3 | B.Cu, F.Cu | 0.2 |
| `/ZD_Z7_N` | Default | 106.2 | 86.7 | 4 | B.Cu, F.Cu | 0.2 |
| `/ZD_Z8_N` | Default | 95.8 | 95.4 | 0 | B.Cu, F.Cu, In2.Cu | 0.2 |
| `V5` | Power | 245.9 | 158.3 | 2 | B.Cu, F.Cu | 0.4 |
| `V_DRV` | Power | 247.6 | 134.5 | 6 | B.Cu, F.Cu | 0.4 |
| `unconnected-(J6-MountPin-PadMP)` | Default | 5.6 | 5.6 | 0 | F.Cu | 0.2 |
