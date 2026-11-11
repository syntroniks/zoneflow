# ZoneFlow enclosure

Parametric OpenSCAD source for the wall-mount enclosure. Designed on paper:
nothing here has been printed.

- **`zoneflow.scad`**: the model. Every part is modelled in its assembled position and turned into print orientation for export.
- **`params.scad`**: the enclosure's own parameters. Wall thickness, clearances, fasteners, vents.
- **`testfit.scad`**: the printable test-fit kit. Stand-ins for the board, C1, K1, the plugs and the display panel, and how each is printed without support.
- **`plates.toml`**: which parts are printed together.
- **`board_params.scad`**: generated from `../hardware/mechanical.yaml`. Do not edit it.
- **`board_layout.json`**: generated from the board file, `../hardware/zoneflow-a/zoneflow-a.kicad_pcb`. Do not edit it.
- **`testfit_params.scad`**: generated from the two above and `../hardware/zoneflow-a/design/heights.toml`. Do not edit it.
- **`tools/gen_params.py`**: writes `board_params.scad` and rejects a contract that contradicts itself.
- **`tools/board_layout.py`**: writes `board_layout.json`. The one script that needs KiCad's `pcbnew` module.
- **`tools/gen_testfit.py`**: writes `testfit_params.scad` and stops if the board file and the contract disagree.
- **`tools/clean_stl.py`**: welds each exported STL into one closed solid.
- **`tools/threemf.py`**: writes each 3MF and each plate from the STL files, the same bytes every time.
- **`tools/check.py`**: intersects the parts with each other, with the contract's placeholder board, with the test-fit kit and with KiCad's detailed model of the board. It measures the gaps a person checks by hand and inspects every print file. Any failure fails the build.
- **`tools/pack.py`**: packs the print files into one zip.
- **`fonts/`**: Liberation Sans, under the SIL Open Font Licence, for the engraved markings. The system's own fonts gave different results from one run to the next.
- **`output/`**: STL and 3MF for each part in print orientation, the renders, the drilling template and the check report. `output/test-fit/` has the test-fit kit, `output/plates/` one 3MF per print batch, `output/test-fit/view-only/` the board as KiCad models it. Committed so that nobody needs the tools to print.

## Build

```sh
make openscad   # once: fetch the OpenSCAD snapshot into .cache/
make            # everything, about half a minute
make test-fit   # the same without the product renders and the drilling template
```

`make` needs an OpenSCAD with the Manifold backend, `uv`, Python 3.11 or
later and KiCad 10. To use another OpenSCAD build, run
`make OPENSCAD=/path/to/openscad`.

KiCad is needed for two files only, which are committed. Without it:

```sh
make -o board_layout.json -o output/test-fit/view-only/zoneflow-a-board-detailed-view-only.stl
```

`build/zoneflow-test-fit-print-files.zip` is every print file in one archive.
The documentation build publishes the same archive.

To look at one part:

```sh
openscad -D 'part="fascia"' zoneflow.scad
```

Values for `part`: `assembly`, `open`, `exploded`, `section`, `section_side`, `board`,
`zones`, `base`, `fascia`, `cover`, `bridge`, `knob`, `button_back`,
`button_home`, `lightpipe`, `template`, and `print_` plus any part name.
For the test-fit kit: `tf_board`, `tf_kit`, `tf_underside`, `tf_scene` (with
`-D tf_step=1` to `5`), and `print_tf_board`, `print_tf_c1`, `print_tf_k1`,
`print_tf_plug_2p`, `print_tf_plug_4p`, `print_tf_plug_7p`, `print_tf_panel`.

Add `-D 'panel="2.4"'` to see the fascia and carrier for the 2.4 inch panel
option.

The reasoning is in `docs/design/mechanical.md` and `docs/enclosure/`. The
dry fit is in `docs/enclosure/test-fit.md`.

Licence: CERN-OHL-P-2.0, see `LICENSE`. The fonts keep their own licence.
