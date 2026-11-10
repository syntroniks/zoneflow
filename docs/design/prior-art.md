# ZoneFlow: prior-art survey of open 24VAC irrigation controllers

Research date: 2026-11-10. Method: web search, project pages, GitHub repositories (read with `gh`), and schematics. I downloaded the PDFs and Eagle `.sch` files and read part values and nets from them.

Legend: **unverified** means I could not confirm the item from a primary source. Where a claim comes only from a search-result summary because the page returned HTTP 403 (opensprinkler.com, hunterirrigation.com), the citation is marked *(search summary, page 403)*. Schematic topology marked "from netlist" is my reading of the Eagle file, not a statement by the author.

---

## 0. Summary (5-minute read)

- **OpenSprinkler is the reference design.** It has run for about 10 years with one topology: a half-wave rectifier, logic ground tied to one 24VAC leg, triacs driven directly from an I²C IO expander, per-zone RC snubber and SMBJ43CA TVS, and one low-side shunt in the shared triac return for total-current sensing. **OpenSprinkler 4 (ESP32-C6) started shipping in Sep/Oct 2026 at €239.** The public hardware repo still stops at 3.4, so I found no OS4 schematic.
- **The OpenSprinkler firmware is GPL-3.0, already runs on ESP32-C6, and has a board-profile layer** (`boards/board_profile.h`). A port that adds a "ZoneFlow" profile is the realistic way to get compatibility with the OpenSprinkler app and the Home Assistant integration. Emulating the API from scratch is the harder route.
- **Open ESP32 boards are mostly relay boards with no protection and no current sensing.** Three ESP32 triac designs stand out: hwstar's 9-valve board (now retired), irriBRANT (ESP32-C6, opto-triac, MOV and NTC), and SprinklerBoy (opto, MAC97A8, SMBJ43CA, PCB spark gaps). Only hwstar has a hardware watchdog that disconnects the valves.
- **The field failures repeat across projects:** triacs blown by shorted or lightning-hit wiring, regulators stressed by unloaded transformers at 28–30 VAC or more, valves pulsed at boot by GPIO or IO-expander glitches, ESP crashes that leave the master valve on, and WiFi/mesh lock-in.
- **No project combines everything ZoneFlow plans.** Nobody pairs per-zone open/short diagnosis, a flow meter with leak alarms, BLE provisioning, a hardware fail-safe, both ESPHome and OpenSprinkler-compatible firmware, and a Matter Water Valve path.

---

## 1. Survey table

### 1a. Open projects: identity, status, licence, price

| Project | URL | MCU | Zones | Licence | Firmware | Status (last push/commit) | Price |
|---|---|---|---|---|---|---|---|
| OpenSprinkler 3.0–3.3 AC (modular: master + AC driver) | [hw repo](https://github.com/OpenSprinkler/OpenSprinkler-Hardware/tree/master/OS/3.0-3.3) | ESP8266 | 8 (+expanders) | HW CERN-OHL-S-2.0; FW GPL-3.0 ([repo](https://github.com/OpenSprinkler/OpenSprinkler-Firmware)) | OpenSprinkler unified FW | Superseded by 3.4 | unverified |
| OpenSprinkler 3.4 AC | [schematic PDF](https://github.com/OpenSprinkler/OpenSprinkler-Hardware/blob/master/OS/3.4/AC/OS34AC_Schematic.pdf) | ESP-12F (ESP8266) | 8, expandable | as above | as above; FW 2.2.1(7) released 2026-11-06 | Active; HW repo last push 2026-04-25 | unverified |
| **OpenSprinkler 4 AC** | [shop page](https://opensprinklershop.de/en/product/opensprinkler-4/), [announcement](https://opensprinklershop.de/en/2026/10/07/opensprinkler-4-ab-16-10-2026-verfuegbar) | ESP32-C6, 8 MB flash | 8, expandable to 72 | FW GPL-3.0; HW schematic not published (unverified) | same FW; uses Arduino core 3 via pioarduino ([issue #442](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/442)) | Shipping since Sep/Oct 2026; DC version planned for Nov 2026 ([search summary of opensprinkler.com blog](https://opensprinkler.com/introducing-ac-powered-opensprinkler-v4/), page 403) | €239 incl. VAT |
| OpenSprinkler Pi 2.0 (Raspberry Pi HAT) | [schematic PDF](https://github.com/OpenSprinkler/OpenSprinkler-Hardware/blob/master/OSPi/2.0/ospi20_schematic.pdf) | Raspberry Pi | 8 | CERN-OHL-S-2.0 | OS unified FW (Linux) | Active (PDF dated 2024-09) | unverified |
| OpenSprinkler Bee 3.0 | [HW repo](https://github.com/OpenSprinkler/OSBeeWiFi-Hardware), [FW](https://github.com/OpenSprinkler/OSBeeWiFi-Firmware) | ESP8266 | 3 (latching DC) | FW GPL-3.0; HW licence not stated | OSBee FW | HW last push 2023-06-05; FW 2026-11-01 | $62–72 ([search summary of product page](https://opensprinkler.com/product/opensprinkler-bee/), page 403) |
| hwstar / Home Control Labs 9-Valve | [repo](https://github.com/hwstar/9-Valve-Sprinkler-Controller), [Tindie](https://tindie.com/products/home-control-labs/9-valve-sprinkler-controller-works-with-esphome/) | ESP32 | 9 | ESPHome licence (MIT/GPL) for FW; HW not stated | ESPHome + custom `valve_dog` component | Last commit 2026-09-12; **Tindie product retired** | was sold as a kit; price unverified |
| irriBRANT | [repo](https://github.com/Renbrant/ESP32-C6-Irrigation-Controller) | XIAO ESP32-C6 | 9 (zone 9 can be master) | MIT | ESPHome, on-device scheduler | Last commit 2026-07-03; PCB v1.2 "field validated" | DIY |
| SprinklerBoy | [Hackster](https://www.hackster.io/carletz7/sprinklerboy-1a6067), [repo](https://github.com/carletz/Sprinkler-Boy) | ESP32-WROOM-32UE + LAN8720A Ethernet | 8 | GPL-3.0 | ESPHome | Last commit 2025-09-23 | DIY (JLC-assembly oriented) |
| 30hours/irrigation | [repo](https://github.com/30hours/irrigation) | ESP32-C3 | 8 | MIT | ESPHome | Last commit 2025-01-25 | DIY (itemised AUD BOM in hardware README) |
| zioCristia smart-irrigation-control-unit | [repo](https://github.com/zioCristia/smart-irrigation-control-unit) | ESP32 (on separate host board) | 7 + pump relay | MIT | ESPHome | Last commit 2025-05-25 | DIY |
| Smart Garden (JGAguado) | [Hackaday.io](https://www.hackaday.io/project/191412-smart-garden), [repo](https://github.com/JGAguado/Smart_Garden), [Tindie](https://www.tindie.com/products/jgaguado/smart-garden) | ESP32-S2 | 4 (relays) | not stated | ESPHome | Last commit 2023-04-12; Tindie sold out since 2025-06-03 | $34.99 |
| BeaverDam ESP32 Sprinkler | [ESPHome devices](https://devices.esphome.io/devices/BeaverDam-Sprinkler), [repo](https://github.com/thebeaverdam/ESP32_SprinklerSystem) | ESP32 | 8 (relays) | none in repo | ESPHome (scheduler in YAML) | Last commit 2026-08-10 | unverified |
| LazyGardener | [repo](https://github.com/pilotak/LazyGardener) | ESP32 | 6 (relays) | MIT | Arduino + MQTT | **Abandoned** (2018-06-01) | — |
| Relay boards used for irrigation (KinCony KC868-A8/E16S, ES32A08, LilyGo T-Relay S3) | e.g. [makstech/esphome-irrigation-system](https://github.com/makstech/esphome-irrigation-system), [HA KC868-A8 thread](https://community.home-assistant.io/t/diy-8-station-irrigation-based-on-esphome-on-kc868-a8-relay-board/459599) | ESP32 / S3 | 8–16 | various | ESPHome | makstech last commit 2025-12-09 | commodity boards |
| ESPrinkler32 | [site](https://esprinkler32-x5o.pages.dev/) | ESP32-S3 touch panel + MCP23017 relay board | 8 | no repo found | custom | Prototype; unverified | — |

Firmware-only or ESP8266 projects, listed for completeness: [ESPyRain](https://community.home-assistant.io/t/espyrain-local-esp32-sprinkler-controller-with-home-assistant-and-autonomous-scheduling/1026737), [esp32-sprinkler-queue](https://github.com/cristoforocervino/esp32-sprinkler-queue) (MIT; ESPHome component with HA valve entities and a one-zone-at-a-time guarantee), [Irrigo](https://github.com/gcarmix/Irrigo) (GPL-3.0, last commit 2023-06-08), and [ESPrinkler2](https://github.com/lawabb/ESPrinkler2) (ESP8266, 2021).

### 1b. Open projects: electrical design

| Project | Zone switch | Logic supply from 24VAC | Protection | Sensor inputs | Current sensing |
|---|---|---|---|---|---|
| OS 3.2 AC driver (from netlist of [acdr_3.2_updated.sch](https://github.com/OpenSprinkler/OpenSprinkler-Hardware/tree/master/OS/3.0-3.3/AC_driver)) | MAC97 triac ×8, gate from PCA9555 output through 390 Ω, 10 kΩ gate–MT1 | Half-wave (M7 diode) → 470 µF/50 V → XL2001 fixed-5 V buck (47 µH, SS36) → 220 µF/35 V | 3 A fuse in COM; SMBJ43CA across 24VAC input; SMBJ43CA per zone; 47 Ω + 10 nF RC snubber per zone; SMAJ5.6A on shunt node | 2 inputs: 4.7 kΩ series + ZMM5V1 zener | Total: 5 × 1 Ω/2512 in parallel (0.2 Ω) in the triac return, LM358 precision half-wave rectifier → RC → ADC |
| OS 3.4 AC ([PDF](https://github.com/OpenSprinkler/OpenSprinkler-Hardware/blob/master/OS/3.4/AC/OS34AC_Schematic.pdf)) | MAC97 ×8 from XL9555 expander (330 Ω gate, 10 kΩ) | M7 half-wave → 470 µF/50 V → XL2001 → 5 V; BL1117-33CX LDO; SY6280AAC load switch | Two 5×20 fuses; 47 Ω + 10 nF RC snubber per zone; 2.2 Ω series resistor per zone (purpose inferred); SMAJ6.0A. No SMBJ43CA string in the PDF text, so per-zone TVS on 3.4 is **unverified** | 4 inputs: 1 kΩ + ZMM5V6 | Total: "Low-Side AC Current Sensor", 1 Ω shunt + LM358D |
| OSPi 2.0 ([PDF](https://github.com/OpenSprinkler/OpenSprinkler-Hardware/blob/master/OSPi/2.0/ospi20_schematic.pdf)) | MAC97 ×8 via 74HC595D | M7 half-wave → 470 µF/50 V → buck labelled "XL2011" in the PDF text (part unverified) → 5 V for the Pi | 2 × 5×20 fuses; SMBJ43CA; 47 Ω + 10 nF per zone | ZMM5V6 clamps; 2 × ADS1115 (8 analog inputs) | none found (unverified) |
| OSBee 3.0 ([BOM](https://github.com/OpenSprinkler/OSBeeWiFi-Hardware/tree/master/v3.0)) | AO3400/AO3407 H-bridges (latching DC) | 5 V USB → MC34063 boost → 2200 µF reservoir (pulse discharge) | — | — | — |
| hwstar 9-valve ([README](https://github.com/hwstar/9-Valve-Sprinkler-Controller)) | Triac per zone, rated 0.4 A (part not published) | Requires a 40 VA transformer with internal fuse; on-board regulator unverified | **Watchdog relay** cuts all valves unless GPIO16 toggles (charge pump + MOSFET) | 3 buttons; inputs unverified | Total, 2.02 V/A into GPIO36 (ADC) |
| irriBRANT v1.2 ([schematic](https://github.com/Renbrant/ESP32-C6-Irrigation-Controller/tree/main/hardware), [changelog](https://github.com/Renbrant/ESP32-C6-Irrigation-Controller/blob/main/CHANGELOG.md)) | MOC3041 zero-cross opto-triac → BT136S triac; MCP23017 expander | AC → MOV → NTC (10D-11) → fuse → MB10S bridge → 470 µF → XL1509-5.0 (100 µH, SS36) | MOV (part unverified), NTC, fuse; 47 Ω + 10 nF snubber per zone | via ESPHome/HA | none |
| SprinklerBoy ([schematic PDF](https://github.com/carletz/Sprinkler-Boy/tree/main/Hardware)) | PC817AS opto → MAC97A8; PCF8574 expander | XL1509-5 buck + BL1117-33CX; Hackster says 5 V DC input, so the 24VAC path is **unverified** | SMBJ43CA ×9; **PCB spark gap per valve**; fuses; SS34 | rain + dry-contact; 8 opto inputs on a separate board | none |
| 30hours ([hardware README](https://github.com/30hours/irrigation/blob/main/hardware/README.md)) | Omron G3MB-202P SSR (opto-triac SSR) | **Mains** HLK-PM03 to 3.3 V, plus a separate 240→24 VAC transformer | 0.63 A transformer fuse | none | none |
| zioCristia ([sch](https://github.com/zioCristia/smart-irrigation-control-unit)) | TIC206 triac via 2N2907 PNP; G2RE pump relay | from host board | no TVS or snubber in parts list | — | via host board (energy monitor) |
| Relay boards (KC868, ES32A08, BeaverDam, Smart Garden, LazyGardener) | Electromechanical relays | 12/24 VDC or mains input; usually **not** from 24VAC ([HA warning](https://community.home-assistant.io/t/diy-8-station-irrigation-based-on-esphome-on-kc868-a8-relay-board/459599)) | typically none | few | none |

### 1c. Commercial feature baseline

| Product | Zones | Sensing and diagnostics | Connectivity and setup | Source |
|---|---|---|---|---|
| Rachio 3 | 4/6/8/12/16 (models differ by page) | 2 sensor inputs (S1/S2) plus ACC+/ACC− sensor power; master valve terminal; wired or wireless flow meter | dual-band WiFi | [Rachio specs](https://support.rachio.com/rachio-3-compatibility-and-tech-specs-H1q8P8JtP) |
| Hunter Hydrawise HPC/Pro-HC | 4–16+ | **Continuous solenoid current monitoring with high/low alerts and auto-off**; typical solenoid 200–400 mA, 300–500 mA with master valve; flow meter with high, low and unscheduled-flow alerts | WiFi | [Hydrawise current page](https://www.hunterirrigation.com/en-metric/support/hydrawise-app-highlow-current) *(search summary, page 403)*; [flow test](https://www.hunterirrigation.com/support/hydrawise-testing-flow-meter) |
| Rain Bird ESP-TM2 + LNK2 | 4–12 | rain sensor; relies on the controller's surge protection | 2.4 GHz WiFi, **BLE used during setup** | [LNK2 tech spec](https://www.rainbird.com/sites/default/files/media/documents/2022-06/d42005_24jn21_lnk2-wifi-tech-spec_en.pdf) |
| Orbit B-hyve XR | 8/16 | rain/freeze sensor; master valve/pump relay; flow only through third-party Flume | dual-band WiFi + Bluetooth 5.0 | [Sprinkler Warehouse listing](https://www.sprinklerwarehouse.com/orbit-b-hyve-xr-8-zone-smart-indoor-outdoor-sprinkler-controller-57985) (retailer, unverified) |

Baseline for ZoneFlow: per-zone fault alert with auto-off, flow meter with leak and high/low alerts, rain input, master valve, local fallback schedule, and BLE-assisted setup.

---

## 2. Circuit details worth copying or avoiding

### Output stage
- **Copy, from OpenSprinkler:** one 24VAC leg is circuit ground and the other is COM. Each valve sits between COM and its triac's MT2, and MT1 goes to ground through the current shunt. This lets the triacs be gate-driven directly from logic, without an opto ([search summary of Ray's forum explanation](https://opensprinkler.com/?p=72067), page 403; confirmed in the 3.2 netlist above). Gate drive is about 330–390 Ω from a 5 V/3.3 V expander pin, with a 10 kΩ gate–MT1 pull-down. The design note says each triac needs only 5–7 mA holding current ([OpenSprinkler circuit notes, 2012](https://openthings.io/?p=2648)).
- **Alternative, from irriBRANT and SprinklerBoy:** a full-wave bridge or isolated logic requires opto drive (MOC3041 zero-cross, or PC817). That costs more parts and gives up the common-return shunt. The benefit is a logic ground that is not tied to an AC leg.
- **Per-zone snubber:** every triac design that publishes values uses 47 Ω + 10 nF (OS 3.2, OS 3.4, OSPi 2.0, irriBRANT). This handles dv/dt and inductive turn-off.
- **Per-zone TVS:** a bidirectional SMBJ43CA from each zone to return (OS 3.2, OSPi 2.0, SprinklerBoy). The 43 V standoff stays above the peak of 30 VAC (42.4 V). SprinklerBoy also adds PCB spark gaps.
- **Avoid MAC97A8 as a new design-in.** Distributors list it at 0.6 A RMS and 8 A surge, other datasheets at 0.8 A, and the onsemi version is listed obsolete ([DigiKey](https://www.digikey.com/en/products/detail/on-semiconductor/MAC97A8/583818), [element14](https://tw.element14.com/en-TW/onsemi/mac97a8g/triac-0-6a-600v-to-92/dp/1459057)). Choose a sensitive-gate SMD triac with at least 1 A RMS and high surge rating, stocked at JLC. Part to be selected.
- **Avoid relays and SSR modules for the product.** Relays click and wear out, and the typical boards have no protection. The G3MB-202P is a through-hole module.

### AC-to-DC stage
- OpenSprinkler and OSPi use a half-wave rectifier with an M7 diode, 470 µF/50 V bulk, and a fixed 5 V XL2001 buck. The XL2001 is rated for **8–45 V input** ([LCSC](https://www.lcsc.com/product-detail/C73336.html)). Unloaded transformers reach 28 VAC, and cheap ones exceed 30 VAC ([search summary, OS forum](https://opensprinkler.com/forums/topic/v3-4-max-power-supply-voltage/), page 403). 30 VAC gives a 42.4 V peak, and 30 VAC plus 10 % mains high gives about 46.7 V. **The 45 V margin is thin.** ZoneFlow should use a buck rated for at least 60 V and put a TVS ahead of it. This is my analysis, not a reported failure.
- irriBRANT adds an input MOV, an NTC inrush limiter and a fuse ahead of the bridge (changelog v1.2). This is worth copying: fuse plus MOV or TVS at the 24VAC input.
- hwstar requires a transformer with an internal fuse. Note that Ray says a fuse gives no lightning protection (see §3).

### Sensor inputs
- OpenSprinkler uses a series resistor (4.7 kΩ on 3.2, 1 kΩ on 3.4) and a 5.1–5.6 V zener to ground. That is cheap and adequate for a dry-contact rain sensor. For the pulse flow meter, add an RC filter and a pull-up. Rachio provides switched sensor power (ACC+/ACC−); copy that so powered flow meters work.

### Current sensing, inrush and turn-off
- **OS method:** one low-side shunt in the shared triac return (0.2 Ω on 3.2, 1 Ω on 3.4), then an LM358 precision rectifier and RC filter into the ADC. It measures **total** current. Firmware ([`defines.h`](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/blob/master/defines.h), [`main.cpp`](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/blob/master/main.cpp)) uses a default overcurrent limit of 1200 mA, adds `OVERCURRENT_INRUSH_EXTRA` of 600 mA, and samples 10 × 5 ms (about 50 ms) right after a zone turns on.
- Ray: a shorted valve draws more than 1000 mA against about 220 mA hold, a gap of at least 5×, so a single threshold works ([issue #382](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/382)). The OS4 shop page now also lists **undercurrent** (open-valve) detection ([shop](https://opensprinklershop.de/en/product/opensprinkler-4/)).
- hwstar senses total current at 2.02 V/A into an ESP32 ADC, and the README warns that the ESP32 ADC is nonlinear near 0. It clips below 0.08 V.
- Inductive turn-off: a triac turns off at current zero, so the solenoid flyback is limited. The RC snubber and TVS handle the residual dv/dt and surges. No project uses flyback diodes on AC outputs. SprinklerBoy's SS34 is on its DC side (unverified).

---

## 3. Known field failures and complaints

| Failure | What happened | Source |
|---|---|---|
| Blown triac, zone dead or always on | Shorted solenoid or wiring destroys the zone triac. Ray: replace with hot air; test solenoid resistance before connecting; "add a per-zone fast-acting fuse, 1A is sufficient" | [OS forum reply](https://opensprinkler.com/forums/reply/50102), [OS forum "Let the smoke out"](https://opensprinkler.com/?p=49947) *(search summary, page 403)* |
| Lightning and surge through long valve wires | Ray: "the fuse won't do any protection under lightning power surge"; the board relies on TVS per zone and on the 24VAC line, and a strike can destroy the TVS | same as above; [OSPi "Surge resulting in controller not working"](https://opensprinkler.com/forums/topic/surge-resulting-in-controller-not-working/) (title only, page 403) |
| Regulator damage from transformer overvoltage | Listed maximum 28 VAC, "can withstand 30VAC at least"; low-quality transformers exceed 30 VAC unloaded; an older OS kit had suspected damaged MC34063/MCP1700-33 regulators | [OS forum max voltage](https://opensprinkler.com/forums/topic/v3-4-max-power-supply-voltage/), [OS forum "Voltage high on VIN"](https://opensprinkler.com/forums/topic/voltage-high-on-vin-vcc/) *(search summary, page 403)*; [Rachio user reading 28–30 VAC](https://community.rachio.com/t/rachio-3-voltage-too-high/40671) |
| ESP32 crash mid-cycle leaves master valve on | KC868-E16S running ESPHome panicked and rebooted every time it reached station 3; the pump relay dropped but the master valve stayed on; the author moved to OpenSprinkler | [pdx.su blog, 2025-03-28](https://pdx.su/blog/2025-03-28-trying-and-failing-to-build-an-esphome-powered-irrigation-system) |
| Commercial cloud controller stops watering | RainMachine crashed after a failed weather update and did not water for about a week | same blog |
| Outputs pulse at boot | ESP32 GPIO32/33/13 go high then low at reset and energize relays | [HA thread 396991](https://community.home-assistant.io/t/help-gpio-outputs-being-toggled-at-boot-on-esp32/396991) |
| | LilyGo T-Relay S3 (74HC595): "All the relays trigger for about 100ms on boot"; no confirmed fix | [HA thread 744979](https://community.home-assistant.io/t/lilygo-t-relay-s3-relays-triggering-on-boot/744979) |
| Strapping pin | 30hours v1: GPIO9 on ESP32-C3 pulled low, so the board booted into the serial bootloader; v2 removed all strapping pins | [30hours hardware README](https://github.com/30hours/irrigation/blob/main/hardware/README.md) |
| Valve state lost on power loss | A latching valve can stay open after reboot because ESPHome assumes it is off | [HA thread 713976](https://community.home-assistant.io/t/esphome-sprinkler-run-off-signal-on-startup/713976) |
| Brownout or reboot when a valve switches | Reports of ESP32 resets on relay or valve switching, often without a brownout log | [HA "ESP32 reboots when turning off relay"](https://community.home-assistant.io/t/esp32-reboots-when-turning-off-relay-why/478322), [HA "esp32 shutting down when triggering relay"](https://community.home-assistant.io/t/issue-esp32-shuting-down-when-triggering-relay/413748) (from search summaries; threads not read in full) |
| WiFi | OpenSprinkler locks to a BSSID and channel, which breaks on mesh networks or channel changes (workaround: reboot) | [OS FW issue #413](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/413) |
| Security | OS 2.2.1(6) on ESP8266: plain WebSocket on port 80 to the OpenThings cloud, plaintext credentials; Ray cites the ESP8266 RAM limit | [OS FW issue #448](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/448) |
| Flash storage | LittleFS filled with about 255 tiny daily log files; program writes failed silently but the API reported success (fixed in 2.2.1(6)) | [OS FW issue #436](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/436) |
| Wrong supply type | 24 VDC on 24VAC solenoids "does cook the solenoid if left to run 20 minutes"; a user back-fed 24VAC into the board | [HA thread 628838](https://community.home-assistant.io/t/simple-reliable-sprinkler-controller-with-esphome/628838) |
| Water hammer | Tindie reviewer reports split pipes; valve overlap or delay features mitigate this | [hwstar Tindie page](https://tindie.com/products/home-control-labs/9-valve-sprinkler-controller-works-with-esphome/) |

---

## 4. Firmware and ecosystem

### ESPHome `sprinkler` component ([docs](https://esphome.io/components/sprinkler.html))
- **Provides:** any number of valves (GPIO or expanders), latching valves via H-bridge, pump/master valve with start and stop delays, `valve_open_delay` or `valve_overlap`, multiplier, repeat, divider, queue (up to 100 entries), pause/resume, reverse, standby, per-zone enable, and run durations restored across reboot.
- **Lacks:** a time-of-day scheduler (the docs expect Home Assistant or the device to call `start_full_cycle`), flow sensing, current/fault detection, and a rain-sensor input. Valves are **switches**, not HA `valve` entities ([HA thread](https://community.home-assistant.io/t/esphome-sprinkler-component-does-not-work-with-the-valve-component/1020577)). ESPHome has a separate `valve` component ([docs](https://esphome.io/components/valve)). Projects fill these gaps in YAML (irriBRANT, BeaverDam) or replace the component ([esp32-sprinkler-queue](https://github.com/cristoforocervino/esp32-sprinkler-queue)).
- **Implication for ZoneFlow's ESPHome config:** ship a package that adds an on-device scheduler (datetime entities), `pulse_meter` flow with leak alarms, current-fault logic with inrush blanking, rain lockout, and a max-runtime guard.

### OpenSprinkler firmware and API
- API: HTTP GET with JSON responses. Every call needs `pw` = lowercase MD5 hash. Commands include `/jc /jo /co /cv /jn /cs /je /js /cm /mp /cr /pq /jp /cp /dp /up /jl /dl /sp /cu /ja /db`. MQTT is configurable. Identity fields: `fwv`/`fwm` (firmware), `hwv`, and `hwt` (0xAC, 0xDC or 0x1A) ([2.2.1(4) API reference](https://opensprinklershop.github.io/2.2.1/221_4_api/)).
- The firmware (GPL-3.0) has a **board profile layer** (`PROFILE_OS_30` … `PROFILE_OS_40`, `PROFILE_OSPI_BOARD`) with pin maps, I²C addresses (PCA9555 at 0x21 for the AC driver), and an OS4 current-sense pin ([board_profile.h](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/blob/master/boards/board_profile.h)).
- HA integration: [vinteo/hass-opensprinkler](https://github.com/vinteo/hass-opensprinkler) (MIT, HACS default, tested on API 2.2.1(4) and HA 2026.7.4) uses this HTTP API.
- **Compatibility options:**
  - (a) **Port the OS firmware** with a ZoneFlow board profile. This gets the app, HA integration, cloud and weather features at once. ZoneFlow's custom firmware must then be GPL-3.0, and the IO-expander and shunt design should match OS (PCA9555-class expander, low-side shunt) to minimise changes. This is realistic.
  - (b) **Emulate the API** in independent firmware. This is possible, but the surface is large (program encoding, logs, `/ja`), it must track OS releases, and app behaviour keyed on `hwv`/`fwv` is undocumented (unverified). Do this only for a subset (`/jc /js /cm /cv /jn`) if at all.

### WiFi provisioning over BLE
- **Improv:** an open standard over BLE and over serial. ESPHome component `improv_ble`, renamed from `esp32_improv` in ESPHome 2026.10.0; the BLE stack is RAM-heavy ([docs](https://esphome.io/components/improv_ble/)). hwstar ships Improv-serial via ESP Web Tools.
- **ESP-IDF unified provisioning:** BLE or SoftAP transport. Security 2 uses SRP6a with AES-256-GCM, and the password goes on a QR sticker. BLE uses about 110 KB RAM, which can be freed after provisioning. Espressif provides Android/iOS apps and the `esp_prov` CLI ([docs](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/provisioning/provisioning.html)).
- Commercial precedent: Rain Bird LNK2 uses BLE for setup ([tech spec](https://www.rainbird.com/sites/default/files/media/documents/2022-06/d42005_24jn21_lnk2-wifi-tech-spec_en.pdf)).

### Matter
- **Water Valve** device type 0x0042 (Matter 1.3) with the Valve Configuration and Control cluster ([matter-survey](https://matter-survey.org/device-types/66)). The **Irrigation System** type 0x0040 is in Matter 1.5 and needs at least one Water Valve endpoint ([HA discussion #3084](https://github.com/orgs/home-assistant/discussions/3084), Mar 2026). Rachio is implementing it; HA support is not native yet.
- SDK gaps: esp_matter adds the Water Valve wrapper only from 1.6.0 ([arduino-esp32 #12855](https://github.com/espressif/arduino-esp32/issues/12855)). The connectedhomeip WaterValve example hardcodes its config ([#74787](https://github.com/project-chip/connectedhomeip/issues/74787)).
- OpenSprinkler 4 has an 802.15.4 radio, but Matter, Thread and Zigbee are "announced", not shipped ([issue #399](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/399), [shop](https://opensprinklershop.de/en/product/opensprinkler-4/)).
- ESPHome Matter support: not researched (unverified).

---

## 5. Recommendations for ZoneFlow

### Design choices the evidence supports

1. **Use the OpenSprinkler output topology.** Half-wave rectification with ground on one 24VAC leg, triacs gate-driven directly from an I²C expander (PCA9555-class), 10 kΩ gate pull-downs so outputs are off at power-on reset, and 47 Ω + 10 nF RC snubber plus SMBJ43CA-class bidirectional TVS per zone and on the input. *Source: OS 3.2/3.4 and OSPi 2.0 schematics.* It is proven and uses few parts, and it keeps the shared-return shunt possible.
2. **Use a buck regulator rated for at least 60 V input,** not a 40–45 V part, with a TVS and fuse ahead of it and bulk capacitance sized for half-wave ripple at peak WiFi load. *Source: XL2001 8–45 V rating vs. unloaded transformers above 30 VAC (§2, §3).*
3. **Add per-zone surge and short tolerance.** Use a per-zone series resistor or PTC/fuse (OS 3.4 has 2.2 Ω per zone; Ray recommends a 1 A fast fuse per zone), PCB spark gaps on terminals (SprinklerBoy), and a MOV or TVS plus fuse at the input (irriBRANT v1.2). Use a triac rated at least 1 A RMS with high I²t instead of MAC97A8.
4. **Measure total current with one low-side shunt and a precision rectifier (or an RMS-capable ADC path),** and get per-zone diagnosis by checking current at each zone turn-on. Use inrush blanking of about 50 ms plus a margin, per-zone learned "expected current" (requested in [OS #382](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/382)), overcurrent auto-off and undercurrent (open-valve) alerts. *Source: OS firmware, Hydrawise behaviour.* Avoid ESP32 ADC readings near 0 V, or use an external ADC (hwstar note).
5. **Make the outputs fail safe in hardware.** Gate the triac drive (or the expander supply) through a charge-pump watchdog that the firmware must keep toggling (hwstar `valve_dog`). Keep all output-control nets off strapping pins (30hours GPIO9). Drive valves only through an expander whose power-on default is off. Enforce a firmware max-runtime per zone and for the master valve. *Sources: §3 boot-glitch and crash rows.*
6. **Protect sensor inputs** with a series resistor and a zener or TVS clamp (OS). Give the flow input an RC filter and pull-up. Provide switched sensor power terminals (Rachio ACC+/ACC−).
7. **Use the ESP32-C6** (WiFi 6, BLE 5, 802.15.4 for future Thread/Matter), which OS4 also chose, or the ESP32-S3 if RAM for BLE plus TLS is tight. Add USB-C for flashing and recovery (OS4) and Improv-serial.
8. **Custom firmware:** use TLS everywhere and no plaintext credentials ([#448](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/448)); never lock to a BSSID ([#413](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/413)); keep logs bounded and check write results ([#436](https://github.com/OpenSprinkler/OpenSprinkler-Firmware/issues/436)); run schedules on-device so watering works without HA or the cloud (RainMachine failure).
9. **BLE provisioning:** use Improv BLE for the ESPHome build and ESP-IDF unified provisioning with Security 2 (QR code) for the custom build. Free the BLE RAM after setup.
10. **Decide early on OpenSprinkler compatibility.** If wanted, the cheapest path is a GPL board profile in the OS firmware, and the hardware should mirror OS conventions (expander, shunt, sensor pins).

### Gaps ZoneFlow can fill (differentiators)

- **Open ESP32 hardware with a published schematic.** The OS4 schematic is not in the public repo yet (unverified beyond the repo tree); hwstar is retired; most ESPHome boards use relays without protection.
- **Per-zone open/short diagnosis on a hobby board.** Only OS (total current) and hwstar (total current) sense current at all, and none of the ESPHome boards do.
- **Flow meter integration with leak and unscheduled-flow alarms** in ESPHome. The `sprinkler` component has none; commercial units (Hydrawise, Rachio) do.
- **Hardware fail-safe watchdog** as a standard feature. Only hwstar has one.
- **One board, two firmwares:** ESPHome (HA-native `valve` entities, on-device scheduler) and OpenSprinkler-compatible or custom firmware.
- **BLE provisioning out of the box.** OS, hwstar and 30hours use serial, Web Tools or a SoftAP.
- **Matter Water Valve / Irrigation System endpoint.** No open project ships one yet, and HA support is pending.
- **JLC-assemblable, single 24VAC input, about one third of OS4's €239 price.** The BOM cost is unverified.

---

## 6. Could not verify

- OpenSprinkler 4 AC schematic, triac part and current-sense circuit (not in the public repo as of 2026-11-10).
- OS 3.4 per-zone TVS (no SMBJ43CA string in the PDF text) and the function of the 2.2 Ω per-zone resistors.
- OSPi 2.0 regulator ("XL2011" in the PDF text; may be a typo or a different part).
- hwstar 9-valve triac part, regulator and sensor inputs (schematic not in repo; wiki not checked); kit price.
- SprinklerBoy power input path (5 V DC per Hackster vs. buck in schematic) and opto-to-triac topology.
- irriBRANT MOV part number.
- MAC97A8 RMS rating (0.6 A vs. 0.8 A differs by source) and exact IGT.
- Prices for OpenSprinkler 3.4, OSPi and BeaverDam.
- Content of opensprinkler.com and hunterirrigation.com pages (HTTP 403); claims from them come from search summaries.
- Brownout reports: taken from search summaries; threads not read in full.
- ESPHome Matter support; whether the OpenSprinkler app behaves differently by `hwv`.
