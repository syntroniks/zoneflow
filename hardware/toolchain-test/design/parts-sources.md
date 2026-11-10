# Part number sources for the toolchain test

The test circuit is not a design; these parts were chosen only to fill the
`LCSC` field with real numbers. Each number was looked up on 2026-11-10 with a
web search and matched to the manufacturer part number on the listing named
in the last column. Stock and price were not checked. Electrical suitability
was not checked. Do not reuse this list for the ZoneFlow board without
reading the datasheets.

| LCSC | Manufacturer part | Used as | Listing read |
|---|---|---|---|
| C165948 | Korean Hroparts Elec TYPE-C-31-M-12 | J11 USB-C receptacle, 16 pin | https://lcsc.com/product-detail/C165948.html |
| C2913198 | Espressif ESP32-S3-WROOM-1-N8 | U31 | https://www.lcsc.com/product-detail/C2913198.html |
| C7519 | STMicroelectronics USBLC6-2SC6 | U11 USB ESD | https://www.lcsc.com/product-detail/C7519.html |
| C6186 | Advanced Monolithic Systems AMS1117-3.3, SOT-223 | U12 | https://www.lcsc.com/product-detail/C6186.html |
| C115469 | Lite-On MOC3023S-TA1, SMD-6 | U101, U201 opto-triac | https://www.lcsc.com/product-detail/C115469.html |
| C163595 | STMicroelectronics Z0103MN5AA4, SOT-223 | Q101, Q201 triac | https://www.lcsc.com/product-detail/C163595.html |
| C78420 | Brightking SMBJ33CA, SMB | D102, D202 clamp | https://www.lcsc.com/product-detail/C78420.html |
| C72043 | Everlight 19-217/GHC-YR1S2/3T, 0603 green | D101, D201 | https://lcsc.com/product-detail/C72043.html |
| C474952 | Cixi Kefa KF128-5.08-2P-AA, 2 pin 5.08 mm | J1, J101, J201 screw terminal | https://www.lcsc.com/product-detail/C474952.html |
| C2905435 | kinghelm KH-2.54PH180-1X4P-L11.5 | J31 1x4 header | https://www.lcsc.com/product-detail/C2905435.html |
| C2886898 | E-Switch TL3342F160QG | SW31, SW32 | https://www.lcsc.com/product-detail/C2886898.html |
| C23186 | UNI-ROYAL 0603WAF5101T5E, 5.1 k 0603 | R11, R12 | https://jlcpcb.com/partdetail/0603WAF5101T5E/C23186 |
| C25804 | UNI-ROYAL 0603WAF1002T5E, 10 k 0603 | R31 | https://jlcpcb.com/partdetail/0603WAF1002T5E/C25804 |
| C23162 | UNI-ROYAL 0603WAF4701T5E, 4.7 k 0603 | R33, R34 | https://jlcpcb.com/partdetail/0603WAF4701T5E/C23162 |
| C22962 | UNI-ROYAL 0603WAF2200T5E, 220 R 0603 | R101, R201 | https://jlcpcb.com/partdetail/0603WAF2200T5E/C22962 |
| C21190 | UNI-ROYAL 0603WAF1001T5E, 1 k 0603 | R102, R202 | https://jlcpcb.com/partdetail/0603WAF1001T5E/C21190 |
| C22828 | UNI-ROYAL 0603WAF1800T5E, 180 R 0603 | R103, R203 | https://jlcpcb.com/partdetail/23555-0603WAF1800T5E/C22828 |
| C17901 | UNI-ROYAL 1206W4F1000T5E, 100 R 1206 | R104, R204 | https://jlcpcb.com/partdetail/1206W4F1000T5E/C17901 |
| C15850 | Samsung CL21A106KAYNNNE, 10 uF 25 V 0805 | C11, C31 | https://www.lcsc.com/product-detail/C15850.html |
| C45783 | Samsung CL21A226MAQNNNE, 22 uF 25 V 0805 | C12 | https://www.lcsc.com/product-detail/C45783.html |
| C14663 | YAGEO CC0603KRX7R9BB104, 100 nF 50 V 0603 | C32 | https://www.lcsc.com/product-detail/_YAGEO-_C14663.html |
| C15849 | Samsung CL10A105KB8NNNC, 1 uF 50 V 0603 | C33 | https://www.lcsc.com/product-detail/C15849.html |
| C1710 | Samsung CL21B103KBANNNC, 10 nF 50 V 0805 | C101, C201 | https://jlcpcb.com/partdetail/CL21B103KBANNNC/C1710 |

Known mismatches between these parts and the KiCad footprints used, left
as they are because the footprints only have to exercise the flow:

- The terminal blocks use KiCad's Phoenix MKDS 1,5/2-5.08 footprint; the
  listed part is a KF128, not checked for the same pin layout.
- The opto-triac uses `SMDIP-6_W9.53mm`; the MOC3023S land pattern was not
  compared with it.
