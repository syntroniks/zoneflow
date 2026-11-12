# Repository audit, 2026-11-12

!!! warning "A reading of files"
    No board has been built, no part has been printed and no firmware has run
    on hardware. This audit compared files with files and ran the checks the
    repository already has. Section 2 names every command. Where this page
    says something "agrees" or "passes", a program or a reading compared two
    files. Nothing here is a bench result.

About 30 pull requests were merged in two days by separate authors, each
working on one part. This is a reading of the whole repository by someone who
wrote none of it, at commit `e9b0b12` of `main`. It looks for places where the
repository disagrees with itself or with what is true today, corrects what is
a matter of text, and lists the rest.

## 1. Result in short

| Severity | Findings | Corrected in the audit's pull request | Left open |
|---|---|---|---|
| Blocking | 0 | | |
| Major | 10 | 7 | 3 |
| Minor | 19 | 14 | 5 |
| Note | 10 | | |

No pin, net, address or dimension was found to differ between hardware,
firmware and enclosure. Every cross-subsystem contract that a program can
check was checked again and agrees (section 6). The defects are in what the
repository says about itself.

The five that matter most:

1. **The README said the PCB layout was not finished.** It is finished,
   routed, clean in DRC and independently reviewed. The warning not to order
   stays, with its real reasons: the review's checklist for a person is open,
   no sample display panel has confirmed the flat cable, and the owner has
   not decided (A1, A2).
2. **The status page counted 27 bench risks. The register has 34.** Risks 28
   to 34 came with review A2 and the hardware answers and were in no table of
   the status page. Issue #33 still says 27 (A3).
3. **The instructions for agents named the wrong source file.** `CLAUDE.md`
   said to edit `design/board.toml`. On the product board that file is
   written by `design/layout.py`, and an edit to it is lost at the next
   `make` (A4).
4. **An installer with a board could not install firmware.** No release
   exists, the installer page said so and stopped there, and the ESPHome
   guide named a tag, `v0.2.0`, that does not exist (A5, A6).
5. **Three things need the owner, not an editor.** CI does not compare the
   board file and the fabrication files with what the sources generate (A7).
   The ESPHome firmware image contains ESPHome's GPLv3 runtime, and nothing
   says under which terms the project would publish it (A8). The
   specification allows through-hole parts only where no surface-mount part
   exists, and C1 is through-hole by a mechanical decision (A9).

## 2. What was read and what was run

Read in full: `CLAUDE.md`, the system specification, the README, the home
page, the status page, safety, contributing, FAQ, every page of Getting
started, the installer and release pages, the hardware reference pages
except the body of the cost page, the enclosure assembly page, the seven
workflows, `hardware/pinmap.yaml`, the Makefiles of `hardware/` and
`enclosure/`, and every README. Read in part, by search for numbers, dates,
status words and issue numbers: the design documents (14,000 lines), the
troubleshooting, display, web interface, MQTT, ESPHome and API pages, and
the firmware sources where a page quotes a default.

Run, all on the build machine on 2026-11-12, before any change:

| Command | Result |
|---|---|
| `uv run mkdocs build --strict` | Passes |
| `tools/docs/check_links.py site` | 8000 internal links in 65 pages, none broken |
| `make -C hardware design-check` | 353 components, 351 fitted, 99 BOM lines, no problem. The committed netlist has the 353 components and 200 nets of the design netlist, pin for pin; both JLCPCB BOMs list the fitted parts |
| `make -C hardware/zoneflow-a board-check ROUTE_FLAGS=--stored-only` | DRC with schematic parity, the mechanical contract and the layout rules pass. `git status` afterwards showed no change under `hardware/`: the board file and the reports are what the sources generate, on this machine with KiCad 10.0.8 |
| `make -C hardware/zoneflow-a review-check` | 63 pictures of the review walkthrough drawn again from the board file, the same as the committed ones |
| `firmware/zoneflow-fw/test/host/run.sh` | 859 of 859 host tests pass, with ASan and UBSan |
| `firmware/esphome/test/host/run.sh` | 45 of 45 pass |
| `firmware/esphome/tools/gen_board_yaml.py --check` | The ESPHome pin file is what the pin map gives |
| `firmware/zoneflow-fw/tools/gen_board_header.py` | Writes the board header from the pin map with no error |
| `zfcost render --check` and `pytest` in `tools/cost` | The cost page is current with its data; 19 tests pass |
| `enclosure/tools/gen_params.py`, `board_layout.py`, `gen_testfit.py` into scratch files | `board_params.scad`, `board_layout.json` (356 footprints), `testfit_params.scad` and `stand-ins.md` come out the same as the committed ones |
| `node --test tools/ewt/installer.test.mjs`, `tools/ewt/vendor.py check` | 5 tests pass; 27 vendored files match their pinned hashes |
| A script over every `/api/v1/...` route named in the documentation | Each is an operation of `docs/firmware/openapi.json` (54 operations) |
| `git grep` and the GitHub API for the owner's private address and for AI attribution | None in the tree, in the 31 commits of `main`, in 33 pull request descriptions or in the issue and review comments |

Run again after the changes: the documentation build, the link check,
`design-check`, `board-check`, the cost check and the installer page test.
All pass. The pull request states the same list.

**Not checked.** The full `make` of the board (Gerbers, placement files,
STEP, renders) and of the enclosure (OpenSCAD) was not run, so their outputs
were not regenerated here. The ESP-IDF build, the ESPHome compile, the web
interface's tests, the soak simulation and the browser demo were not run
locally; CI runs them. The display pictures and the web interface
screenshots were not drawn again. The simulations in `hardware/sim/` were
not run again. The body of the cost page and the long design documents were
not read line by line. No statement in a design document was checked against
a data sheet: this audit looked for disagreement inside the repository, not
for errors in the engineering.

## 3. Findings

"Fixed" means changed in the pull request that adds this page.

### Major

| No. | Where | Finding | Evidence | What was done |
|---|---|---|---|---|
| A1 | README | The status box said "the PCB layout is not finished" and the table said "In progress" with a link to issue #5, which was closed on 2026-11-11. The table had no line for the layout review, the test-fit kit, the cost estimate, the review walkthrough or the browser demo, and said the circuit was "reviewed once" | `README.md` lines 9 to 30 at `e9b0b12` | Fixed. The warning not to order stays and names its three reasons |
| A2 | Home page | The table said the layout was "not reviewed". The warning box above it said it "has had an independent review". The circuit row named one review of three | `docs/index.md` line 52 at `e9b0b12` | Fixed |
| A3 | Status page | "A register of 27 risks": the register of hardware design section 20 has 34. Risks 28 to 34 were in no row of the page's table. The roadmap said of the prototypes "There is no issue for this yet"; issue #33 exists. Its steps were numbered 1, 2, 3, 4, 6. The circuit row named review A1 only | `docs/status.md` lines 27 to 60 at `e9b0b12`; `docs/design/hardware-design.md` section 20 | Fixed. The page now says the layout is finished and what stands before an order |
| A4 | `CLAUDE.md` | "Edit only ... `board.toml`". On `hardware/zoneflow-a/` `board.toml` is generated by `design/layout.py` with `patches.toml` and `patches-hand.toml` (hardware toolchain 13.1), so an agent who followed the instruction would lose the edit. The same file said "CI regenerates them and fails on a difference", which holds for the schematic half only (A7) | `CLAUDE.md` lines 45 to 58 at `e9b0b12`; `hardware/flow.mk`, target `layout` | Fixed in `CLAUDE.md`, in the toolchain document's table of sources and in its summary |
| A5 | Manual | Step 5 of Getting started is "Installing firmware". No release exists, so the installer page shows "No release yet" and `zfflash` has nothing to fetch. No page said how else to put firmware on a board | `docs/getting-started/index.md` line 16; `docs/firmware/web-installer.md`; `gh release list` is empty | Fixed: the installer page has a section on building both firmwares from the repository, with the commands of their READMEs, and step 5 points to it. Not tried on a board |
| A6 | ESPHome guide | The device file of the guide and `examples/minimal.yaml` take the package from tag `v0.2.0`. The repository has no tag. `esphome run` on that file cannot fetch the package | `docs/firmware/esphome.md` lines 65 and 70; `git tag` is empty | Fixed with a note in the guide and a comment in the example: until a release is tagged, write `main` |
| A7 | CI | The `hardware` workflow regenerates the schematic and fails on any difference. Its `board` job rebuilds the board and runs the checks but compares nothing. The board file, Gerbers, drill files, placement files, pin 1 table, STEP, renders, interactive BOM, board PDFs and the routing report can differ from what the sources give and no job says so | `.github/workflows/hardware.yml`, job `board` has no `git diff` step | Left open: adding the comparison is new CI work, and the toolchain document says three outputs differ from run to run. On the build machine `board-check` reproduced the committed board file today. Issue text in section 11 |
| A8 | Licences | The specification gives firmware the Apache-2.0 licence. The ESPHome firmware image that the release workflow builds is ESPHome's runtime linked with the project's components, and ESPHome publishes its C++ and runtime code under GPLv3. No page says under which terms that image is published. No release carries a notice of the third-party code inside it. No release has been made, so nothing has been published wrongly | `LICENSE` of `esphome/esphome`, read 2026-11-12; `.github/workflows/release.yml`, job `build-esphome` | Left open: the owner's decision. Recorded on the new [third-party material](../third-party.md) page. Issue text in section 11 |
| A9 | Specification against the design | Specification section 1, Manufacture: "Through-hole parts are limited to terminal blocks and parts with no SMT equivalent." K1 is through-hole and the design says why no surface-mount relay fits (section 7.2). C1 is through-hole although the first draft used a surface-mount part: the mechanical contract moved it to the bottom side, lying down, to avoid a second assembly side. The reason is written down, but the specification was not changed and section 2 of the hardware design does not list it as a departure | `docs/design/system-spec.md` section 1; `docs/design/hardware-design.md` section 4.5; `docs/design/mechanical.md` section 6 | Left open: a requirement is the owner's to change. Issue text in section 11 |
| A10 | Licences | Nothing recorded the licence of the KiCad library symbols, footprints and 3D models that are inside the committed board files, STEP files and renders, nor of the Interactive HTML BOM code inside the committed HTML file, nor of the tools the builds download | Search for "CC-BY-SA", "GPL" and "MIT" in the tree at `e9b0b12`: one comment in `hardware/flow.mk` | Fixed: [third-party material](../third-party.md), with how each licence was read. The README's sentence on licences now says which `LICENSE` file covers what |

### Minor

| No. | Where | Finding | What was done |
|---|---|---|---|
| B1 | Component counts | Hardware design section 16.4 said "346 components"; the toolchain document's status line stopped at 348; a comment in `design/layout.py`, copied into `board.toml`, said "351 parts on one side". The netlist has 353 components and the board 356 footprints, 354 of them on the top side | Fixed. `board.toml` was written again by `make`; nothing else in the board directory changed |
| B2 | Test and picture counts | The ESPHome page said 41 host tests; 45 run. The display document said 73 scenes; the simulator draws 75 and 75 pictures are committed | Fixed |
| B3 | Design documents index | Firmware security and the review records are in the site navigation and were missing from the list on the index page | Fixed |
| B4 | Contributing, `CLAUDE.md` | Both said generated outputs go in `hardware/output/`, a directory that does not exist: each board has its own `output/`. Contributing said "Do not commit build output" and did not name the generated files that are committed on purpose | Fixed |
| B5 | Firmware documents | Firmware architecture, status: "The web UI itself is later work"; it was merged in pull request #25. The firmware README called the status "phase 3 (MQTT)" and "every pin number provisional", and said in one place that the first build downloads LVGL and in another that it downloads two other components | Fixed |
| B6 | Specifications page | The service header's pads were named TXD0, RXD0 and IO0. The netlist and the release page name them U0TXD, U0RXD and BOOT | Fixed |
| B7 | Wiring diagram | Its footnote said "Silkscreen labels are not final until the PCB layout exists" | Fixed in `docs/assets/diagrams/wiring.svg` |
| B8 | Mechanical design | Section 11 said "The BOM still lists the Lelon part first". The BOM carries the Rubycon part since the hardware design took it over | A sentence added that says so. The original remark is kept |
| B9 | README | "See the `LICENSE` file in each directory": `firmware/` and `tools/` have none. The root file covers them | Fixed in the wording |
| B10 | Home page | Specification 4.3: the site says the demo is a simulation "wherever it offers the demo". The button at the top of the home page had no such words beside it | Fixed: one line under the button |
| B11 | CI | `firmware`, `esphome` and `web-ui` cancelled a running job on every new push, on `main` too. A commit on `main` could be left with no result: the `firmware` run of `d028325` was cancelled this way | Fixed: they cancel only on pull requests, as the other four workflows do |
| B12 | Hygiene | `firmware/esphome/.gitkeep` and `tools/.gitkeep`, in directories that have long had files | Removed |
| B13 | Board Makefile | "fails if ... or (once the board is laid out) DRC fail" | Fixed |
| B14 | Schematic page | "The results are from the build of 2026-11-10": 2026-11-10 is the title-block date that the build writes into every file, and the 353 components date from 2026-11-11 | Reworded |
| B15 | Specification against the BOM | Specification 3.1: "every BOM line carries an LCSC part number". One of the 99 lines of `hardware/bom-preliminary.csv` has none: the CR2032 cell, which JLCPCB does not stock as a part to fit | Left open with A9 |
| B16 | Issue #33 | Its text says the bench list has 27 items and that it is "Blocked on layout (#5)" | Left: issues are not edited by the audit. Text in section 10 |
| B17 | CI | Checks that exist and that no workflow runs are listed in section 8 | Left open, listed |
| B18 | Enclosure | Specification 5 asks for "STEP if the tool allows". There is no STEP of the enclosure and no page said why | One line on the enclosure page. OpenSCAD 2021.01 refuses the format; the newer build the enclosure is made with was not tried |
| B19 | Signatures | GitHub shows the first commit of `main`, `a4ed44d`, as unverified: it is signed with a key (`D36B565C5B85FD62`) that the GitHub account does not hold. The other 30 are squash merges signed by GitHub. Commits pushed to branches with that key show the same | Left: only the owner can add the public key to the account |

### Notes

| No. | Note |
|---|---|
| C1 | The brief for this audit expected issue #5 to be open and about 45 pull requests. Issue #5 was closed on 2026-11-11 with a comment that names the layout review. `main` has 31 commits from 33 pull requests; issues and pull requests share one series of numbers, which reaches 49 |
| C2 | Old numbers in dated records are kept on purpose and were left: GPIO38 as the keep-alive pin in review A1, "348 components and 199 nets" in review A2, "59 of 59 pairs" in mechanical design 12.1, "65 scenes" in a memory measurement of the firmware architecture, "18 tests" of the cost tool in the hardware answers. Each stands in a text that says when it was true |
| C3 | The cost page was expected to predate the BOM consolidation. It does not: `zfcost render --check` passes, the page counts 35 extended part types and explains the 40 before. The copies of its figures on the 4-zone page and in the FAQ agree with it |
| C4 | Tracked files are 102 MB. The largest are the board STEP (7.9 MB), the view-only board model (5.7 MB) and the board file (4.8 MB). The STEP is written differently by every build (toolchain 13.8), so each board change adds its size to the history |
| C5 | Seven pictures in `docs/design/hardware-toolchain/` (2.2 MB) are byte-for-byte copies of files in `hardware/toolchain-test/output/`. The site could publish them as it publishes the product board's outputs. `docs/assets/enclosure/` also copies `enclosure/output/`, by design and written by `make` |
| C6 | `output/reports/routing.md` says 104 nets are routed by hand and 97 have autorouted or patched copper, 201 together. The netlist has 200 nets. The cause was not looked for |
| C7 | `output/reports/placement-check.md` is committed and is written only by `make cpl-check`, which needs the network and is not part of `make` or CI. It agrees with the placement files today (312 placements) |
| C8 | The workflows pin different major versions of the same actions: `actions/checkout` v4 and v7, `actions/upload-artifact` v4 and v7 |
| C9 | Four commit subjects on `main` are longer than 72 characters once GitHub adds " (#NN)" to a squash merge. A pull request title of 66 characters or fewer avoids that |
| C10 | The hand-drawn block diagram does not show the 3.3 V over-voltage detector U13 or the supervisor. The fail-safe page describes U13 in words |

## 4. Numbers that appear in more than one place

| Number | Source of truth | What guards the copies | Copies found, and their state before the audit |
|---|---|---|---|
| 348 components, 346 fitted, 198 nets | `hardware/netlist.yaml` | `make -C hardware design-check` writes and checks the tables of the hardware design. It does not read prose | Schematic page, layout document, layout review, toolchain 12 and the review walkthrough: right. Hardware design 16.4 and toolchain status: stale (B1) |
| 351 footprints, 349 on top | The board file; `output/reports/build-summary.json` | Nothing checks prose | Layout document: right. Comment in `layout.py`: stale (B1) |
| 99 BOM lines; 95 and 94 lines of the JLCPCB BOMs; 307 and 262 placements | `hardware/bom-preliminary.csv`; the generated CSV files | `design-check` for the first; `zfcost` reads the placements | BOM page, cost page, layout review: right |
| 114 x 110 mm, four layers, 1.6 mm | `hardware/mechanical.yaml` | `hwflow mech` in every board build | Eight pages: right |
| 8 zones and a master; 44 parts per four channels; 46 parts not fitted on the 4-zone build | The netlist's `variant` field | `design-check`, build parity | Right everywhere. "45 parts" on the cost and 4-zone pages counts placements, which leave out the header J3 |
| 36 bench risks, 18 bring-up steps | Hardware design sections 20 and 21 | Nothing | Status page and issue #33 said 27 (A3, B16) |
| 47 review steps, 7 checklist items, 30 layout findings | `docs/hardware/review/data/`, layout review | The walkthrough's build counts its own steps | Ordering page, layout page: right |
| Landed cost: 121.22, 89.67, 70.57, 53.71 USD (8-zone, 5 to 100 units) | `tools/cost` and its data files | `zfcost render --check` in CI, for the cost page only | 4-zone page and FAQ: right, by hand (C3) |
| 8 printed parts; 91 pairs, 89 gaps, 20 alignments | `enclosure/Makefile`; `enclosure/output/check-report.md` | The enclosure build writes the report. Prose is by hand | Status, enclosure, test-fit and mechanical pages: right |
| 873 host tests; 45 ESPHome host tests; 75 display scenes | The test runs | Nothing checks prose. CI compares the names of the display pictures with the scene list | Firmware architecture: right. ESPHome page and display document: stale (B2) |
| Keep-alive on GPIO17, encoder switch on GPIO38 | `hardware/pinmap.yaml` | `design-check` against the netlist; both firmwares generate their pins | Right in every current document. Review A1 keeps GPIO38 as a record (C2) |
| Specification v0.7 | The first line of the specification | Nothing | Every citation of an older version names the version a text was written against. None claims an older version is current |

Where a number is prose only, the cheapest guard is to stop repeating it.

The first column of this table was brought up to date on 2026-11-12, after
the audit, by the pull request that moved the debounce of the three push
contacts into firmware (hardware design, section 28): five parts and two
nets fewer, two bench risks and 14 host tests more, and the landed cost with
them. The audit found 353 components, 351 fitted, 200 nets, 356 footprints,
312 and 267 placements, 34 risks, 859 tests and 121.28, 89.72, 70.62 and
53.76 USD. The results in section 2 and the findings are of the audit's
commit and are left as they were written.
The status page and the issue could say "the risk register" and link to it.
This audit did not build a checker for prose.

## 5. The specification against what exists

"Exists" means the file or text is in the repository. It does not mean that
it works: no requirement has been checked on hardware.

| Spec | Requirement | Where it is met | State |
|---|---|---|---|
| 1 | Supply, zones, master, load, radio, sensors | Hardware design sections 3 to 13; `hardware/netlist.yaml` | Exists, by calculation and simulation |
| 1 | Diagnostics: current, open and shorted valve, loss of 24 VAC | Hardware design 9 to 11; `zf_core` `diag/`, `hw/output_stage` | Exists, host-tested |
| 1 | Local UI, timekeeping | Hardware design 14, 15; `zf_display`; display document | Exists; drawn in the simulator only |
| 1 | Machine-assembled SMT; through-hole only where no SMT part exists | Placement files leave out seven parts | C1 departs (A9) |
| 2 rules 1, 2, 3, 5, 6 | Outputs off by default, no strapping pins, two keep-alive detectors, Class 2 only, relay in the common | Hardware design 7 and 8; review A2's fault injection in `hardware/sim/failsafe/`; pin map rules | Exists, by simulation |
| 2 rule 3 | Firmware tests both detectors before and after every run | `zf_core` `hw/output_stage`; hardware design 19.7 | Exists, host-tested. Two questions to the hardware are open, issue #45 |
| 2 rule 4 | Maximum run time per zone with rest time | `zf_core` `zone/zone_driver`; defaults 60 min and 30 min in `config/config.hpp` | Exists, host-tested and in the soak simulation |
| 3.1 | Each baseline decision | Hardware design, the sections its departures list names | Exists. Section 2 of the hardware design lists seven departures that the specification has since taken over |
| 3.1 | Every BOM line carries an LCSC number | `hardware/bom-preliminary.csv` | 98 of 99 lines (B15) |
| 3.2 | Pin numbers from one generated header | `gen_board_header.py`, `zoneflow_rev_a.hpp`; a search of both firmwares finds GPIO numbers in comments only | Met |
| 3.3 | KiCad project that passes ERC and DRC | `hardware/zoneflow-a/`; `output/reports/` | Met; run again today |
| 3.3 | Schematic PDF, Gerbers, drill, pick-and-place, BOM with LCSC numbers, STEP, renders | `hardware/zoneflow-a/output/` | All present |
| 3.3 | Design notes with the four calculations | Hardware design; indexed by the design notes page | Present |
| 3.3 | Bring-up and test procedure | Hardware design section 21, 18 steps | Present, as a section, not a document of its own |
| 4.1 | Zone driver, scheduler, configuration, provisioning, web UI, REST API, event stream, MQTT, OTA, display, diagnostics, host tests | `firmware/zoneflow-fw/components/`; `web/`; `docs/firmware/openapi.json` | All exist. Only logic is tested; the ESP-IDF glue is compiled and has never run |
| 4.2 | ESPHome package with its additions | `firmware/esphome/` | Exists; compiles in CI |
| 4.3 | Browser installer and release builds by CI | `docs/firmware/web-installer.md`, `tools/zfrelease`, `tools/zfflash`, `release.yml` | Tooling exists. No release has been built for publication (A5) |
| 4.3 | Browser demo; the site says it is a simulation wherever it offers it | `firmware/zoneflow-fw/sim/web/`, `docs/try-it.md` | Exists. The home page lacked the words (B10) |
| 4.4 | Nothing prevents an OpenSprinkler profile or Matter | Not checked by anything | Not assessed |
| 5 | Prints without supports on a 220 mm bed; wiring cover; window, controls, light pipes; keyholes, cable entry, strain relief | `enclosure/zoneflow.scad`; `enclosure/output/check-report.md` | Exists as a model. Nothing printed |
| 5 | Source, STL, 3MF, STEP if the tool allows, renders, print settings | `enclosure/`, `enclosure/output/`, the printing page | Present, except STEP (B18) |
| 6 | Landing page, demo page, getting started, hardware reference, firmware guides and API reference, enclosure pages, troubleshooting, safety | The site navigation | All present |
| 7 | Licences | `LICENSE`, `hardware/LICENSE`, `enclosure/LICENSE`, `docs/LICENSE` | Present. See A8 and A10 |
| 8 | Every release states which checks were done and which need a bench | The status page; the release notes written by `zfrelease notes` say that booting needs a board | No release yet |

## 6. Contracts between subsystems

| Contract | How it was checked | Result |
|---|---|---|
| Pin map against the netlist | `design-check`: every GPIO, the enable pin and all 16 expander bits against the module's and the expander's pins | Agrees |
| Pin map against the standalone firmware | The header is generated at every build. The hand-written header takes every pin from it and asserts the pin rules | Agrees. Constants it adds were compared by hand with the netlist: shunt 0.1 ohm with 49.9 k and 10 k over 7.5 k give 1.503 and 7.50 mA per mV; the supply divider is 300 k over 10 k; expander at 0x74 with A0 and A1 at ground; clock at 0x51 |
| Pin map against the ESPHome package | `gen_board_yaml.py --check`; a search of the package for GPIO numbers outside the generated file | Agrees |
| Netlist against the schematic and the board | Netlist parity, build parity, DRC with schematic parity | Agrees |
| Mechanical contract against the board | `hwflow mech` in `board-check` | Agrees: outline, 4 holes, 26 placed parts, height zones for 294 parts |
| Mechanical contract and board against the enclosure | The three generators of `enclosure/tools/` run into scratch files | The committed parameter files are what they give. The OpenSCAD build and its interference check were not run again |
| REST API document against the firmware | A host test compares the route table with `openapi.json`; another checks the firmware's answers against its schemas (in the 859) | Agrees |
| REST API document against the web interface | `npm run check:api` in CI generates the types; the mock's answers are tested against the schemas | Not run locally; green on `main` |
| REST API document against the manual | A script over every route named in `docs/` | Each exists |
| MQTT topics against the manual | The 12 state topics and 12 command topics of the MQTT page compared by hand with `mqtt_logic.hpp` and `mqtt_logic.cpp`; the 720 h limit | Agrees |
| Firmware defaults against the manual | Current window 80 to 700 mA, settle time 500 ms, short at 3000 mA, maximum run 60 min, rest 30 min, master 1 s before and 2 s after, 450 pulses per litre, leak at 0.5 L/min for 60 s, missed start 60 min, 16 programs with 4 start times, sensor debounce 2 s, compared with `zf_core` | Agrees |
| Walkthrough against the board | `review-check`; the site build refuses pictures of another board file | Agrees |

## 7. The manual, read as an installer with a board

The pages were followed in the order Getting started gives.

| Step | Can it be done with what the repository has? |
|---|---|
| Safety, what you need | Yes. "How you get a board is open" is said plainly |
| Mounting | Yes on paper. The drilling template is published. Nothing printed |
| Wiring | Yes. Terminal names agree with the netlist and with the labels the layout prints. Strip length and torque are marked as missing |
| Installing firmware | **No**, before this audit (A5). Now: build from the repository; the commands are on the page. Not tried on a board |
| First power-up | Yes on paper. The ESPHome status screen has no picture, and the page says so |
| Joining the network | Standalone: yes on paper. ESPHome with `esphome run`: **no**, the tag did not exist (A6) |
| First schedule, testing zones | Yes on paper. The limits and defaults quoted agree with the firmware |
| Enclosure printing and assembly | Yes on paper. Screw and insert counts agree between the pages (7 and 7) |

Nothing in these pages reads as if the product were tested. Every page
carries one warning at the top, and the stronger statements (the fail-safe,
flashing with valves connected) repeat it in place. The safety page, the
ordering page and the site-wide banner were not weakened.

Dead ends that remain: the installer, `zfflash` and the esptool commands
until a release exists (issue #8); the photographs and measured values that
the "To be completed" notes name (issue #33).

## 8. CI

| Workflow | Runs on | Guards | Does not guard |
|---|---|---|---|
| `docs` | Every pull request and push | Strict site build, internal links, cost page against its data, walkthrough against the board file, vendored ESP Web Tools, the browser demo's build and smoke test | External links. Prose numbers. The step that adds a release to the site has never done anything: there is no release |
| `hardware` | Changes under `hardware/`, the hardware design, `tools/hwflow`, `tools/designsync`, `tools/reviewwalk` | Design consistency; hwflow tests with and without KiCad; the schematic regenerated and compared; the board rebuilt and checked; the walkthrough pictures compared | The board file and fabrication outputs against the sources (A7). `hardware/toolchain-test/`. The simulations |
| `enclosure` | Changes under `enclosure/`, the contract, the board file, the heights table | `board_layout.json` regenerated and compared; all print files rebuilt and compared; the interference check | Pictures and the drilling template PDF, which it says it does not compare. The view-only board model |
| `firmware` | Changes under `firmware/` except ESPHome, the pin map, the API document, the display pictures | Host tests, the soak subset, the display simulator and the names of its pictures, formatting, the ESP-IDF build | The pixels of the display pictures. The long soak run. The broker test |
| `esphome` | Changes to the package, `zf_core`, the board header, the pin map | The generated pin file, host tests, lint, compile of three configurations | `examples/minimal.yaml`, which cannot build until a tag exists (A6) |
| `web-ui` | Changes to the web interface, the schedule vectors, the API document | API types, schedule vectors, unit tests, size budget, end-to-end tests on three viewports | The 104 screenshots in `docs/firmware/img/webui/` |
| `release` | Tags, and pull requests that touch the release tooling or what goes into an image | Tooling tests, both firmware builds, the layout of the factory image, the manifest, the checksums, the installer page | Never runs on a push to `main` |

**Generated and committed, with no regenerate-and-compare job:**

- `hardware/zoneflow-a/zoneflow-a.kicad_pcb`, `design/board.toml` and everything in `output/` that the schematic job does not write: Gerbers, drill files, placement files, the pin 1 table, the STEP, the renders, the interactive BOM, the board and assembly PDFs, the board plots, `reports/drc.*`, `reports/routing.md`, `reports/layout-rules.md`, `reports/build-summary.json`, `reports/placement-check.md`.
- All of `hardware/toolchain-test/`.
- `hardware/sim/failsafe/results/` and the tables in the design documents that quote them.
- The pictures: `docs/design/display-ui/` (names are compared, pixels are not), `docs/firmware/img/webui/`, the enclosure renders and plate pictures in `enclosure/output/` and `docs/assets/enclosure/`, the drilling template.
- `enclosure/output/test-fit/view-only/`.

**Tests and checks that exist and that no workflow runs:**

- `firmware/zoneflow-fw/test/broker/run.sh`, the MQTT logic against a real broker. Its README says it is not in CI.
- `firmware/zoneflow-fw/test/soak/run_long.sh`, by design: about two hours.
- `tools/reviewwalk/tests/browser/walk.cjs`, the walk through every step of the review page in a browser.
- `make cpl-check`, the placement files against JLCPCB's library. It needs the network.
- `make check` in a board directory, which also proves that ERC and DRC catch deliberately broken inputs, and the full `make` of either board.
- The ngspice runs of `hardware/sim/`.
- The web interface's `screenshots` project.

`tools/designsync`, `tools/docs/check_links.py`, `tools/mkdocs_hooks/` and
`enclosure/tools/` have no unit tests. Each runs in CI as part of a build.

## 9. Licences

The four `LICENSE` files are the right ones for the specification's table:
Apache-2.0 at the root, CERN-OHL-P-2.0 in `hardware/` and `enclosure/`,
CC-BY-4.0 in `docs/`. `firmware/` and `tools/` have no file of their own
and fall under the root file. 305 source files carry an SPDX line. It agrees
with the directory in 304. The other is `docs/hardware/review/review.js`,
program code under `docs/` that is marked Apache-2.0, which is the licence
for code. The enclosure sources, `tools/cost`, `tools/zfflash`,
`tools/zfrelease` and `tools/designsync` carry no SPDX line and rely on the
`LICENSE` file above them.

Vendored and embedded material, and the tools the builds download, are now
listed on [third-party material](../third-party.md) with how each licence
was read. Two rows there say "not read" or "not stated" and mean it. A8
is the open question.

## 10. Open issues

The audit changed no issue. For each, a recommendation and a comment ready
to post.

| Issue | Recommendation | Comment |
|---|---|---|
| #5 PCB layout | Nothing to do: closed on 2026-11-11 with the evidence | None |
| #6 Standalone firmware | Keep, and reword to what is left | "Audit of 2026-11-12: every item of specification 4.1 is in `firmware/zoneflow-fw/` and covered by host tests (859 pass) and the soak simulation; the ESP-IDF build is green. What is left needs a board: the first run on hardware, the self-test limits and current thresholds from measurements, the display and encoder, provisioning with a real phone. Suggested title: 'Standalone firmware: first run on hardware'. Depends on #33." |
| #8 Installer and release builds | Keep, and reword | "Audit of 2026-11-12: the web installer, the release workflow, `zfflash`, `zfrelease` and the browser demo are merged and tested in CI. No release has been published. Left: decide when to tag the first release; decide the licence statement and the third-party notice of the ESPHome image (docs/third-party.md); the installer and `zfflash` against a real board. Suggested title: 'First firmware release'." |
| #9 Enclosure | Keep, and reword. It can start now | "Audit of 2026-11-12: the model, the STL and 3MF files, the plates, the drilling template, the test-fit kit generated from the board file and the interference check are merged, and CI rebuilds the print files. Nothing has been printed. Left: print the enclosure and the test-fit kit and go through docs/enclosure/test-fit.md; a thermal test with a board. The print needs no board. Suggested title: 'Enclosure: print and test fit'." |
| #10 Documentation site | Close | "Audit of 2026-11-12: every item of specification section 6 is on the site and in its navigation: landing page, browser demo with a page on what it simulates, getting started, hardware reference with schematic, BOM, design notes and ordering, both firmware guides and the API reference, enclosure printing and assembly, troubleshooting and safety. CI builds it strictly and checks its links. What is missing are photographs and measured values, which need a prototype and are tracked in #33. Closing." |
| #11 Independent design review | Keep, and reword | "Audit of 2026-11-12: the circuit was reviewed twice (A1, A2), the firmware's questions were answered, and the layout was reviewed (30 findings, fixed in the sources). The list of what needs a bench is hardware design section 20, 34 items. What is left is the part a person does: the pre-order review walkthrough, recorded with `reviewwalk record` under docs/design/reviews/, and a sample display panel to confirm the flat cable. Close when a record is merged. Suggested title: 'Pre-order review by a person'." |
| #33 Prototype and bench verification | Keep, and correct the text | "Audit of 2026-11-12: the bench list in hardware design section 20 now has 34 items, not 27, and the bring-up plan 18 steps. The layout (#5) is merged and reviewed. This is now blocked on three things: the pre-order review by a person (#11), a sample display panel, and the owner's decision to order." |
| #45 Two soak gaps for the hardware design | Keep | "Audit of 2026-11-12: both questions are still open. Firmware architecture section 22 marks gaps 1 and 10 as the hardware author's, and neither the hardware design nor a later document answers them. They do not block the pre-order review: the first is a question of what rule 3 covers, the second can be answered by a simulation in `hardware/sim/failsafe/`." |

Issue #31, reducing the part count, is deferred by the owner and was not
looked at.

## 11. Follow-up issues, ready to post

**CI: compare the board file and the fabrication outputs with what the sources generate (A7).**
The `hardware` workflow regenerates the schematic and fails on a difference.
Its `board` job runs `make board-check` and compares nothing. Add a
`git diff --exit-code` step after it for `zoneflow-a.kicad_pcb`,
`design/board.toml` and `output/reports/`, which `board-check` writes. On the
build machine with KiCad 10.0.8 that step would pass today. Then decide
whether a job should run the full `make` and compare Gerbers, drill files
and placement files; the STEP and the two renders differ from run to run
(hardware toolchain 13.8) and must be left out.

**Decide the licence statement of the ESPHome firmware image, and add a notice to releases (A8).**
The project's firmware code is Apache-2.0. The image built from
`firmware/esphome/factory.yaml` links it with ESPHome's runtime, which
ESPHome publishes under GPLv3 (`LICENSE` of `esphome/esphome`). Before the
first release: state on the release and in `docs/firmware/esphome.md` under
which terms that image is distributed, and attach a notice file that names
the third-party code in both images (`docs/third-party.md` has the list).
This is a question for the owner and, if in doubt, for someone qualified in
licensing. No release exists, so nothing has been distributed.

**Specification: two requirements the design does not meet as written (A9, B15).**
Section 1, Manufacture, allows through-hole parts only for terminal blocks
and parts with no SMT equivalent. C1 is a through-hole capacitor although a
surface-mount one was in the first draft; the reason is the single assembly
side (mechanical design section 6, hardware design section 4.5). Section 3.1
says every BOM line carries an LCSC part number; the CR2032 cell has none
(JLCPCB does not fit it). Either change the two sentences, for example
"...and parts that the enclosure or a single assembly side rules out as
SMT" and "every line that JLCPCB fits", or change the design. Add both to
section 2 of the hardware design, which lists the departures.

**Register the commit signing key with GitHub (B19).**
Commits signed with key `D36B565C5B85FD62` show as unverified on GitHub
(the first commit of `main`, and every branch commit). Add the public key
to the account that owns the commits.

## 12. Decisions made in this audit

- A requirement was not changed, and neither was the circuit, the copper,
  the mechanical contract, firmware behaviour or the enclosure geometry.
  The one edit under `hardware/zoneflow-a/design/` is a comment; `make`
  wrote `board.toml` again and `board-check` passed with no other change.
- Dated records were not rewritten to today's numbers (C2). Where a dated
  remark had become false and looked current, a sentence was added beside
  it (B8).
- The ESPHome example keeps its tag `v0.2.0` with a comment, because the
  guide explains the two places a tag goes; pointing it at `main` would
  hide that.
- No checker for prose numbers was built. Section 4 says which numbers have
  a guard and which do not.
- The third-party page records only licences that were read, and says how.
