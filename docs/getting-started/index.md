# Getting started

!!! warning "Written from the design; not yet verified on hardware"
    No ZoneFlow board has been built. These pages describe how the board,
    the enclosure and the firmware are designed to be installed and used.
    Expect corrections after the first prototype. Notes marked "to be
    completed" say what is still missing and which issue unblocks it.

This is the installation manual. It is written for someone who can wire a
sprinkler timer and use a multimeter. Do the steps in this order:

1. [Safety](../safety.md): read it first, in particular the USB and earthing rules.
2. [What you need](what-you-need.md): transformer, cable, valves, tools.
3. [Mounting](mounting.md): put the enclosure on the wall with the drilling template.
4. [Wiring](wiring.md): terminal by terminal, with a diagram.
5. [Installing firmware](../firmware/web-installer.md): only if the board has no firmware yet. No firmware release has been published, so for now this step means [building it yourself](../firmware/web-installer.md#until-the-first-release-build-it-yourself).
6. [First power-up](first-power-up.md): what the display and the lights should show.
7. [Joining your network](provisioning.md): standalone firmware or ESPHome.
8. [First schedule](first-schedule.md): from the display, the web interface or Home Assistant.
9. [Testing each zone](testing-zones.md): run each valve once and record its current.

If something does not behave as described, see [troubleshooting](../troubleshooting.md).

Before step 1, decide which firmware you will run:
[choosing a firmware](../firmware/index.md). The wiring and mounting are the
same for both.
