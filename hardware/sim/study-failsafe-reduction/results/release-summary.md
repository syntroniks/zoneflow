# Release after the keep-alive stops: range over the corners

Study, not the product. Written by `study.py`; do not edit.

From `release.csv`: 81 runs, three ways of stopping times 27 corners (timing parts, 3.2 to 3.6 V, transistor thresholds, coil inductance).

| stop | what | earliest, ms | latest, ms |
|---|---|---|---|
| low | DRV_EN low | 139 | 281 |
| low | LED dark | 139 | 281 |
| low | K1 coil released | 141 | 287 |
| high | DRV_EN low | 149 | 291 |
| high | LED dark | 149 | 291 |
| high | K1 coil released | 151 | 297 |
| hiz | DRV_EN low | 139 | 281 |
| hiz | LED dark | 139 | 281 |
| hiz | K1 coil released | 141 | 287 |
