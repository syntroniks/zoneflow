# Design documents

Design documents are the source of truth for the project. If your work needs
one to change, change it in the same pull request. See
[contributing](../contributing.md).

- [System specification](system-spec.md): hardware, firmware, enclosure, documentation, licences and verification limits.
- [Prior-art survey](prior-art.md): existing open irrigation controllers, their circuits, known field failures, and what ZoneFlow takes from them.
- [Hardware toolchain](hardware-toolchain.md): how a board goes from text to a KiCad 10 project and fabrication files with no GUI, what was checked, and the known limits.

An independent design review before fabrication is tracked in
[issue #11](https://github.com/syntroniks/zoneflow/issues/11).

New design documents go in `docs/design/`. Add each one to the `nav` section
of `mkdocs.yml` and to this list.
