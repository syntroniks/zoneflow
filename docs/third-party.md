# Third-party material

ZoneFlow's own work is under three licences: hardware and enclosure
CERN-OHL-P-2.0, firmware and tools Apache-2.0, documentation CC-BY-4.0. This
page lists what comes from other projects, where it is, and under which
licence. It was written on 2026-11-12 by the
[repository audit](design/repository-audit.md). The last column says how each
licence was read. Nothing here is legal advice.

## In the repository

These files are committed, so a copy of the repository carries them.

| What | Where | Licence | Read from |
|---|---|---|---|
| ESP Web Tools 10.4.0, unmodified | `docs/assets/vendor/esp-web-tools/10.4.0/` | Apache-2.0 | `LICENSE` in that directory. `VENDOR.json` pins every file by SHA-256, and CI checks it |
| Liberation Sans, regular and bold | `enclosure/fonts/`. The engraved markings in the enclosure STL and 3MF files are outlines of its letters | SIL Open Font Licence 1.1 | `enclosure/fonts/LICENSE-LiberationFonts.txt` |
| KiCad symbols and footprints, library release 10.0.8 | Copies inside the generated `.kicad_sch` and `.kicad_pcb` files of `hardware/zoneflow-a/` and `hardware/toolchain-test/`, and in every plot of them. The libraries in `hardware/zoneflow-a/lib/` are the project's own, written by `design/symbols.py` and `design/footprints.py` | CC-BY-SA 4.0 with KiCad's exception: a design that uses library data, and the files generated from it, need not carry that licence | `LICENSE.md` of the `kicad-footprints` repository on gitlab.com, read on 2026-11-12 |
| KiCad 3D models, library release 10.0.8 | Inside `hardware/zoneflow-a/output/zoneflow-a.step`, `hardware/toolchain-test/output/toolchain_test.step`, the view-only board model in `enclosure/output/test-fit/view-only/`, and the board renders | The same licence and exception | The same file. The Arch Linux package `kicad-library-3d` 10.0.8 names CC-BY-SA-4.0 |
| Interactive HTML BOM 2.12.0 | Its script and styles are inside `hardware/zoneflow-a/output/zoneflow-a-ibom.html` | MIT | The licence GitHub reports for `openscopeproject/InteractiveHtmlBom`, read on 2026-11-12 |
| Split.js 1.3.5, lz-string 1.4.4, PEP 0.4.3 | Inside the same HTML file, put there by Interactive HTML BOM | MIT; WTFPL version 2; the jQuery Foundation's licence | The notice each carries in that file. The jQuery Foundation's licence page was not opened |

The simulation models in `hardware/sim/` are the project's own: generic
models fitted to data sheet figures, with no maker's model file among them.
The diagrams in `docs/assets/diagrams/`, the logo and the footprints drawn
from makers' drawings (`hardware/zoneflow-a/design/footprints.md`) are also
the project's own work.

## Built into what the project publishes

These are not in the repository. A build fetches them at a pinned version,
and the result is published: in a firmware image, in the browser demo or in
the documentation site.

| What | Goes into | Licence | Read from |
|---|---|---|---|
| ESP-IDF v5.5.6 and the components it brings, cJSON 1.7.19 among them | The standalone firmware and the ESPHome firmware | Apache-2.0 for ESP-IDF itself. Its components carry their own licences, which were not read for this page | The licence GitHub reports for `espressif/esp-idf` |
| LVGL 9.6.0, with its Montserrat font | The standalone firmware's display, the display simulator, the browser demo | MIT; the font SIL Open Font Licence 1.1 | The licence GitHub reports for `lvgl/lvgl`; the note in `firmware/zoneflow-fw/sim/lv_conf.h` for the font |
| `espressif/mdns` 1.14.1 | The standalone firmware | Apache-2.0 | `components/mdns/LICENSE` of `espressif/esp-protocols` |
| `joltwallet/littlefs` 1.22.3, which contains littlefs | The standalone firmware | MIT for the component, BSD-3-Clause for littlefs | The licences GitHub reports for `joltwallet/esp_littlefs` and `littlefs-project/littlefs` |
| cJSON 1.7.19 | The browser demo and the host tests | MIT | The licence GitHub reports for `DaveGamble/cJSON` |
| Preact 10.29.8, `@preact/signals` 2.11.3 | The web interface the controller serves, and the browser demo | MIT | The licences GitHub reports for `preactjs/preact` and `preactjs/signals` |
| ESPHome 2026.10.2 | The ESPHome firmware. The image is ESPHome's C++ runtime linked with the project's components | GPLv3 for ESPHome's C++ and runtime code, MIT for its Python and the rest | `LICENSE` of `esphome/esphome`, read on 2026-11-12 |
| Emscripten 7.0.0 runtime | The browser demo | MIT or University of Illinois/NCSA, at the user's choice | `LICENSE` of `emscripten-core/emscripten` |
| Swagger UI, through `mkdocs-swagger-ui-tag` | The REST API reference page of the site | Apache-2.0 | The licence GitHub reports for `swagger-api/swagger-ui` |
| Material for MkDocs | The documentation site | MIT | The licence GitHub reports for `squidfunk/mkdocs-material` |

Two things are open, and the [audit](design/repository-audit.md) lists
both. No release has been published, so neither has done harm yet.

- A firmware release does not ship a notice file that names these.
- The project's own firmware code is Apache-2.0. The ESPHome firmware image
  also contains ESPHome's runtime, which is GPLv3. Nothing in the repository
  says under which terms that image is published.

## Tools a build downloads and only runs

Nothing of these is committed or published. They are named here because the
builds fetch them by themselves.

| Tool | Fetched by | Licence | Read from |
|---|---|---|---|
| Freerouting 2.5.0 | `tools/hwflow`, into `hardware/.cache/` | GPL-3.0 | The licence GitHub reports for `freerouting/freerouting` |
| OpenSCAD snapshot 2026.11.06 | `make openscad` in `enclosure/`, into `enclosure/.cache/` | GPL version 2 with an exception for linking CGAL | `COPYING` of `openscad/openscad` |
| Emscripten SDK 7.0.0 | `firmware/zoneflow-fw/sim/web/build.sh --emsdk` | MIT or University of Illinois/NCSA | `LICENSE` of `emscripten-core/emscripten` |
| GoogleTest 1.17.0 | The host tests | BSD-3-Clause | The licence GitHub reports for `google/googletest` |
| JLCPCB library footprints | `make cpl-check`, into `hardware/.cache/`. Used to compare the placement files; not copied anywhere | Not stated by the source | |
