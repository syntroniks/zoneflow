# Design documents

Design documents are the source of truth for the project. If your work needs
one to change, change it in the same pull request. See
[contributing](../contributing.md).

- [System specification](system-spec.md): hardware, firmware, enclosure, documentation, licences and verification limits.
- [Prior-art survey](prior-art.md): existing open irrigation controllers, their circuits, known field failures, and what ZoneFlow takes from them.
- [Firmware architecture](firmware-architecture.md): standalone firmware modules, threading, safety design, interfaces for later modules, scheduler and missed-start policy, open questions for hardware.
- [Display interface](display-ui.md): the local interface on the 320x240 panel: controls, screen map, every screen as drawn by the desktop simulator, visual language, wording, what is verified and what is still weak.
- [Hardware toolchain](hardware-toolchain.md): how a board goes from text to a KiCad 10 project and fabrication files with no GUI, what was checked, and the known limits.

An independent design review before fabrication is tracked in
[issue #11](https://github.com/syntroniks/zoneflow/issues/11).

New design documents go in `docs/design/`. Add each one to the `nav` section
of `mkdocs.yml` and to this list.
