# Working in this repository

ZoneFlow is an open-source 24 VAC irrigation controller. Read
`docs/design/system-spec.md` before changing anything; it is the source of
truth. If your work needs the spec to change, change the spec in the same pull
request and say so in the description.

## Git rules

- Never commit to `main`. Work on a branch, open a pull request to
  `syntroniks/zoneflow`.
- Conventional Commits: `type(scope): summary`, imperative, lowercase, at most
  72 characters. Types: feat, fix, docs, chore, refactor, test, perf, ci,
  build, revert. Scopes: `hw`, `fw`, `esphome`, `enclosure`, `docs`, `tools`,
  `ci`.
- Every commit is GPG-signed and signed off: `git commit -S -s`. The repo
  config already holds the identity and key. Never pass `--no-gpg-sign`.
- No AI attribution anywhere: no `Co-Authored-By` trailers, no "Generated
  with" footers in commits, pull requests, issues or comments.
- Do not commit build output unless a section below says it is a release
  artefact.

## Honesty rules

- This design has never been built. Do not write that something "works" or
  "is tested" unless you ran the check; name the check.
- Every part number must come from a datasheet or distributor page you
  actually read. Record the source. Do not invent LCSC numbers.
- State calculations with their inputs so a reviewer can redo them.

## Layout

- `hardware/` KiCad 10 project. Generated outputs go in `hardware/output/`.
- `firmware/zoneflow-fw/` ESP-IDF firmware. Pin numbers live in one board
  header only.
- `firmware/esphome/` ESPHome package.
- `enclosure/` OpenSCAD source; exports in `enclosure/output/`.
- `docs/` site source. `docs/design/` holds design documents.

## Hardware flow

Boards are generated from text. Read `docs/design/hardware-toolchain.md`
before touching anything under `hardware/`.

- Edit only `design/circuit.py`, `design/board.toml` and
  `design/rules.kicad_dru` in a board directory, and the files in
  `hardware/rules/`. Run `make` there. It fails if ERC or DRC fail.
- The `.kicad_sch`, `.kicad_pcb`, `.kicad_pro`, library tables, `route/` and
  `output/` in a board directory are generated and committed, so that a
  reviewer can open the project and see the outputs without the tools. Never
  edit them by hand; the next `make` overwrites them.
- `hardware/toolchain-test/` is a test of the flow, not the product. Do not
  reuse its part choices.

## Enclosure flow

The enclosure is generated from text. Read `docs/design/mechanical.md` before
touching `hardware/mechanical.yaml` or anything under `enclosure/`.

- `hardware/mechanical.yaml` is the mechanical contract between the board
  and the enclosure. Edit it, `enclosure/params.scad` and
  `enclosure/zoneflow.scad`. Run `make` in `enclosure/`. It fails if the
  interference check fails.
- `enclosure/board_params.scad`, `enclosure/output/` and
  `docs/assets/enclosure/` are generated and committed, so that someone can
  print the parts and read the site without the tools. Never edit them by
  hand.
- `make` needs an OpenSCAD with the Manifold backend. The packaged 2021.01
  does not have it. `make openscad` in `enclosure/` fetches a snapshot into
  `enclosure/.cache/`, which is not committed.
- Nothing has been printed. Claim only what `enclosure/tools/check.py`
  checked.

## Tools on the build machine

KiCad 10 (`kicad-cli`, `pcbnew` Python module), ngspice, OpenSCAD, `uv`,
headless Java (for Freerouting), `xvfb-run`, Node, Rust. Install Python tools
with `uv` into a project-local environment, not system-wide.
