# FAQ

## Can I buy one?

No. ZoneFlow is a design on paper. No board has been made, the pre-order
checklist of the PCB layout review has not been done, and nobody sells it. See [status](status.md) and
[ordering boards](hardware/ordering.md).

## Does it work?

Nobody knows yet. The circuit has been calculated, simulated and reviewed,
and the firmware has been tested on a PC. Nothing has run on a board. The
[status page](status.md) lists what can only be answered with a prototype.

## Does it need the internet or a cloud account?

No. There is no cloud service. The schedule runs on the controller from its
own clock, and the standalone firmware has its own web interface on your
network. The internet is used only to set the clock (network time) if it is
available, and to download firmware updates if you ask for one.

## Does it keep watering when WiFi or Home Assistant is down?

Yes, in both firmwares. Schedules run on the controller. The ESPHome package
is set never to restart because WiFi or Home Assistant went away, so a run in
progress is not cut short.

## Which firmware should I choose?

If Home Assistant already runs your house and you want the controller in
your dashboards and automations, the ESPHome package. If you want a
controller that works on its own with its own display menus and web
interface, the standalone firmware. You can change later by flashing the
other one. See [choosing a firmware](firmware/index.md).

## Will it work with my valves?

It is designed for standard 24 VAC solenoid valves drawing up to 0.5 A each,
and for a master valve or a pump-start relay with a 24 VAC coil. It does not
drive DC or latching (pulse) valves. See [what you need](getting-started/what-you-need.md#valves).

## Can it switch my pump directly?

No. The MV output drives the 24 VAC coil of a pump-start relay. The relay's
contacts switch the pump, and that wiring is outside the project and may
need a qualified person. There is no mains voltage on the ZoneFlow board.

## How many zones can run at once?

The hardware allows two zones and the master valve at once. Programs water
their zones one after another. The ESPHome package runs one zone at a time
by default.

## Can I mount it outdoors?

No. The enclosure is for indoor or garage walls and is not weatherproof.

## What happens if the controller crashes while a valve is open?

The hardware turns the valve off within about a second, without the
firmware. See [how the fail-safe works](hardware/fail-safe.md).

## What happens after a power cut?

Every valve is off while there is no power. The clock keeps time on its
CR2032 cell. When power returns, the controller does not resume a run that
was in progress. A scheduled start that was missed runs late if it is no more
than 60 minutes old; older ones are skipped and recorded.

## Can I plug in USB while it is installed?

Only with care. The board's ground is one transformer wire, and a desktop
computer's USB ground is earth. In some installations that can short the
transformer through the USB cable. Use a laptop on battery, or pull the
24 VAC plug first. Read [USB and earthing](safety.md#usb-and-earthing).

## Why a colour display and a knob?

The project owner found the small screens on existing controllers too
limiting. With the display, you can run a zone, edit a program, set a rain
delay and read a fault without a phone. See [the display](guide/display.md).

## Is it compatible with OpenSprinkler?

Not now. The board uses the same kind of output circuit as OpenSprinkler,
and the specification keeps an OpenSprinkler firmware board profile open as
a later option. Nobody has written it.

## Does it support Matter?

Not now. A Matter irrigation endpoint is an option the specification keeps
open for later.

## Can I connect a weather service or soil sensor?

Neither firmware has a weather service. The Qwiic connector is there for
future sensors, but no firmware uses it yet. With the ESPHome package you can
add any ESPHome component yourself, and Home Assistant automations can set a
rain delay from any source.

## What does it cost?

Nobody sells ZoneFlow controllers; you have one made. The
[cost estimate](hardware/cost.md), from prices checked on 2026-11-11, puts a
finished 8-zone controller, assembled by JLCPCB, delivered to the United
States with duty paid and in its printed enclosure, at about 121 USD each
for a batch of 5, 71 USD for 25 and 54 USD for 100. The 4-zone build is 6 to
8 USD less. The transformer and the valve cable come on top. These are
estimates, not prices, and US tariffs move them most.

## Is it certified?

No. It has no safety, EMC or radio certification. The radio module has its
own modular approval, which covers the module only.

## Under what licence?

Hardware and enclosure CERN-OHL-P-2.0, firmware and tools Apache-2.0,
documentation CC-BY-4.0.

## How can I help?

Review the [circuit](design/hardware-design.md), the
[schematic](hardware/schematic.md) or the firmware, and open an issue with
what you find. See [contributing](contributing.md).
