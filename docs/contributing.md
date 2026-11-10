# Contributing

ZoneFlow is open source and in early design. Contributions are welcome:
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
| `hardware/` | KiCad 10 project. Generated outputs go in `hardware/output/` |
| `firmware/zoneflow-fw/` | ESP-IDF firmware. Pin numbers live in one board header only |
| `firmware/esphome/` | ESPHome package |
| `enclosure/` | OpenSCAD source. Exports in `enclosure/output/` |
| `docs/` | This site's source. `docs/design/` holds design documents |

Do not commit build output unless the repository says it is a release
artefact.

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

The build must pass with `--strict`. Pull requests run it in CI. A push to
`main` deploys the site to GitHub Pages.

To add a page, create the file under `docs/` and add it to `nav` in
`mkdocs.yml`.

## Licences

By contributing you agree that your work is released under the licence of the
part you change:

| Part | Licence |
|---|---|
| Hardware and enclosure | CERN-OHL-P-2.0 |
| Firmware and tools | Apache-2.0 |
| Documentation | CC-BY-4.0 |
