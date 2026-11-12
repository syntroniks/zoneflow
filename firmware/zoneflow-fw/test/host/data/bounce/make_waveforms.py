#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Writes the button waveforms of this directory. Standard library only.

    python3 make_waveforms.py

THESE ARE NOT RECORDINGS. No ZoneFlow board exists, so no switch on it has
been measured. Each file is made up to a stated limit: how long the contact
chatters when it closes and when it opens, and how long it is closed in
between. The chatter is a burst of closures and openings of 20 to 900 us, from
a fixed seed, so the files are the same on every run.

The format is the one a logic analyser on the pin would give, so that a
recording from the first board can be put beside these files as it is:

    # comment
    time_us,pin        pin is the level at the processor pin from that time on:
    0,1                1 = high, contact open; 0 = low, contact closed

Add a recording to kWaveforms in ../../ui_debounce_test.cpp with the events it
must give.
"""

import random
from pathlib import Path

HERE = Path(__file__).parent


def chatter(rng, start_us, length_us, ends_closed):
    """Edges of a burst that starts at start_us, lasts at most length_us and ends in the given state."""
    edges, t, closed = [], start_us, not ends_closed
    end = start_us + length_us
    # the burst starts with the first change of state
    closed = not closed
    edges.append((t, closed))
    while True:
        t += rng.randint(20, 900)
        if t >= end:
            break
        closed = not closed
        edges.append((t, closed))
    if closed != ends_closed:
        edges.append((end, ends_closed))
    return edges


def press(rng, t0_us, closing_us, closed_us, opening_us):
    """One actuation: chatter while closing, closed, chatter while opening. Returns its edges and its end."""
    edges = chatter(rng, t0_us, closing_us, True) if closing_us else [(t0_us, True)]
    release = t0_us + closing_us + closed_us
    edges += chatter(rng, release, opening_us, False) if opening_us else [(release, False)]
    return edges, release + opening_us


def write(name, text, edges, tail_us=400_000):
    lines = [f"# {line}".rstrip() for line in text.strip().split("\n")]
    lines += ["# Made up by make_waveforms.py to the limits above. Not a recording.", "time_us,pin", "0,1"]
    last = None
    for t, closed in edges:
        if closed != last:
            lines.append(f"{t},{0 if closed else 1}")
            last = closed
    lines.append(f"{edges[-1][0] + tail_us},1")
    (HERE / name).write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    rng = random.Random(20261112)
    lead = 100_000

    e, _ = press(rng, lead, 10_000, 20_000, 10_000)
    write("short-press-30ms.csv", """
A short press. The contact chatters for 10 ms while it closes, is closed for
20 ms and chatters for 10 ms while it opens: 30 ms from the first contact to
the start of the release.""", e)

    e, _ = press(rng, lead, 9_000, 12_000, 9_000)
    write("short-press-30ms-all-in.csv", """
The same press read the hard way: 30 ms from the first contact to the last,
with both bursts of chatter inside it. 9 ms closing, 12 ms closed, 9 ms
opening. 12 ms is what two samples in a row need when each may be 1 ms late.""", e)

    e, _ = press(rng, lead, 3_000, 150_000, 3_000)
    write("click-chatter-3ms.csv", """
An ordinary click of 150 ms with 3 ms of chatter at each end.""", e)

    e, _ = press(rng, lead, 14_000, 200_000, 14_000)
    write("click-chatter-14ms.csv", """
A worn switch: 14 ms of chatter at each end of a 200 ms press. The decoder's
margin ends at 17 ms.""", e)

    e, _ = press(rng, lead, 10_000, 1_000_000, 10_000)
    write("hold-1s.csv", """
Held for one second, with 10 ms of chatter at each end. Longer than the
0.8 s of Manual/Home and the first repeats of Back.""", e)

    e1, end = press(rng, lead, 8_000, 60_000, 8_000)
    e2, _ = press(rng, end + 70_000, 8_000, 60_000, 8_000)
    write("double-click.csv", """
Two presses of 60 ms, 70 ms apart, each with 8 ms of chatter at both ends.""", e1 + e2)

    spikes, t = [], lead
    for _ in range(60):
        t += rng.randint(6_300, 23_000)
        spikes += [(t, True), (t + rng.randint(5, 300), False)]
    write("spikes-no-press.csv", """
Nobody presses. The pin dips low 60 times, for 5 to 300 us each, never less
than 6.3 ms apart: interference on a line that has no capacitor any more.
What a real board picks up is not known.""", spikes)


if __name__ == "__main__":
    main()
