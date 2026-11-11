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
| `tools/mkdocs_hooks/review_walkthrough.py` | Puts all of it into the site, and fails the build on a problem |
| `hardware/zoneflow-a/output/review/` | The pictures and `manifest.json`. Generated and committed |

## Commands

```
make -C hardware/zoneflow-a review         # draw the pictures from the board; part of `make`
make -C hardware/zoneflow-a review-check   # draw them again into build/ and compare with the committed ones
PYTHONPATH=tools/reviewwalk python3 -m reviewwalk check   # content and pictures against the board, no KiCad
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

## Writing a step

A step is a `[[step]]` table. `check`, `why`, `good`, `bad` and `how` are
needed. `fact` holds what a datasheet says, with the document and the place
in it: write there only what you read yourself, and say so where you did
not. `cannot` says what the picture does not show. A `[[step.figure]]` is
one picture:

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
colour schemes, no storage, no JavaScript. It is not part of CI, which has
no browser. Its header says how to run it.
