# Design documents

This section is the engineering record: requirements, analysis, reviews and
decisions, written for people who build, review or change ZoneFlow. The
user manual (Getting started, the user guide, the hardware reference,
troubleshooting) is written from these documents for people who install and
use the controller. Where the two disagree, the design document is right and
the manual has a bug.

Design documents are the source of truth for the project. If your work needs
one to change, change it in the same pull request. See
[contributing](../contributing.md). Each document records its own history,
so earlier decisions it describes as replaced are kept on purpose.

- [System specification](system-spec.md): hardware, firmware, enclosure, documentation, licences and verification limits.
- [Prior-art survey](prior-art.md): existing open irrigation controllers, their circuits, known field failures, and what ZoneFlow takes from them.
- [Firmware architecture](firmware-architecture.md): standalone firmware modules, threading, safety design, interfaces for later modules, scheduler and missed-start policy, open questions for hardware.
- [Display interface](display-ui.md): the local interface on the 320x240 panel: controls, screen map, every screen as drawn by the desktop simulator, visual language, wording, what is verified and what is still weak.
- [Hardware toolchain](hardware-toolchain.md): how a board goes from text to a KiCad 10 project and fabrication files with no GUI, what was checked, and the known limits.
- [Hardware design](hardware-design.md): the rev A circuit, block by block, with part numbers, calculations, the net list, the risk register and the bring-up plan. A paper design. Nothing in it is measured.
- [Mechanical design](mechanical.md): the board outline, every mechanically fixed part position, the component height limits and the antenna rule, for the PCB layout engineer. Explains `hardware/mechanical.yaml`. A paper design. Nothing has been printed.
- [Hardware review A1](hardware-review-a1.md): the independent review of the rev A circuit before schematic capture. Findings by severity, the verdict on each departure from the specification, the keep-alive contract with the firmware, what was verified from data sheets and what was not.
- [Hardware review A2](hardware-review-a2.md): the second review, of the fail-safe circuits only. A single-fault table rebuilt from the netlist by fault injection, tolerance corners of both keep-alive detectors, the regulator over-voltage protection, and the output stage after the change of triac.
- [Hardware answers A2](hardware-answers-a2.md): the hardware answers to the nine questions the firmware raised on the self-test of review A2, the decision on a shorted 3.3 V regulator with the detector it added, and the list of what the firmware must change.
- [Web UI contract](../firmware/web-ui-contract.md): what the standalone firmware's web interface may rely on from the REST API and event stream.

The independent design review before fabrication is tracked in
[issue #11](https://github.com/syntroniks/zoneflow/issues/11).

New design documents go in `docs/design/`. Add each one to the `nav` section
of `mkdocs.yml` and to this list.
