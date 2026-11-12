# Contributing

ZoneFlow is open source and designed on paper; nothing has been built yet. Contributions are welcome:
schematic review, firmware, documentation, enclosure work and bug reports.

## Start here

Read the [system specification](design/system-spec.md). It is the source of
truth. If your change needs the specification to change, change it in the same
pull request and say so in the description.

Open work is tracked in
[GitHub issues](https://github.com/syntroniks/zoneflow/issues).

## Branches and pull requests

- Do not commit to `main`.
- Create a branch from `main`, make your changes, and open a pull request to
  `syntroniks/zoneflow`. If you do not have write access, fork the repository
  first.
- Reference the issue in the description, for example `Refs #10`.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/):

```text
type(scope): summary
```

- Write the summary in the imperative mood and in lowercase.
- Keep the first line to 72 characters or fewer.
- Types: `feat`, `fix`, `docs`, `chore`, `refactor`, `test`, `perf`, `ci`,
  `build`, `revert`.
- Scopes: `hw`, `fw`, `esphome`, `enclosure`, `docs`, `tools`, `ci`.

Example: `docs(hw): add output stage design notes`

## Signed commits and sign-off

Every commit must be GPG-signed and carry a sign-off line:

```sh
git commit -S -s
```

`-S` signs the commit with your GPG key. `-s` adds a `Signed-off-by:` line.
Set up your name, email and signing key in your Git configuration first. Do
not bypass signing.

Do not add AI attribution to commits, pull requests, issues or comments. That
means no `Co-Authored-By` trailers for AI tools and no "generated with"
footers.

## Be honest about what is verified

This design has never been built.

- Do not write that something "works" or "is tested" unless you ran the check.
  Name the check.
- Every part number must come from a datasheet or distributor page you read.
  Record the source. Do not guess LCSC numbers.
- State calculations with their inputs so a reviewer can redo them.

## Repository layout

| Path | Contents |
|---|---|
| `hardware/` | KiCad 10 projects generated from text, one directory for each board. Generated outputs go in the board's `output/`, for example `hardware/zoneflow-a/output/` |
| `firmware/zoneflow-fw/` | ESP-IDF firmware. Pin numbers live in one board header only |
| `firmware/esphome/` | ESPHome package |
| `enclosure/` | OpenSCAD source. Exports in `enclosure/output/` |
| `docs/` | This site's source. `docs/design/` holds design documents |

Do not commit build output, with one exception: the generated files that
let a reader open the project, order boards or print parts without the
tools. Those are committed and rebuilt by `make`: the KiCad project and
`output/` of each board, `enclosure/output/` and the files beside it that
`enclosure/README.md` names, `docs/assets/enclosure/`, and
`firmware/esphome/zoneflow/board-rev-a.yaml`. Never edit them by hand.

## Working on this site

The site uses [MkDocs](https://www.mkdocs.org/) with the Material theme.
Dependencies are pinned with [uv](https://docs.astral.sh/uv/) in
`pyproject.toml` and `uv.lock` at the repository root, in the `docs`
dependency group.

```sh
uv sync                    # install into .venv
uv run mkdocs serve        # preview at http://127.0.0.1:8000/zoneflow/
uv run mkdocs build --strict
```

```sh
python3 tools/docs/check_links.py site   # internal links, images and anchors
```

The build must pass with `--strict`, and the link check must find nothing
broken. Pull requests run both in CI. A push to `main` deploys the site to
GitHub Pages.

To add a page, create the file under `docs/` and add it to `nav` in
`mkdocs.yml`.

### How the site is organised

- **User manual**: Getting started, the user guide, troubleshooting, the hardware reference, the enclosure pages, FAQ and glossary. Written for the person who installs and uses the controller, from the design documents and the firmware source. Write procedures as numbered steps with one action each, and put a warning before the step it applies to.
- **Design documents** (`docs/design/`): the engineering record. The manual follows them; if they disagree, fix the manual or raise the contradiction.
- Each manual page states its verification status once, at the top, in a `warning` admonition titled "Written from the design; not yet verified on hardware" (or "Design values; ..." for reference tables). Do not hedge every sentence after that.
- Where a fact is not established yet, write a short `note` admonition titled "To be completed" that says what is missing and which issue unblocks it.
- Generated files the site shows (schematic SVGs and PDF, BOM CSVs, the drilling template) are published by `tools/mkdocs_hooks/hardware_outputs.py`; the BOM table is rendered from `hardware/bom-preliminary.csv` by `tools/mkdocs_hooks/bom_table.py`. Do not copy them into `docs/`.
- The diagrams in `docs/assets/diagrams/` are original SVGs drawn from the hardware design. Change them when the terminal order or the circuit changes.
- Spelling: British, "licence" as a noun. Metric units, with AWG in brackets for wire sizes.

## Licences

By contributing you agree that your work is released under the licence of the
part you change:

| Part | Licence |
|---|---|
| Hardware and enclosure | CERN-OHL-P-2.0 |
| Firmware and tools | Apache-2.0 |
| Documentation | CC-BY-4.0 |
