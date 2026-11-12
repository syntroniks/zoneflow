---
title: Pre-order review walkthrough
description: A guided check of the revision A board, one step at a time, for the person who decides whether to order it.
hide:
  - navigation
  - toc
no_web_fonts: true
---

<p class="zf-crumbs" markdown><a href="../">Hardware reference</a> / <a href="../ordering/">Ordering boards</a> / this page.
Also: <a href="../layout/">PCB layout</a>, <a href="../../design/hardware-layout-review/">the layout review</a>,
<a href="bench-sheet/">the bench sheet</a>.</p>

# Pre-order review walkthrough

!!! warning "No board has been made"
    This page helps a person check design files. Nothing on it is a
    measurement, and passing every step does not show that the board works.
    **Do not order boards before the owner has gone through it**: see
    [ordering boards](../ordering.md).

<!-- zf:review-walkthrough -->

<noscript>
<p><strong>This page works without JavaScript as one long document.</strong>
With JavaScript it shows one step at a time, remembers your answers in this
browser and writes the report.</p>
</noscript>

## How this page is made

The steps and findings are text files in
[`docs/hardware/review/data/`](https://github.com/syntroniks/zoneflow/tree/main/docs/hardware/review/data).
The pictures are drawn from `hardware/zoneflow-a/zoneflow-a.kicad_pcb` by
[`tools/reviewwalk`](https://github.com/syntroniks/zoneflow/tree/main/tools/reviewwalk)
when the board is built, with KiCad's own conversion of pads, tracks, zones
and text to outlines. The build of this site fails if a step names a part or
a net that is no longer on the board, or if the pictures were drawn from
another board file than the one in the repository.

Each shape in a picture carries the net, the part and the pad number that the
board file gives it. When you enlarge a picture and select a shape, the page
reads those and shows them. It works nothing out by itself, so it cannot name
a pad that the board does not have. The layer switches hide and show what the
picture already holds.

## What happens to your answers

They stay in your browser. The page sends nothing. Two things leave it, and
only when you take them out: the report, and the hand-off file for a coding
agent. The hand-off file follows
[a schema](handoff.schema.json). `reviewwalk record` checks a file against the
schema and the board and writes it into
[the review records](../../design/reviews/index.md) of the repository.

The pictures show the board file. They are not KiCad's window, and they do
not show solder mask, paste or the inner structure of a via. Where a step
depends on something a picture cannot show, the step says so.
