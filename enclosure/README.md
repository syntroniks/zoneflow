# ZoneFlow enclosure

Parametric OpenSCAD source for the wall-mount enclosure. Designed on paper:
nothing here has been printed.

- **`zoneflow.scad`**: the model. Every part is modelled in its assembled position and turned into print orientation for export.
- **`params.scad`**: the enclosure's own parameters. Wall thickness, clearances, fasteners, vents.
- **`board_params.scad`**: generated from `../hardware/mechanical.yaml`. Do not edit it.
- **`tools/gen_params.py`**: writes `board_params.scad` and rejects a contract that contradicts itself.
- **`tools/check.py`**: intersects the parts with each other and with the placeholder board, and fails the build on any overlap.
- **`fonts/`**: Liberation Sans, under the SIL Open Font Licence, for the engraved markings. The system's own fonts gave different results from one run to the next.
- **`output/`**: STL and 3MF for each part in print orientation, the renders, the drilling template and the check report. Committed so that nobody needs the tools to print.

## Build

```sh
make openscad   # once: fetch the OpenSCAD snapshot into .cache/
make            # everything, about half a minute
```

`make` needs an OpenSCAD with the Manifold backend, `uv` and Python 3. To use
another OpenSCAD build, run `make OPENSCAD=/path/to/openscad`.

To look at one part:

```sh
openscad -D 'part="fascia"' zoneflow.scad
```

Values for `part`: `assembly`, `open`, `exploded`, `section`, `board`,
`zones`, `base`, `fascia`, `cover`, `bridge`, `knob`, `button_back`,
`button_home`, `lightpipe`, `template`, and `print_` plus any part name.

The reasoning is in `docs/design/mechanical.md` and `docs/enclosure/`.

Licence: CERN-OHL-P-2.0, see `LICENSE`. The fonts keep their own licence.
