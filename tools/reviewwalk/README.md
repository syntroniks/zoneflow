# reviewwalk

The pre-order review walkthrough of the revision A board: the page at
`docs/hardware/review/` on the site, its bench sheet, and the pictures both
show. No board has been made. The pictures are drawings of the board file.

## What is where

| Path | What |
|---|---|
| `docs/hardware/review/data/*.toml` | The content: groups, steps, findings, and a description of every picture |
| `docs/hardware/review/review.js`, `review.css` | The page's behaviour and looks |
| `docs/hardware/review/find.html` | Opens the interactive BOM at a part or a net |
| `tools/reviewwalk/reviewwalk/render.py` | Draws the pictures from the board file. Needs KiCad's Python |
| `tools/reviewwalk/reviewwalk/check.py` | Is the content still about the committed board? Python only |
| `tools/reviewwalk/reviewwalk/page.py` | Content and manifest to HTML |
| `tools/reviewwalk/reviewwalk/handoff.py` | Reads and checks a hand-off file and writes its record |
| `docs/hardware/review/handoff.schema.json` | The schema of the decisions in a hand-off file, version 1 |
| `docs/design/reviews/` | The records that `reviewwalk record` writes, and the page that lists them |
| `tools/mkdocs_hooks/review_walkthrough.py` | Puts all of it into the site, and fails the build on a problem |
| `hardware/zoneflow-a/output/review/` | The pictures and `manifest.json`. Generated and committed |

## Commands

```
make -C hardware/zoneflow-a review         # draw the pictures from the board; part of `make`
make -C hardware/zoneflow-a review-check   # draw them again into build/ and compare with the committed ones
PYTHONPATH=tools/reviewwalk python3 -m reviewwalk check   # content and pictures against the board, no KiCad
uv run --project tools/reviewwalk reviewwalk record FILE  # check a hand-off file and write its record
cd tools/reviewwalk && uv run --locked pytest -q          # tests; those that draw are skipped without KiCad
node --test tools/reviewwalk/tests/js/review.test.cjs     # the page logic
```

## How the pictures stay true

- **Drawn from the board.** Pads, tracks, vias, zone fills, drawings and text
  become outlines through KiCad's own conversion (`TransformShapeToPolygon`
  and its relatives). `render.py` adds the crop, the colours, the marking of
  nets and parts, and the labels.
- **A part that is gone stops the build.** `generate` fails on a part or a
  net it cannot find. `check` reads the board file as text and fails when any
  designator named in a picture description or in the text of a step or a
  finding is not a part on the board. A finding id such as L5, and the words
  in `meta.not_parts`, are not taken for parts.
- **Pictures of another board are refused.** `manifest.json` holds one hash
  over the board file, the picture descriptions, the drawing code and the
  place of the board in the top render. The bytes of the render do not
  count: KiCad does not write the same picture file twice. `check` and the site build compare it with the files in the
  repository. Change any of them and the site does not build until
  `make -C hardware/zoneflow-a review` has run and its result is committed.
  Changing the text of a step does not need new pictures.
- **CI draws them again.** The `board` job of the hardware workflow runs
  `review-check` on the committed board under the KiCad image and compares,
  with a tolerance of 0.02 picture units for rounding.

## The hand-off to a coding agent

The owner's decisions leave the page as one Markdown file, taken from the
Result tab by copy or by download. Nothing is sent. The file has three
parts:

1. **Instructions for the agent.** The repository, the board revision, the
   SHA-256 of the board file that was reviewed and the commit the page was
   built from. Then what to do: read `CLAUDE.md`, check the board hash,
   record the review, one pull request for each change, nothing for a
   passed step, a question to the owner for an unsure one, and never an
   order.
2. **The decisions as JSON,** in a fenced block, following
   `docs/hardware/review/handoff.schema.json`. A step that is not passed
   carries the parts, the nets, the position in both coordinate systems,
   the pictures, the source files a change would be made in, where the step
   comes from, and for a failed or rejected step the remedy. A passed or
   accepted step has its id, title, decision and note only.
3. **The same as text,** changes first and passed steps last.

A step with `kind = "decision"` is answered Accept or Reject and recorded
as "accepted" or "rejected". The others are "pass" or "fail". A remedy is
"agent" when the owner chose none, "listed" with the id of one that the
step offers, or "own" with the owner's words.

`reviewwalk record FILE` checks the JSON twice. First against the rules of
the schema, which `handoff.py` states in Python so that the command needs no
package; a test holds the two against each other with 21 broken files. Then
against the repository as it is: the board file must have the hash that was
reviewed, every step must exist and be of the kind the file says, every
part and net must be on the board, every file must exist, a listed remedy
must be one the step offers, and the counts must add up. It then writes
`docs/design/reviews/<date>-pre-order.md` and the JSON beside it, and adds
the record to `docs/design/reviews/index.md`. `--allow-other-board` records
a review of another board file and says so in the record. `--force` writes
over a record of the same date.

The page takes a hand-off file back in, by paste or by file, and replaces
its answers with those of the file. If the file is of another board file
than the page shows, it says so, keeps the answers, and writes no new
hand-off until the owner has confirmed them.

## Pictures that answer

Each shape in a drawn picture carries what the board file says of it:
`data-n` with the net on a group of copper shapes, `data-p` with part and
pad number on a pad, `data-r` with the part on outlines and text, and
`data-layer` on the group of a layer. An enlarged picture is read again as
SVG and put into the page. The layer boxes hide and show those groups. A
press on a shape shows its attributes in words. The page looks nothing up
and computes nothing, so it cannot name what the board does not have. The
annotation, which is the grid, the holes, the labels and the legend, lets a
press through to the copper under it.

## Writing a step

A step is a `[[step]]` table. `check`, `why`, `good`, `bad` and `how` are
needed. `fact` holds what a datasheet says, with the document and the place
in it: write there only what you read yourself, and say so where you did
not. `cannot` says what the picture does not show.

For the hand-off, a step may have `kind = "decision"`, `files` and
`remedies`. `files` are the sources a change would be made in, never a
generated file; the files of its group are added. Every file named must
exist, or the build fails. `remedies` are the few likely cures, each with an
id and a sentence; those of its group follow, unless the step sets
`group_remedies = false`.

A `[[step.figure]]` is one picture:

```toml
[[step.figure]]
id = "pad"                       # the file is <step id>-<id>.svg
alt = "..."                      # for a screen reader
caption = "..."
box = [16.0, 29.0, 27.0, 38.0]   # board coordinates in mm; or around = ["U1"] and margin = 2.0
view = "top"                     # "bottom" mirrors, as the board looks from below
layers = ["F.Cu", "Edge.Cuts"]   # drawn in this order
zones = true                     # false leaves the pours out
marks = [{ nets = ["GND"], refs = ["U1"], label = "GND" }]   # up to four, each in its own colour
pad_labels = ["U1"]              # pad number and net on these parts
callouts = [{ at = [21.5, 33.0], text = "six vias", dx = 0, dy = -4.3, ring = 1.9 }]
```

Coordinates are those of the mechanical contract: origin at the bottom-left
corner seen from the component side, x right, y up. After a change, run
`make -C hardware/zoneflow-a review`, then look at the picture. A crop that
misses its part is worse than no picture.

## The browser walk

`tests/browser/walk.cjs` goes through all steps in Chromium with Playwright:
every picture loads, zoom, answers over a reload, the report, print, both
colour schemes, no storage, no JavaScript. It also takes the hand-off file
from the page, gives it to `reviewwalk record`, and brings it back in on a
fresh page, once as it is and once as a file of another board. It is not part of CI, which has
no browser. Its header says how to run it.
