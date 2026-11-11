# Glossary

Terms as this manual uses them.

**24 VAC**
: The low-voltage alternating supply that sprinkler valves run on, from a transformer. ZoneFlow's board and valves all run from it.

**Class 2**
: Low voltage and power-limited: the class of transformer and circuit that sprinkler systems use. ZoneFlow is designed for Class 2 supplies only and has no mains voltage on the board. Check the transformer's marking.

**COM, valve common**
: The wire shared by every valve. Each valve has one lead on its own zone wire and one on the common. On ZoneFlow the COM terminals are fed through a relay.

**Zone**
: One output and the valve, or valves, on it: usually one area of the garden. ZoneFlow has 8 zones, or 4 on the 4-zone build.

**Master valve**
: A valve on the main supply line, upstream of the zone valves. The controller opens it 1 second before the first zone of a run and closes it 2 seconds after the last.

**Pump-start relay**
: A relay with a 24 VAC coil that switches a pump on when watering starts. It is wired to the master output (MV) in place of a master valve. Its contacts, which switch the pump's own supply, are outside ZoneFlow.

**Program**
: A schedule: start times, the days it runs, and a run time for each zone. The zones water one after another from each start time.

**Start time**
: A time of day at which a program begins. The standalone firmware allows up to four per program, the ESPHome package two.

**Day rule**
: Which days a program runs: chosen weekdays, every N days, odd dates or even dates.

**Run time**
: How long a zone waters in a program or a manual run.

**Maximum run time**
: The longest a zone may be open, counted across back-to-back runs. No schedule, app or automation can exceed it. 60 minutes by default.

**Rest time**
: How long a zone that reached its maximum run time stays closed before it may open again. 30 minutes by default.

**Seasonal adjust, seasonal adjustment**
: A percentage that scales the run times of every program that follows it, to water less in spring and more in summer without editing programs.

**Rain delay**
: A period during which scheduled starts are skipped, set by hand or by an automation. Manual runs still work.

**Rain sensor**
: A switch that changes state when it is wet. While it reads wet, programs that obey it skip their starts.

**Normally closed (NC), normally open (NO)**
: Whether a sensor's contact is closed or open in its resting (dry) state. Most rain sensors are normally closed and open when wet.

**Flow meter**
: A sensor on the water line that gives a pulse for each small volume of water. The **K-factor** is its number of pulses per litre.

**Leak alarm ("Water flows with all valves off")**
: Water counted by the flow meter while every valve is closed.

**Inrush**
: The higher current a solenoid draws for a short time when it first opens. The firmware ignores the first 500 ms of a run when it checks the current.

**Open circuit**
: A valve that draws no current: a broken wire or a burnt-out solenoid.

**Short circuit**
: A zone wire touching the common, or a shorted solenoid, so that the current is far above normal.

**Keep-alive**
: A train of pulses the firmware must keep sending for any valve to be on. When it stops, the hardware turns every valve off within about a second. See [how the fail-safe works](hardware/fail-safe.md).

**Keep-alive detector**
: The circuit that turns the keep-alive pulses into permission to drive the valves. ZoneFlow has two, built from separate parts.

**Self-test**
: The checks the firmware runs on the valve board at start-up, before and after every run, and every 10 minutes: both keep-alive detectors, the expander, a welded relay and a shorted output.

**Triac**
: The semiconductor switch that turns each valve on and off. It switches alternating current.

**Opto-triac**
: A small part that fires a triac through light, so that the triac's side and the logic side share only a light path.

**PTC, resettable fuse**
: A part whose resistance rises sharply when it carries too much current, limiting a short, and falls again when it cools.

**TVS, surge clamp**
: A diode that conducts when the voltage across it rises above its rating, to absorb a surge.

**Snubber**
: A resistor and capacitor across an output that limit the voltage spike when an inductive load like a solenoid switches off.

**Expander**
: The chip that holds the on/off bit for each valve. The processor writes it over I2C. It is held in reset whenever valve drive is not allowed, so it forgets every bit.

**Provisioning**
: Joining the controller to a WiFi network the first time, from a phone or computer.

**Setup code**
: The 10-character code that proves you are at the controller during setup (standalone firmware). The display shows it under **Menu → Setup → Network → Show setup code** after you hold the knob for 3 seconds. Also called the proof of possession in Espressif's app.

**Improv**
: The open standard the ESPHome package uses to receive WiFi settings over USB or Bluetooth LE.

**OTA**
: Over the air: a firmware update sent over the network.

**Rollback**
: Going back to the previous firmware automatically if a new one does not start properly.

**MQTT**
: A message protocol for home automation. The standalone firmware can publish its state to an MQTT broker and take commands from it, for Home Assistant or other systems.

**Qwiic**
: A small 4-pin connector for I2C devices. ZoneFlow has one, for future expansion.

**RTC**
: Real-time clock: a chip with its own crystal and a coin cell that keeps time when the board has no power.

**LCSC number**
: The part number at LCSC, the distributor that supplies JLCPCB's assembly service. Every line of the BOM has one.
