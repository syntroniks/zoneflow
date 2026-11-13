# What a controller costs

!!! warning "An estimate, dated 2026-11-12. Not a quote."
    No ZoneFlow board has been ordered, built or tested. Every number on this
    page is calculated from part prices read on **2026-11-12**, from board
    and freight quotes and price lists read on 2026-11-11,
    and from the assumptions named in the [last section](#assumptions). The
    assembler's own quote needs a login and uploaded files; it has not been
    asked for. Tariffs changed several times in 2026 and will change again.
    The tables are written by `tools/cost` from dated data files, so they can
    be redone with new prices in a minute: see [redoing it](#redoing-it).

This is the cost of a finished controller delivered to a builder in the
United States: the board assembled by JLCPCB, the parts it does not fit, the
coin cell, the printed enclosure, freight and US duty. The transformer and
the valve cable are listed [apart](#what-the-user-buys-besides), because a
user buys them whichever controller they choose.

## The answer

<!-- zfcost:begin headline -->

| USD per controller | 5 units | 10 units | 25 units | 100 units |
|---|---|---|---|---|
| 8-zone, board landed | 107.72 | 79.99 | 63.16 | 47.61 |
| 8-zone, with enclosure | 117.60 | 87.13 | 69.00 | 52.79 |
| 4-zone, board landed | 100.95 | 72.76 | 57.03 | 42.29 |
| 4-zone, with enclosure | 110.82 | 79.90 | 62.87 | 47.48 |

<!-- zfcost:end -->

"Board landed" is the assembled board with its plugs, display panel, cell and
a spare fuse, at the builder's door with duty paid. "With enclosure" adds the
filament, inserts, screws and label. Neither includes the builder's time
([hours are listed](#time) instead), US sales tax, tools, or any
certification.

What a whole batch costs, which is what has to be paid up front:

<!-- zfcost:begin orders -->

| USD for the whole batch | 5 units | 10 units | 25 units | 100 units |
|---|---|---|---|---|
| 8-zone | 587.98 | 871.27 | 1,724.96 | 5,278.68 |
| 4-zone | 554.12 | 799.05 | 1,571.66 | 4,747.59 |

<!-- zfcost:end -->

### Where the money goes

<!-- zfcost:begin breakdown-8 -->

| 8-zone, USD per controller | 5 units | 10 units | 25 units | 100 units |
|---|---|---|---|---|
| Parts fitted by JLCPCB | 37.21 | 33.47 | 31.04 | 24.93 |
| Parts bought loose: headers, C1, K1, plugs, display, spare fuse | 6.45 | 5.74 | 5.57 | 4.43 |
| Coin cell (US distributor) | 2.74 | 1.37 | 0.61 | 0.37 |
| Bare board | 6.56 | 4.05 | 2.55 | 1.65 |
| Assembly fees | 23.02 | 12.18 | 5.68 | 2.94 |
| &nbsp;&nbsp;of which for extended parts | 19.65 | 9.82 | 3.93 | 0.58 |
| Freight to the US | 6.10 | 3.77 | 2.01 | 1.40 |
| US duty | 25.63 | 19.40 | 15.70 | 11.88 |
| Enclosure materials | 9.87 | 7.14 | 5.83 | 5.18 |
| **Total per controller** | **117.60** | **87.13** | **69.00** | **52.79** |
| Assembly service | economic 5 | economic 10 | economic 25 | standard 100 |
| Boards ordered | 5 | 10 | 25 | 100 |

<!-- zfcost:end -->

<!-- zfcost:begin breakdown-4 -->

| 4-zone, USD per controller | 5 units | 10 units | 25 units | 100 units |
|---|---|---|---|---|
| Parts fitted by JLCPCB | 32.73 | 28.66 | 27.04 | 21.42 |
| Parts bought loose: headers, C1, K1, plugs, display, spare fuse | 6.09 | 5.37 | 5.21 | 4.15 |
| Coin cell (US distributor) | 2.74 | 1.37 | 0.61 | 0.37 |
| Bare board | 6.56 | 4.05 | 2.55 | 1.65 |
| Assembly fees | 22.85 | 12.01 | 5.50 | 2.80 |
| &nbsp;&nbsp;of which for extended parts | 19.65 | 9.82 | 3.93 | 0.58 |
| Freight to the US | 6.10 | 3.77 | 2.01 | 1.40 |
| US duty | 23.88 | 17.53 | 14.11 | 10.51 |
| Enclosure materials | 9.87 | 7.14 | 5.83 | 5.18 |
| **Total per controller** | **110.82** | **79.90** | **62.87** | **47.48** |
| Assembly service | economic 5 | economic 10 | economic 25 | standard 100 |
| Boards ordered | 5 | 10 | 25 | 100 |

<!-- zfcost:end -->

### Reading the tables

- **At 5 units, fixed charges per order are the largest share.** The board's
  engineering fee, the assembly setup and stencil, and above all the
  extended-part fees are charged once per order whatever its size:
  132.95 USD. With the duty on them they are about 36 USD per controller,
  31 % of the 118 USD. Parts are 46 USD, 39 %.
- **At 100 units, parts and duty dominate.** Parts are 30 of the 53 USD and
  duty 12. Board and assembly fees together have fallen to 4.59 USD.
- **Extended-part fees are the largest single fixed charge.** JLCPCB's
  Economic assembly charges 3.07 USD for each extended part type that is not
  "preferred"; this board has 32 such types, so every order pays 98.24 USD
  before it fits a part. On five boards that is 20 USD each, 27 USD with
  duty, 23 % of the total. It had 40 as designed; two passes replaced eight:
  [fewer extended part types](#fewer-extended-part-types). Standard assembly, which the model uses
  above 50 boards, charges 1.53 USD for every type, basic or extended.

<!-- zfcost:begin types -->

| Build | Parts placed | Joints | Part types | Basic | Preferred extended | Extended, fee charged |
|---|---|---|---|---|---|---|
| 8-zone | 307 | 840 | 88 | 50 | 6 | 32 |
| 4-zone | 262 | 730 | 88 | 50 | 6 | 32 |

<!-- zfcost:end -->

<!-- zfcost:begin extfee -->

| Units | Service | Extended-part fees, USD per batch | With duty, USD per controller | Share of the total |
|---|---|---|---|---|
| 5 | economic | 98.24 | 26.52 | 23 % |
| 10 | economic | 98.24 | 13.26 | 15 % |
| 25 | economic | 98.24 | 5.30 | 8 % |
| 100 | standard | 58.14 | 0.78 | 1 % |

<!-- zfcost:end -->

- **US duty is the second-largest line at every quantity.** At the 35 % that
  JLCPCB says it collects on boards, it adds 35 % to everything made in
  China, fees included: 26 USD of the 118 at 5 units, 12 of the 53 at 100.
- **The 4-zone build saves only 5.31 to 7.22 USD.** It drops 45 parts, but every
  part type stays (zones 1 to 4 use the same types as 5 to 8), so it pays
  the same per-order fees and the same board.
- **Batch size is the biggest lever a builder controls.** Five boards cost
  49 USD more each than 25 boards.

## Components

**Method.** Every line of
[`hardware/bom-preliminary.csv`](https://github.com/syntroniks/zoneflow/blob/main/hardware/bom-preliminary.csv)
was priced again on 2026-11-12 from the JLCPCB parts search: stock, price
breaks, basic or extended, preferred or not. The snapshot is
`tools/cost/data/jlcpcb-parts.csv`. For a part JLCPCB fits, the number
bought is the number fitted plus JLCPCB's attrition quantity for that part,
and at least its minimum assembly quantity; the price break is the one that
total reaches. Both quantities come from the parts search; reading them as
attrition and minimum is an [assumption](#assumptions). They add 10.76 USD to
a batch of five (2.15 per controller) and 8.54 USD to a batch of a hundred
(under 0.09 per controller): the minimums are 20 or fewer, and the parts they
apply to cost a cent or less.

Which parts JLCPCB fits comes from the [layout](layout.md) as it stands
after its [review](../design/hardware-layout-review.md): its placement files
hold 307 parts for the 8-zone build and 262 for the 4-zone build, all SMT on
the top side. The model checks that the BOM gives the same counts. The six
through-hole parts (four terminal headers, the bulk
capacitor C1 and the relay K1) are not in the placement files, so they are
bought loose with the order, as are the four plugs, the display panel and
one spare 3 A fuse (0452003.MRL, the replacement named in the BOM). They are
priced at the JLCPCB parts price for the quantity and travel in the same box.

The CR2032 cell is not stocked by JLCPCB. It is priced from Digi-Key
(Panasonic CR2032, P189-ND, 0.2855 USD at 20, the smallest break listed), so
a batch of five still buys twenty.

The most expensive lines, 8-zone:

<!-- zfcost:begin toplines -->

| Part | USD per controller at 5 | USD per controller at 100 |
|---|---|---|
| ESP32-S3-WROOM-1-N16R8 | 5.38 | 3.78 |
| T810-600B-TR | 4.71 | 3.75 |
| HS20HS072RX | 3.58 | 2.41 |
| 0154003.DRT | 3.06 | 2.38 |
| MOC3023SR2M | 3.75 | 2.12 |
| EC11J1525402 | 3.06 | 1.80 |
| LM5164DDAR | 2.56 | 1.67 |
| BSMD2920-100-60V | 2.19 | 1.47 |
| TCA9539PWR | 1.22 | 0.80 |
| KF2EDGK-3.81-7P | 0.75 | 0.55 |

<!-- zfcost:end -->

The nine output channels carry two of the most expensive families of the
board: nine triacs and nine opto-triacs. The fourteen 48 V TVS diodes were
the third until 2026-11-12: the part of the second maker costs 0.04 USD
where the first cost 0.11 to 0.13 USD. The module rose by 0.43 USD between
2026-11-11 and 2026-11-12.

**Stock.** No line was out of stock or below what 100 eight-zone controllers
need. The thinnest, as the number of 100-unit batches JLCPCB had in stock:

<!-- zfcost:begin stock -->

| Part | LCSC | Library | JLCPCB stock | Needed for 100 | Batches in stock | Flag |
|---|---|---|---|---|---|---|
| SMBJ48CA | C19077591 | extended | 2,730 | 1,402 | 1.9 | thin: under 5 batches |
| T810-600B-TR | C155588 | extended | 2,882 | 900 | 3.2 | thin: under 5 batches |
| HS20HS072RX | C5329582 | extended | 334 | 100 | 3.3 | thin: under 5 batches |
| KF2EDGK-3.81-7P | C440863 | extended | 1,522 | 200 | 7.6 |  |
| KF2EDGR-3.81-7P | C441187 | extended | 1,599 | 200 | 8.0 |  |
| LM5164DDAR | C477928 | extended | 852 | 100 | 8.5 |  |
| TLV803EA29DBZR | C1852114 | extended | 1,206 | 100 | 12.1 |  |
| 0154003.DRT | C206913 | extended | 2,504 | 200 | 12.5 |  |
| EC11J1525402 | C209762 | extended | 1,481 | 100 | 14.8 |  |
| 1812L020/60DR | C315900 | extended | 1,540 | 100 | 15.4 |  |

<!-- zfcost:end -->

The display panel had 460 on 2026-11-10, 413 a day later and 334 the day
after that. The buck converter LM5164DDAR had 1,651 on 2026-11-11 and 852 on
2026-11-12, and the Qwiic connector SM04B-SRSS-TB fell from 75,739 to 7,233
in the same day.

## Board fabrication

From JLCPCB's online quote page, which needs no login, on 2026-11-11: FR-4,
4 layers, 114 x 110 mm, 1.6 mm, 1 oz outer and 0.5 oz inner copper, green,
single boards, default build time, every other option at its default. That
is the stack-up the layout asks for; it needs no special option
([hardware layout](../design/hardware-layout.md), section 2). The calculator gave the same
price for HASL with lead, lead-free HASL and ENIG; the model assumes
lead-free HASL.

<!-- zfcost:begin pcbfab -->

| Boards | USD, smallest hole 0.3 mm (this layout) | USD per board, 0.3 mm | USD, smallest hole 0.2 mm |
|---|---|---|---|
| 5 | 32.80 | 6.56 | 71.89 |
| 10 | 40.50 | 4.05 | 82.28 |
| 20 | 56.00 | 2.80 | 103.17 |
| 25 | 63.70 | 2.55 | 113.57 |
| 30 | 71.40 | 2.38 | 123.98 |
| 50 | 102.40 | 2.05 | 165.76 |
| 75 | 131.50 | 1.75 | 208.35 |
| 100 | 165.40 | 1.65 | 255.73 |
| 150 | 233.40 | 1.56 | 350.68 |
| 200 | 301.20 | 1.51 | 445.45 |

<!-- zfcost:end -->

**A 0.2 mm hole costs more, so the layout has none.** The layout first
drilled twelve vias in the ground pad of the module U4 at 0.2 mm and recorded
that as the smallest hole without a surcharge. On 2026-11-11, choosing
"0.2mm/(0.3/0.35mm)" in the quote page added 18.10 USD for the hole size and
switched on TG155 material and the 4-wire Kelvin test, 39 USD on five boards
and 90 USD on a hundred. The
[layout review](../design/hardware-layout-review.md) drilled those vias
0.3 mm (its finding L9), and the design rules now fail a board with a smaller
hole. The model uses the 0.3 mm column.

The board quantity is rounded up to a quantity the quote page offers; the
extra bare boards are spares.

## Assembly

From JLCPCB's price page, last updated 2026-10-22, and its capability page.
Economic assembly takes 2 to 50 boards with parts on one side; Standard
takes 2 to 80,000 on one or both sides. The model uses Economic up to 50
and Standard above. For 100 eight-zone boards, one Standard order comes to
52.79 USD per controller and two Economic orders of 50 to 59.28 USD, because
each Economic order pays the extended-part fees and the board's engineering
fee again.

| Fee | Economic | Standard |
|---|---|---|
| Setup, per order | 8.18 | 25.56 one side |
| Stencil, per order | 1.53 | 8.21 one side |
| Solder joint | 0.0016 | 0.0016 to 50,000 joints, 0.0013 to 100,000, 0.0012 above |
| Feeder loading, per part type | 3.07 for each extended type; preferred extended parts exempt | 1.53 for every type |
| Confirm parts placement | 0.45 | 0.45 |
| Packing | free | 0.50 + 0.00003 per cm² of board |
| Manual (through-hole) assembly | 0.0164 per joint up to 10,000, plus 3.58 hand-soldering labour per order | the same, plus fixtures (8.21 each, 2 to 5 for these quantities) |

Joints are counted from the layout: 840 soldered pins on the 8-zone board and
730 on the 4-zone board, counting each pin once.

**Through-hole parts.** The layout plans to solder C1 and K1 by hand from
the top and leaves all six through-hole parts out of the placement files. The
base case keeps that plan. JLCPCB could fit them by hand for its manual
assembly fee, but C1 and K1 sit on the bottom side, so the order becomes a
double-sided Standard order. Nobody has asked JLCPCB whether it would. Both
options, by the published rules:

<!-- zfcost:begin tht -->

| 8-zone, USD per controller (builder minutes per controller) | 5 units | 25 units | 100 units |
|---|---|---|---|
| Builder solders them | 117.60 (79 min) | 69.00 (60 min) | 52.79 (56 min) |
| JLCPCB fits them (Standard service) | 140.06 (69 min) | 73.96 (50 min) | 53.97 (46 min) |

<!-- zfcost:end -->

**What the published rules do not settle.** The exact assembly price needs a
login and the uploaded files, so these are the published rules applied to the
board, not a quote. The open points: whether the module's ground pad triggers
JLCPCB's X-ray fee (1.64 down to 0.08 USD per part by quantity; not in the
totals); whether Standard also charges the double-sided setup (51.12) when
the bottom side has only through-hole parts; whether the solder-joint count
JLCPCB makes from the files equals the 840 counted here; and how the
"single-board assembly surcharge" and "handling fee" of the price page apply.
A plausible range at 5 boards is the base case plus up to 15 USD per board
for these; at 100 boards, plus up to 2 USD.

## Enclosure

Materials per controller, from the model volumes in
[printing](../enclosure/printing.md) (182 g of PETG, which reads high because
it treats the parts as solid) and the [assembly](../enclosure/assembly.md)
parts list:

| Item | Quantity per controller | Price used | Source |
|---|---|---|---|
| PETG filament | 182 g + 10 % waste, bought as whole 1 kg spools | 18.99 USD per kg | Polymaker PETG, shop.polymaker.com |
| M3 x 5.7 heat-set inserts | 7, bought in packs of 100 | 8.34 USD per 100 | ruthex at 3djake.com, without VAT and delivery |
| M3 machine screws | 7 | 0.08 USD each | Assumption |
| Address label 28 x 89 mm | 1 | 0.05 USD | Assumption |
| Knob, button caps, light-pipe comb | printed | in the filament | |

That is 9.87 USD per controller at 5 (two spools for five) and 5.18 at 100.
The knob, the light pipes and the buttons are printed, so they cost
filament only; the clear comb weighs 1 g.

### Time

Not converted to money. Printer hours are the model volume at 10 mm³/s (a
current fast printer) and at 5 mm³/s, from the printing page. Builder minutes
are [assumptions](#assumptions): 10 minutes to solder the six through-hole
parts, 25 to fit inserts, light pipes, display, board and fascia, 20 to flash
and check a board, and 2 hours per batch to order, check the placement
preview and unpack.

<!-- zfcost:begin time -->

| Units | Printer hours, fast printer | Printer hours, slow printer | Builder hours | Builder minutes per controller |
|---|---|---|---|---|
| 5 | 22 | 42 | 6.6 | 79 |
| 10 | 43 | 83 | 11.2 | 67 |
| 25 | 108 | 208 | 24.9 | 60 |
| 100 | 430 | 830 | 93.7 | 56 |

<!-- zfcost:end -->

At 100 units the printing is the bottleneck: 430 hours is about 18 days of
one fast printer running without a stop. To put a price on the builder's
time, set `labour_usd_per_hour`.

## Shipping, duty and taxes

### United States, the base case

**Freight** is JLCPCB's UPS Express Saver (DDP) price from the quote page on
2026-11-11, by weight: 26.60 USD up to 0.5 kg, 31.69 at 1 kg, 67.64 at
5 kg, 155.77 at 15 kg, interpolated between. The weight is the bare boards
(42 g each, from the quote page) plus assumed masses for the fitted parts,
the loose parts and the packing. A batch of five is about 0.9 kg, a hundred
about 13 kg.

**Duty.** These are the facts the model rests on, as found on 2026-11-11:

- **De minimis is gone.** Duty-free treatment of low-value parcels is
  suspended for all countries (91 FR 17839, 2026-04-09, as summarised in
  CRS fact sheet R49481 of 2026-10-08), so a 50 USD order pays duty like a
  5,000 USD one.
- **IEEPA tariffs** were struck down by the Supreme Court on 2026-02-20. The
  Section 122 10 % surcharge that followed expired on 2026-07-24 (same CRS
  fact sheet).
- **Section 301** duty of 25 % on Chinese goods still applies to boards.
- **JLCPCB's own FAQ** (last updated 2026-10-29) lists a "122 tariff" of
  12.5 % from 2026-07-24 that it collects at 10 %, and a total of "about
  35 %~92.5 %" by product. The CRS fact sheet says Section 122 expired that
  day and lists a separate July 2026 Section 301 action of 10 to 12.5 % on
  60 economies without saying whether China is one. The two sources do not
  agree on the name of the second layer; both put a layer of 10 to 12.5 % on
  top of the 25 %.

The model applies **35 %** to everything bought from China (parts, board,
assembly fees), not to freight, because US duty is assessed on the goods'
value. JLCPCB ships DDP: it collects the duty at checkout and the parcel
arrives without a customs bill. The sensitivity table below shows 0, 25 and
60 %. Sales tax is not included; it depends on the state.

### European Union, a note

Not modelled. Since 2026-07-01 the EU no longer waives customs duty on
parcels worth up to 150 EUR; such parcels pay a flat 3 EUR per item until
2028-07-01 (Commission guidance of 2026-06-08 on the temporary flat fee).
Above 150 EUR the ordinary duty for the tariff heading applies. Import VAT,
19 to 27 % by country, applies to all of it, freight included. A builder in
the EU would replace the 35 % US duty with VAT at their country's rate plus
the duty for the heading, which for assembled control boards is low.

## What the user buys besides

Every 24 VAC controller needs these, so they are not in the controller cost.

<!-- zfcost:begin enduser -->

| Item | USD | Source |
|---|---|---|
| 24 VAC transformer | 32.18 to 39.99 | Hunter 526500 plug-in, 24 VAC 1 A (about 24 VA): 32.18 USD at sprinklersupplystore.com, 35.24 and 39.99 elsewhere (search summary, 2026-11-11) |
| Valve cable, 250 ft roll | 142.66 to 231.67 | 18 AWG 8-conductor direct-burial irrigation wire, 250 ft: 142.66 (sale) to 231.67 USD (sprinklerwarehouse.com, sprinklersupplystore.com, supplystop.com 159.00; search summary, 2026-11-11) |

<!-- zfcost:end -->

The transformer must meet the requirements on
[what you need](../getting-started/what-you-need.md#transformer): Class 2,
20 to 75 VA. A replacement of an existing controller usually reuses the
transformer and the cable already installed.

## One-time costs

### Per order and for a first prototype

<!-- zfcost:begin oneoff -->

| Item | USD | When |
|---|---|---|
| Economic assembly: setup, stencil, placement confirmation | 10.16 | Every order |
| Standard assembly: setup, stencil, placement confirmation | 34.22 | Every order |
| Economic extended-part fees, 8-zone | 98.24 | Every order |
| Bare boards, 5 pieces, smallest hole 0.3 mm | 32.80 | Every order; 25.00 of it is the engineering fee |
| Prototype run: 5 eight-zone controllers, landed, with enclosures | 587.98 | Once |

<!-- zfcost:end -->

JLCPCB charges the setup, stencil and part-type fees with every order, so
they recur; they are "one-time" only for a builder who orders once.

### Bench equipment for bring-up

What the [bring-up test plan](../design/hardware-design.md#21-bring-up-test-plan)
uses, for someone who owns none of it. Only the oscilloscope price was looked
up; the others are assumptions.

| Item | Why the plan needs it | USD |
|---|---|---|
| Four-channel oscilloscope, 70 MHz or more | Keep-alive timing, relay timing, triac turn-off (steps 4, 10 to 12) | 373 to 580 (Rigol DHO804 at US dealers, search summary 2026-11-11) |
| Bench DC supply with current limit | USB-only start at 200 mA (step 2) | 50 to 150, assumed |
| Isolated variable 0 to 32 VAC source: a variac ahead of a 24 VAC transformer | First AC power, 32 VAC soak (step 8) | 80 to 250, assumed |
| True-RMS multimeter | Every step | 30 to 150, assumed |
| 47 ohm 25 W resistors, a 24 VAC valve, a 40 VA transformer | Load tests and the short-circuit test (steps 11 to 13) | 60 to 120, assumed |
| Soldering iron or station | Through-hole parts, inserts | 50 to 150, assumed |
| **Total** | | **about 640 to 1,400** |

A battery-powered laptop is assumed for step 9, and a household freezer for
the -20 °C keep-alive test.

### Radio and safety approvals

!!! note "Not legal advice"
    This is what the sources below say, read by a non-lawyer. Anyone who
    plans to sell controllers should ask a compliance lab or a lawyer.

**Building for yourself in the US.** FCC rule 47 CFR 15.23 exempts
home-built devices from equipment authorization if they are not marketed,
not built from a kit, and built in quantities of five or less for personal
use. The builder is still expected to meet the technical limits "to the
greatest extent practicable", and must not cause harmful interference
(15.5).

**Selling in the US.** The ESP32-S3-WROOM-1 module holds its own FCC grant
(FCC ID 2AC7Z-ESPS3WROOM1, Part 15 Subpart C). A modular grant covers the
module's transmitter when it is installed as its grant allows. It does
**not** cover the rest of the controller: the host is an unintentional
radiator under Part 15 Subpart B, and its maker must show compliance, usually
by a Supplier's Declaration of Conformity based on emissions tests of the
finished product with the module installed (FCC KDB 996369, as quoted by
module makers' integration guides). The host must also carry the module's
FCC ID on its label. A kit counts as marketed, and selling boards that the
buyer finishes does not escape this. No price is given here for the tests;
none was quoted.

**Selling in the EU.** The finished controller is radio equipment under the
Radio Equipment Directive, and its maker does the conformity assessment and
signs the declaration of conformity. The module's CE documentation (Espressif
lists a CE certificate for the ESP32-S3-WROOM-1 dated 2025-12-18) helps with
the radio part but does not replace the assessment of the product: EMC,
safety (the RED's safety requirement has no lower voltage limit, unlike the
Low Voltage Directive) and, since the RED delegated act (EU) 2022/30 applies
from 2025-08-01 to internet-connected radio equipment, cybersecurity against
EN 18031-1. RoHS applies too, which is one reason the model assumes
lead-free HASL.

## Five parts fewer: debounce in firmware

On 2026-11-12 the owner decided to debounce the encoder switch and the two
buttons in firmware. Five 0603 parts left both builds: the 10 kohm resistors
R71 and R73 and the 100 nF capacitors C41, C42 and C43
([hardware design, section 28](../design/hardware-design.md#28-what-changed-with-the-firmware-debounce)).
Both part types are basic parts and are still used elsewhere on the board, so
no part type and no feeder fee went with them. What went is five placements,
ten solder joints, the parts, and the duty on all of it.

| Per controller, with enclosure | 5 | 10 | 25 | 100 |
|---|---|---|---|---|
| Saved, 8-zone, USD | 0.05 | 0.05 | 0.05 | 0.04 |
| Saved, 4-zone, USD | 0.05 | 0.05 | 0.05 | 0.04 |

These are the batch totals this page had on 2026-11-11 before and after the
decision, with the prices of that day, divided by the batch size: 606.38 to
606.11 USD for five 8-zone controllers, 5,375.55 to 5,371.33 USD for a
hundred. The decision was not made for the money.

## Fewer extended part types

Revision A as designed had 40 part types that pay JLCPCB's Economic feeder
fee. Two passes over that list have taken eight types off it, and a package
of seven more is drawn up for revision B.

- **The first pass, 2026-11-11**, allowed only a basic or preferred part in
  the same footprint that changes nothing in the circuit: 40 to 35.
- **The second pass, 2026-11-12**, followed the owner's instruction to
  "focus on fewer distinct extended part types" and was allowed to change the
  design. What keeps the footprint and the copper and passes a comparison
  figure by figure is done: 35 to 32. What needs new copper, the enclosure, a
  firmware change or a bench is the revision B package: 32 to 25.

**The fee rule, read again on 2026-11-12.** JLCPCB's price page (last updated
2026-10-22) charges "Feeder loading" of 3.07 USD per extended type on an
Economic order and 1.53 USD per type, basic or extended, on a Standard order.
Its FAQ page (last updated 2026-09-09) exempts preferred extended parts from
the Economic fee. The fee is per part type and per order, not per side or
per board. That is what this page used on 2026-11-11, and nothing below
rests on anything else.

**The lists, read again.** The parts search returned 351 basic and 1,235
preferred extended parts on 2026-11-12, the same counts as the day before.
Every candidate below was matched against those two lists by package and
value, not by keyword. Every one of the 35 types was also looked up again:
none had changed its class.

<!-- zfcost:begin consolidation -->

| Build | Stage | Fee-carrying types | Their fees, USD per Economic order | USD per controller at 5 | USD per controller at 10 | USD per controller at 25 | USD per controller at 100 |
|---|---|---|---|---|---|---|---|
| 8-zone | Revision A as designed | 40 | 122.80 | 125.99 | 92.37 | 72.02 | 54.29 |
| 8-zone | After the first pass, 2026-11-11 | 35 | 107.45 | 121.80 | 90.26 | 71.15 | 54.30 |
| 8-zone | After the second pass: the board as it is | 32 | 98.24 | 117.60 | 87.13 | 69.00 | 52.79 |
| 8-zone | After the revision B package | 25 | 76.75 | 111.22 | 83.85 | 67.24 | 52.29 |
| 4-zone | Revision A as designed | 40 | 122.80 | 118.74 | 84.61 | 65.39 | 48.54 |
| 4-zone | After the first pass, 2026-11-11 | 35 | 107.45 | 114.55 | 82.50 | 64.52 | 48.55 |
| 4-zone | After the second pass: the board as it is | 32 | 98.24 | 110.82 | 79.90 | 62.87 | 47.48 |
| 4-zone | After the revision B package | 25 | 76.75 | 104.44 | 76.62 | 61.10 | 47.00 |

<!-- zfcost:end -->

<!-- zfcost:begin stage-savings -->

| Stage, 8-zone | Fee-carrying types | Saved per controller at 5, USD (of which fitted parts before duty) | Saved per controller at 10, USD (of which fitted parts before duty) | Saved per controller at 25, USD (of which fitted parts before duty) | Saved per controller at 100, USD (of which fitted parts before duty) |
|---|---|---|---|---|---|
| After the first pass, 2026-11-11 | 40 to 35 | 4.19 (0.03) | 2.11 (0.03) | 0.87 (0.03) | -0.01 (-0.00) |
| After the second pass: the board as it is | 35 to 32 | 4.21 (1.27) | 3.13 (1.40) | 2.16 (1.23) | 1.51 (1.10) |
| After the revision B package | 32 to 25 | 6.38 (0.46) | 3.27 (0.31) | 1.76 (0.47) | 0.50 (0.33) |

<!-- zfcost:end -->

The first table is the model run four times: on the board as it is, with the
old part numbers of one or both passes put back
(`tools/cost/data/replaced-parts.csv`, fetched the same day), and with the
revision B package applied (`scenario_revb` in `tools/cost/zfcost/model.py`).
All four use the prices of 2026-11-12. The second table is the difference
between each stage and the one before it, and in brackets how much of it is
the price of the fitted parts.

- **Each type is 3.07 USD per Economic order**, 4.14 USD with duty: 0.83 USD per controller at 5, 0.17 USD at 25. At 100 units the model orders Standard assembly, which charges every type alike, so a type saves money there only if it leaves the board altogether, 0.02 USD per controller.
- **The second pass also saves in part prices.** The fourteen 48 V clamps of the second maker cost 0.04 USD each where the first maker's cost 0.11 to 0.13 USD. That is why it saves 1.51 USD per controller at 100 units, where the first pass saved nothing.
- **The revision B package adds ten parts** and still saves at every quantity, because one of its changes removes the supervisor chip.

### The second pass: what was swapped on revision A

| Parts | Was | Is now | Library, stock on 2026-11-12, USD at 100 pieces | Fee |
|---|---|---|---|---|
| D1, surge clamp on the 24 VAC input | Littelfuse SMCJ48CA, C151287 | Hongjiacheng SMCJ48CA, C41411765 | Preferred extended, 14,082, 0.098 (was 0.197) | One type fewer |
| D12, D13, D14, D18, D23, D101 to D901: the fourteen 600 W clamps on the field terminals | Littelfuse SMBJ48CA, C151264 | Hongjiacheng SMBJ48CA, C19077591 | Preferred extended, 2,730, 0.039 (was 0.127) | One type fewer |
| D6, D102 to D902: status light and the nine output lights | Lite-On LTST-C190KGKT, yellow-green, C125094 | KENTO KT-0603R, red, C2286, the part of the fault light D8 | Basic, 5,966,082, 0.0077 (was 0.019) | One type fewer, and one type fewer on the board |
| U12, the supply supervisor | TI TLV803EA30DBZR, 3.08 V, C5218924 | TI TLV803EA29DBZR, 2.93 V, C1852114 | Extended, 1,206, 0.31 (was 0.24) | None. This is the correction of [issue #58](https://github.com/syntroniks/zoneflow/issues/58) |

**The two 48 V clamps.** Second makers of protection parts were ruled out in
the first pass. They are allowed now, on one condition: every figure and
curve that a sum of the design uses must be in the second maker's own sheet.
I read the Hongjiacheng SMCJ sheet (rev 2.2) and SMBJ sheet (rev 1.1) against
the Littelfuse sheets the design was calculated from.

| | Littelfuse SMCJ48CA | Hongjiacheng SMCJ48CA | Littelfuse SMBJ48CA | Hongjiacheng SMBJ48CA |
|---|---|---|---|---|
| Stand-off voltage | 48 V | 48 V | 48 V | 48 V |
| Breakdown at 1 mA | 53.3 to 58.9 V | 53.3 to 58.9 V | 53.3 to 58.9 V | 53.3 to 58.9 V |
| Clamping voltage at the rated pulse current, 10/1000 us | 77.4 V at 19.4 A | 77.4 V at 19.4 A | 77.4 V at 7.8 A | 77.4 V at 7.75 A |
| Peak pulse power, 10/1000 us | 1500 W | 1500 W | 600 W | 600 W |
| Leakage at 48 V | 1 uA | 1 uA | 1 uA | 5 uA |
| Steady power | 6.5 W | 6.5 W | 5 W | 5 W |
| Junction to lead | 15 K/W | 15 K/W | 20 K/W | 20 K/W |
| Junction temperature | -65 to 150 °C | -55 to 150 °C | -65 to 150 °C | -55 to 150 °C |
| Failure mode stated | Short | Short | Short | Short |
| Body, lead span | 6.60 to 7.11 x 5.59 to 6.22 mm, 7.75 to 8.13 mm | The same | 4.06 to 4.75 x 3.30 to 3.94 mm, 5.21 to 5.59 mm | 4.25 to 4.85 x 3.30 to 3.94 mm, 5.08 to 5.59 mm |

What the design takes from these parts, and where:

- **The stand-off, breakdown and clamping voltages at the rated 10/1000 us pulse** (design sections 4.4, 6.6, 7.2, 13.3 and 13.4). They are equal in both makers' tables, so the sums of those sections stand as written: the 14 V rise of C1 behind R1, the 137 V across D2, the 77 V on a triac rated 600 V and on a snubber capacitor rated 100 V, the 7 mA into a sensor clamp.
- **The energy of the relay's inductive kick into D23** (section 7.2), which the design compared with "600 W for 1 ms". The Hongjiacheng curve of peak power against pulse length (its figure 1) ends at 1 ms, and the kick lasts 2.3 to 3.3 ms. I redid that sum so that it needs no curve: the kick starts at 38 to 55 W and falls to zero, and the rated 10/1000 us pulse of figure 3 still delivers 61 W after 3.3 ms, so the kick is under the rated pulse at every moment. It carries 63 mJ against the 0.87 J of the rated pulse.
- **Leakage.** 5 uA at 48 V against 1 uA. The largest effect is on a sensor terminal, 10 kohm from 5 V: 50 mV at most if the part leaked its 48 V figure at 5 V. On an output it is 5 uA through a valve that carries 90 uA of snubber current already.
- **Nothing else.** No sum of the design reads the pulse curve of the 48 V parts beyond the rated pulse. Surge immunity of the terminals was a bench item before and is one now.

**D25 was not swapped, for that reason.** The 6 V clamp on the buck output is
the Littelfuse SMBJ6.0A. The Hongjiacheng part of that name, C19077560, is
preferred, has 174,319 in stock and the same table figures: 6.67 to 7.37 V,
10.3 V at 58.25 A, 800 uA, 5 W. But section 4.8 of the design reads the
curve of peak power against pulse length out to 10 ms: it is what says that
D25 survives the cases in which the fuse F5 takes up to 18 ms to open. The
Littelfuse figure 2 runs to 10 ms. The Hongjiacheng figure 1 ends at 1 ms,
and so does the Brightking one for the basic P6SMB6.8CA. A part whose sheet
lacks the curve that the sum needs does not qualify.

**The lights.** The owner decided on 2026-11-12 that the colour of the lights
does not matter. The basic library has two 0603 LEDs, red and white, and the
preferred list one, yellow. White needs 2.6 to 3.1 V and does not run from a
3.3 V rail behind 680 ohm and an expander pin. Red is the KT-0603R that the
fault light D8 used already, so the eleven lights are now one basic type.

| | LTST-C190KGKT, before | KT-0603R, now |
|---|---|---|
| Forward voltage at 20 mA | 1.9 to 2.4 V | 1.8 to 2.4 V |
| Luminous intensity at 20 mA | 18 to 71 mcd | 145 to 300 mcd |
| Forward current, maximum | 30 mA | 25 mA |
| Reverse voltage | 5 V | 5 V |
| Operating temperature | -30 to 85 °C | -40 to 85 °C |
| Height | 0.8 mm | 0.6 mm |

- **Current with the resistors that are fitted.** 1.7 mA in an output light, 2.1 mA in the status light, 1.4 mA in the fault light, from the typical curve of the KENTO sheet. Far inside 25 mA.
- **Brightness.** Eight times the light at the same current by the minimum figures of the two sheets. The fault light has run this part at 1.4 mA from the start. Whether that is right behind a light pipe was a bench item for the old part and is one for this.
- **The expander pin.** Each output light shares its expander pin with the opto LED of its channel, so its current adds to the pin's. The lowest forward voltage is 0.1 V lower, which is 0.14 mA more. Solved for both parts in the same way (design section 6.4): the worst-case opto LED current at -20 °C goes from 7.06 to 7.02 mA against the 5.6 mA it needs, a margin of 1.26 before and 1.25 now, and the highest pin current from 17.0 to 17.2 mA against the 18 mA that the TCA9539 sheet recommends up to 85 °C.
- **The fail-safe.** The fault injection models every indicator with one diode model, the red fault light included, so its decks do not change with the colour. An indicator's current never passes through an opto LED, so it cannot fire a triac.
- **The status light is on a strapping pin**, GPIO46, which must read low at reset. R20 and the LED to ground can only pull down, with either part.
- **Pins and pads.** Anode and cathode on the same pads. The placement check puts the cathode of all eleven where JLCPCB's library footprint has it.
- **The enclosure.** The part is 0.2 mm lower, so each light pipe ends 0.9 mm above its LED where it ended 0.7 mm. The mechanical contract and the test-fit page say so now.

**The supervisor.** U12 was the wrong option of its family: its threshold is
3.08 V and not the 3.0 V the design gave it, which left 12 mV between the
lowest rail and the level at which the board is released from reset. Option
29 of the same family, 2.93 V, has the same package and pins and leaves
167 mV. [Design section 12.4](../design/hardware-design.md#124-supply-reset-and-boot)
has the figures and what the lower threshold costs. It is an extended part
like the one it replaces, so no fee moves. Can U12 and the other supervisor
on the board, U13, be one type? Not on these pads: U13 is a window detector
in a six-pin package that watches for a rail that is too high. But U13 has a
second comparator that is not used and that watches for a rail that is too
low. In revision B it can take over the job of U12, which then leaves the
board: [the package](#the-revision-b-package).

**The 100 kohm arrays were not swapped.** The basic array is 10 kohm. The
brief for this pass was to run the fail-safe fault injection with 10 kohm
and to swap only if every margin of the analysis holds. One does not.

| | 100 kohm, as fitted | 10 kohm |
|---|---|---|
| Fault injection, 397 faults (`hardware/sim/failsafe`, `faults`) | 3 leave a valve on, all bridges onto the keep-alive net | The same 3. No row loses another layer. A valve pin stuck high is found by the stop test where the weld check found it. Three rows with a bridge at U7 did not finish in their 90 s, on a machine that was busy, and were not repeated |
| Self-test on a healthy board, 96 corners (`sequence`) | Passes | Passes. Two release times move by 5 ms |
| Worst-case opto LED current at -20 °C against the 5.6 mA it needs, with the red lights | 7.02 mA, margin 1.25 | 6.95 mA, margin 1.24 |
| Highest expander pin current against 18 mA | 17.2 mA | 17.5 mA |
| **Opto LED current of a running zone after the keep-alive has stopped, with an expander that keeps its bits** (`hardware/sim/array_feed.cir`) | **0.08 to 0.13 mA** | **1.3 to 1.6 mA** |
| The criterion of the analysis for "the triac may fire" | 0.25 mA | 0.25 mA |

The last rows are the reason. If the expander ignores its reset, or its
reset pin is open, or `DRV_EN` is stuck high, the transistor Q3 still takes
the LED supply away. That is the layer of the fail-safe that answers those
faults. But the valve pins that are off are then still driven high, and each
feeds the LED supply backwards through its pull-up, around Q3. With 100 kohm
that is a third to a half of the current the analysis counts as dangerous.
With 10 kohm it is five to six times more than that, and only the relay
would be left to stop the valve. The fault injection does not show this,
because it models two of the nine valve pins. `array_feed.cir` is new and has
all of them. So the type stays, and the way to remove it is in the revision B
table below.

**Checks run for the second pass.** No board exists; these are the checks that ran.

- `make -C hardware design-check`: netlist, design tables, BOM and the committed schematic outputs agree.
- `make` in `hardware/zoneflow-a/` with the autorouter forbidden (`ROUTE_FLAGS=--stored-only`): netlist parity, ERC, DRC with schematic parity and every board check pass on the stored routing.
- The board file differs from the one before in 170 lines, each a `Value`, `Spec`, `LCSC`, `MPN`, `Manufacturer` or `Description` field of one of the 26 parts. No Gerber, drill or placement file changed. The two renders and the STEP model came out different in 84 and 3 pixels and in the order of their entries, with the same 77 bodies; they are left as they were.
- `make cpl-check`: 307 placements of the 8-zone build and 262 of the 4-zone build against JLCPCB's library footprints, none misfits. JLCPCB's library footprint for the Hongjiacheng SMB part is drawn for a lead span of 5.4 mm where the Littelfuse one has 5.3 mm; the worst pad is 0.44 mm from its land where it was 0.24 mm, which the check passes. Both clamps are bidirectional, so they have no polarity to turn.
- `hardware/sim/failsafe`, every run, on the netlist as it is, with the supervisor of the model moved to the lowest threshold of the new part: see [what the runs gave](#what-the-fail-safe-runs-gave).
- `hardware/sim/array_feed.cir`, as above.

### All 35 types, one by one

The 35 types that paid the fee before this pass. "Fee" in the last column is
the 3.07 USD per Economic order of one type. Stock and prices are JLCPCB's on
2026-11-12; the price is for 100 pieces.

| # | Type, and where it is | Verdict | Replacement | What was compared, and what it takes | Saving |
|---|---|---|---|---|---|
| 1 | 0154003.DRT, fuse holder with a 3 A time-lag fuse: F1, F4 | Stays | None. No fuse or holder is basic or preferred | Its melting I²t of 20.16 A²s and its opening times are the inputs of design sections 4.3 and 6.6. It cannot share a type with F5, which must melt at 0.0824 A²s | |
| 2 | SMCJ48CA, 1500 W clamp: D1 | **Swapped** | Hongjiacheng SMCJ48CA, C41411765, preferred, 14,082, 0.098 | Table above. No sum redone | Fee, and 0.10 a board |
| 3 | SR2512JK-073RL, 3 ohm surge resistor: R1 | Stays | None. No 2512 part and no pulse-rated resistor in either list | Design section 4.4 needs a single-pulse curve at 240 to 390 W for 1 ms. Basic 1206 parts in parallel have no such curve in the sheet | |
| 4 | 0451.500MRL, 0.5 A very fast fuse: F5 | Stays | None | Its I²t and its 0.42 ohm are in the sum of section 4.8. "Do not fit a larger rating" | |
| 5 | CL32B225KCJSNNE, 2.2 uF 100 V, 1210: C2, C3 | Revision B | Two series pairs of the basic 4.7 uF 50 V X7R 1206 part, C29823, 575,531, 0.055, each part with a basic 1 Mohm across it | 100 V because the input reaches 59 V in a surge. Two 50 V parts in series see 32.5 V each at 10 % mismatch. Needs the layout at the pins of U1 and the capacitance under bias read against the LM5164 sheet | Fee, for six parts more |
| 6 | LM5164DDAR, 100 V buck converter: U1 | Stays | None. The basic and preferred converters stop at 28 to 40 V of input by their listings | The specification asks for 60 V or more. 852 in stock, half of the day before | |
| 7 | SRN6045TA-470M, 47 uH: L1 | Stays | None. The lists hold no power inductor | | |
| 8 | 4D03WGJ0104T5E, 100 kohm x4: RN1, RN2, RN3 | Revision B, or stays | On these pads only the basic 10 kohm array, C29718. In revision B twelve basic 100 kohm 0603 resistors, the type of R96 | Not swapped: table above. Twelve single resistors are nine placements more for one fee, so this is in the package only if the board is laid out again anyway | Fee, for nine parts more |
| 9 | SMBJ6.0A, 6 V clamp: D25 | Stays; decision | Hongjiacheng SMBJ6.0A, C19077560, preferred, 174,319, 0.039 | Same table figures. Its pulse curve ends at 1 ms and section 4.8 needs 10 ms: above | Fee |
| 10 | TYPE-C-31-M-12, USB-C receptacle: J5 | Stays | None. The lists hold no connector | | |
| 11 | USBLC6-2SC6: U2 on USB, U9 on the Qwiic connector | Stays; decision | SRV05-4, Hongjiacheng C7420376, preferred, 307,505, 0.075 | Fails on two figures. Working voltage 5.0 V at most, and pin 5 of U2 is on VBUS, which may be 5.25 V. Leakage 5 uA against 150 nA. Its capacitance is given as typical only. It would do on U9, which sits on 3.3 V, but one position does not remove the type. With pin 5 of U2 taken off VBUS it protects the data lines and no longer VBUS | Fee |
| 12 | LDL1117S33R, 3.3 V regulator: U3 | Stays; decision | AMS1117-3.3, C6186, basic, 1,476,272, 0.15 | Same pins. Drops up to 1.3 V at 0.8 A, so it needs 4.6 V in. On 24 VAC the 5 V bus is 4.66 V at its lowest: 0.06 V in hand. On USB it can be 4.1 V, and the rail would fall to 2.8 V | Fee, and 0.13 a board |
| 13 | ESP32-S3-WROOM-1-N16R8: U4 | Stays | None | | |
| 14 | SM04B-SRSS-TB, Qwiic connector: J6 | Stays | None. No connector | 7,233 in stock, a tenth of the day before | |
| 15 | 1206L012WR, PTC 0.125 A: F2, Qwiic supply | Revision B | 1812L020/60DR, the part of F3, on an 1812 footprint | Two PTCs become one type. Holds 0.2 A where F2 holds 0.125 A, 1.4 to 4.4 ohm cold where F2 has up to 6 ohm. To check then: how long the 3.3 V rail sags while it trips on a shorted cable | Fee |
| 16 | TCA9539PWR, expander: U5 | Stays | None of its kind. The lists hold shift registers, 74HC595 and 74HC165 | Shift registers need three processor pins that the module does not have (design 12.2), and the fail-safe analysis rests on what this part does on its reset pin | |
| 17 | LM2901DR, quad comparator: U6 | Stays | LM393DR2G, C7955, basic | Two comparators in SOIC-8, 0 to 70 °C by its listing. The board uses four, from -20 °C. Two of them are two footprints and still the wrong temperature range | |
| 18 | TLV803EA30DBZR, supervisor: U12 | **Swapped**, then revision B | Now TLV803EA29DBZR, C1852114, extended, 1,206, 0.31. In revision B: no part, comparator A of U13 with two basic resistors | Issue #58. The second step needs a divider, a look at what holds `EN` below 1.8 V of supply, where U13's outputs are not defined, and the fault injection of U13 again | None now. Fee and 0.31 a board in revision B |
| 19 | TPS3701DDCR, over-voltage detector: U13 | Stays | None. A discrete detector from the basic CJ431 and two transistors is about seven parts for this one | In revision B it does the job of U12 as well | |
| 20 | 74HC132D, quad Schmitt NAND: U7 | Stays | 74HC14D, basic: six Schmitt inverters. 74HC08D, preferred: AND gates without Schmitt inputs | The latch and the enable gate need three two-input gates on slow inputs. Inverters with diode gates are at least nine parts more, with low levels a diode drop above ground, and the whole fault injection again. Not proposed | |
| 21 | RLP25FEER100, 0.1 ohm 2 W shunt: R37 | Stays | None in 2512. The basic 0.1 ohm is 1206, 0.25 W, 800 ppm/K | 0.63 W at inrush and 1 % with a low drift are what section 10.5 budgets. Ten basic 1 ohm 1206 parts in parallel would carry it at four times the drift, with no defined Kelvin point, for nine parts more | |
| 22 | LTST-C190KGKT, yellow-green LED: D6, D102 to D902 | **Swapped** | KT-0603R, red, C2286, basic, 5,966,082, 0.0077 | Above. Section 6.4 solved again | Fee, the type itself, and 0.12 a board |
| 23 | CR2032-BS-6-1, cell holder: BT1 | Stays | None | | |
| 24 | FPC-05F-12PH20, display connector: J7 | Stays | None | | |
| 25 | 1206W4F330JT5E, 33 ohm 1206: R60, backlight | Revision B | Three 100 ohm 0805 parts, C17408, in parallel: basic, on the board already as R103 to R903 | 33.3 ohm, each part 0.05 W of its 0.125 W at the 0.16 W the three share. On these pads the basic 22 ohm 1206 gives 89 mA into a backlight rated 80 mA | Fee, and the type, for two parts more |
| 26 | EC11J1525402, encoder: SW3 | Stays | None | | |
| 27 | TS-1102S-C-M-B, 6 x 6 mm switch: SW4, SW5 | Revision B | TS-1187A-B-A-B, C318884, basic, on the board as SW1 and SW2 | 1.5 mm high where this one is 10 mm: the button caps of the enclosure have to reach down to it. Its force was not compared | Fee, and the type |
| 28 | SMBJ48CA, 600 W clamp: fourteen places | **Swapped** | Hongjiacheng SMBJ48CA, C19077591, preferred, 2,730, 0.039 | Table above. The sum of section 7.2 redone. Stock is under two batches of 100 | Fee, and 1.2 a board |
| 29 | TPS2553DBVR, sensor supply switch: U11 | Stays; decision | A basic P-channel switch, AO3401A, with a transistor to drive it | Loses the limit of 116 to 158 mA and the fault flag. A shorted sensor wire would then draw 0.5 to 0.8 A from the buck converter for the seconds the PTC F3 takes, against the 0.72 A it has in hand | Fee, and about 0.3 a board |
| 30 | 1206W4F470KT5E, 4.7 ohm 1206: R84, sensor supply | Revision B | Two basic 10 ohm 1206 parts, C17903, 900,456, in parallel | 5 ohm. 15 mV more drop at 50 mA, 6 % less current into D24 in a miswire (section 13.5), each part at 0.06 W where the one part takes 0.12 W with U11 at its limit | Fee, for one part more |
| 31 | CL21B103KCANNNC, 10 nF 100 V 0805: nine snubbers and C48 | Revision B | The basic 100 nF 100 V 0805 part, C28233, 2,961,434, 0.012, on the same pads. Or no snubbers, which is issue #31's own candidate and needs a bench | Ten times the snubber current: 0.9 mA through each idle valve, 8 mA for nine against the firmware's idle threshold of 30 mA. A slower voltage rise at turn-off, which is the safe direction. The threshold and the 5 mV it puts on the high-gain current channel have to be looked at with the firmware first | Fee |
| 32 | 1812L020/60DR, PTC 0.2 A 60 V: F3, sensor supply | Stays | None. The lists hold no PTC | It takes F2's place as well in revision B | |
| 33 | BSMD2920-100-60V, PTC 1.1 A: F101 to F901 | Stays | None | Its 40 A maximum fault current and its trip point of 0.5 s at 8 A are the inputs of section 6.6 | |
| 34 | T810-600B-TR, triac: Q101 to Q901 | Stays | None. The lists hold no triac | 2,882 in stock, 3.2 batches of 100 | |
| 35 | MOC3023SR2M, opto-triac: U101 to U901 | Stays | None. The two basic optocouplers have transistor outputs | | |

Counted: 3 swapped for a fee (rows 2, 22, 28), 7 in the revision B package
(rows 5, 15, 18, 25, 27, 30, 31, of which row 18 also changed its part
number now), 1 more that revision B could take for nine parts (row 8), 4
that need a decision (rows 9, 11, 12, 29), and 20 that stay because the
lists hold no part of the kind or none that meets the figures.

**Parts the builder solders anyway.** The six through-hole parts are bought
loose and pay no fee already. Every one of the 32 types left is a
surface-mount part. Moving one to the soldering iron to save 3.07 USD would
cost the builder more minutes than the fee is worth and is not what the
specification asks for, so none is proposed.

### The revision B package

Seven changes, each of which takes one fee-carrying type off the board. None
is made in this revision: each needs new copper, the enclosure, the firmware
or a look that was not taken here. `uv run zfcost table` prices the package
as the scenario `revb`.

| Change | Type it removes | Parts | What it needs first |
|---|---|---|---|
| U13's unused comparator A watches for a low rail and pulls `EN`; U12 goes | TLV803EA29DBZR | -1 chip, +2 resistors | The divider values, what holds `EN` low under 1.8 V, the fault injection of U13 again |
| R60 becomes three 100 ohm 0805 in parallel | 33 ohm 1206 | +2 | Layout |
| R84 becomes two 10 ohm 1206 in parallel | 4.7 ohm 1206 | +1 | Layout |
| C2 and C3 become two series pairs of 4.7 uF 50 V with 1 Mohm across each part | 2.2 uF 100 V 1210 | +6 | Layout at U1, the LM5164's input capacitance under bias |
| F2 becomes the PTC type of F3 | 1206L012WR | 0 | An 1812 footprint, the trip time against the 3.3 V rail |
| SW4 and SW5 become the 5.1 mm switch of SW1 and SW2 | TS-1102S-C-M-B | 0 | Button caps 8.5 mm longer in the enclosure, a new footprint |
| The ten 10 nF 100 V capacitors become the basic 100 nF 100 V part, or the nine snubbers go | 10 nF 100 V 0805 | 0, or -18 | The firmware's idle threshold; for no snubbers, a bench |

That is 32 types to 25, ten parts and nineteen solder joints more, and by
the model 6.38, 1.76 and 0.50 USD less per 8-zone controller at 5, 25 and
100. Rows 8 of the table above, twelve single resistors for the arrays,
would make it 24.

**The fail-safe variants.** The [reduced fail-safe](../design/failsafe-reduction-study.md)
that the owner is considering changes parts in this list. Under "V1 with the
comparator and K1", which its [review](../design/failsafe-reduction-review.md)
recommends, the 74HC132 goes and a 74LVC1G123 comes, an extended part too.
The LM2901 stays, with three of its four comparators in use. A second
supervisor comes, of the type of U12. So the count is the same as for
revision A, with or without this package: 32 now, 25 after it. One thing
changes: with a second TLV803E on the board, folding U12 into U13 no longer
removes the type, and the package is worth 26.

### Decisions waiting for the owner

Each would remove one more type. The scenario `needs_decision` prices the
four together: 3.32, 0.66 and 0.00 USD per controller at 5, 25 and 100,
before any difference in part prices.

| Type | The swap | What it costs |
|---|---|---|
| SMBJ6.0A, D25 | The preferred part of the same name, or a basic 6.5 V or 6.8 V part | The sum of design section 4.8 would rest on a pulse curve that the maker does not print beyond 1 ms. A bench test of the clamp and the fuse together (risk 21 of the design) would settle it |
| USBLC6-2SC6, U2 and U9 | SRV05-4 | Pin 5 of U2 off VBUS, which is a track to cut in the layout, and no clamp on VBUS at the connector |
| LDL1117S33R, U3 | AMS1117-3.3 | The board would no longer be sure to run from a USB port alone: it needs 4.6 V on the 5 V bus and a port may leave 4.1 V. On 24 VAC it has 0.06 V in hand, which a buck output 0.2 V higher would cure |
| TPS2553DBVR, U11 | A basic transistor switch | No current limit and no fault flag on the sensor supply; a shorted sensor wire loads the buck converter until the PTC trips |

I recommend none of the four as they stand. The first is worth a bench test
when boards exist.

### What the fail-safe runs gave

`hardware/sim/failsafe` writes its decks from the netlist and from part
models. Two things it reads changed in this pass. The value text of the
lights changes no deck, because every indicator, red or not, has one diode
model. The supervisor does: the model takes the processor as reset below the
lowest threshold of U12, which was 2.94 V in the model and is 2.871 V for
the part now fitted. So every run was made again on 2026-11-12: `sequence`,
`faults`, `bridges`, `duty`, `warmup`, `ldo`, `floatload`, `corners` and
`commission`.

- **No verdict changed.** 397 faults, the same 3 that leave a valve on, all bridges onto the keep-alive net; the same 60 rows with a layer lost, each found by the same step; the self-test passes on a healthy board at the same 96 corners; the tables of design sections 7.3, 8.4, 8.5 and 9.5 stand.
- **What moved is the last printed digit** in 395 of about 9,300 result rows: 1 ms, 1 mV or less. Two lines of the summaries moved with it: the longest time the relay detector alone may hold in the Monte Carlo run is 0.682 s where it printed 0.681 s, and the shortest enable time of detector A in the commissioning grid is 12 ms where it printed 13 ms. One row of the commissioning run moved by more: an enable time of detector A of 26 ms where it was 31 ms, which is one keep-alive edge earlier. The result files are now those of the present model.
- **So no result depends on where between 2.87 and 2.94 V the processor is reset.** I did not look for the reason in each run. The corners set the rail no lower than 3.0 V, and the faults that pull it down pull it far below both levels.
- **Four rows of `faults` ran out of their 90 s** while the machine was building the board, and were made again alone. Two of them are bridges across a gate of U7 that oscillate in the model and never finish their first run, as the design says in section 8.5. Their other runs finished this time, with the verdict they had.

The run with 10 kohm arrays is not in the result files, because that value
was not fitted. Its outcome is in the table of the arrays above.

### The first pass, 2026-11-11

The rule was: no change to what the circuit does, no footprint change, no
new layout. Five types changed.


| Parts | Was | Is now | Library, stock | Why it is the same part for this circuit |
|---|---|---|---|---|
| C4, bootstrap of the buck converter | Samsung CL10B222KB8NNNC, C16033 | Fenghua 0603B222K500NT, C1604 | Basic, 543,476 | 2.2 nF, X7R, ±10 %, 50 V, 0603: all equal |
| C24, C34, C55: comparator inputs of the keep-alive detector, the zero-cross input and the 3V3 over-voltage detector | Samsung CL10B103KB8NNNC, C1589 | Fenghua 0603B103K500NT, C57112 | Basic, 5,459,902 | 10 nF, X7R, ±10 %, 50 V, 0603: all equal. Insulation resistance 10 Gohm minimum in both makers' specifications |
| C33, filter ahead of the over-current comparators | Samsung CL10B472KB8NNNC, C1621 | Fenghua 0603B472K500NT, C53987 | Basic, 818,247 | 4.7 nF, X7R, ±10 %, 50 V, 0603: all equal |
| C28, C29, feedback capacitors of the high-gain current amplifier | Samsung CL10C221JB8NNNC, 220 pF C0G, C27675 | Samsung CL10C331JB8NNNC, 330 pF C0G, C1664 | Basic, 627,149 | **A value change**, the only one. Same maker, series, C0G, ±5 %, 50 V, 0603. No 220 pF C0G is basic or preferred in 0603; the basic 220 pF is X7R. See below |
| D7, D9, D15, D16, D17, D22: the two keep-alive pump diodes and four input clamps | Nexperia BAT54S,215, C47546 | Hongjiacheng BAT54S, C7420333 | Preferred extended, 514,643 | Data-sheet limits equal where the design uses them. See below |

**The three X7R capacitors.** Value, dielectric, tolerance, voltage and
package were read from the listings, and the insulation resistance from the
Fenghua general specification that LCSC serves for these numbers (10 Gohm
minimum up to 25 nF) and the Samsung catalogue (10 Gohm or 500 Mohm x uF,
whichever is less). C24, C33 and C55 sit in circuits that the reviews
simulated. Those simulations take a capacitance and its tolerance, which did
not change: see "checks" below.

**C28 and C29, 220 pF to 330 pF.** With the 49.9 kohm feedback resistors
they set the corner of the high-gain current amplifier: 14.5 kHz before,
9.7 kHz now. That output goes only to the ADC pin, through the 1.6 kHz filter
R47 and C31, so the corner that the firmware sees does not move. The
over-current comparators hang on the other amplifier. A larger feedback
capacitor makes the stage slower and no less stable. `hardware/sim/isense.cir`
run with 330 pF gives the same results at the precision the design quotes:
664.9 mV/A on the high-gain channel (0.664935 V/A before, 0.664932 after),
0.94 mV peak to peak for 20 mV of ground shift on both channels, the same
low-gain figures, and the trip 66 us after a short (65.7 and 65.9 us, a
difference of one time step in a path that does not contain these parts).
The other choice was 100 pF C0G, which is on the board already as C8 and
would remove a type; it moves the corner up to 32 kHz, the direction of less
filtering, so I took 330 pF.

**BAT54S.** I read the Hongjiacheng sheet (BAT54 to BAT54S, rev 2.0) against
the Nexperia one the design and review A2 used:

| | Nexperia BAT54S | Hongjiacheng BAT54S |
|---|---|---|
| Forward voltage, maximum at 0.1, 1, 10, 30 mA | 240, 320, 400, 500 mV | 240, 320, 400, 500 mV |
| The same at 100 mA | 800 mV | 1000 mV |
| Reverse current, maximum at 25 V and 25 °C | 2 uA | 2 uA |
| Reverse current, typical, hot | 18 to 35 uA at 85 °C, 180 to 300 uA at 125 °C, from 1 to 10 V | about 25 to 55 uA at 100 °C from 0 to 10 V, read from figure 2 |
| Reverse current, typical, 25 °C | 0.25 to 0.7 uA from 1 to 10 V | about 0.1 to 0.3 uA from 0 to 10 V |
| Capacitance, recovery | 10 pF, 5 ns | 10 pF, 5 ns |
| Reverse voltage, forward current, surge | 30 V, 200 mA, 600 mA | 30 V, 200 mA, 600 mA |
| Junction temperature | 150 °C | 125 °C |
| Pins | 1 anode, 2 cathode, 3 common | The same, from the function diagram |

No diode on this board carries more than about 15 mA (3.3 V through R29,
220 ohm), so the 100 mA row is not used, and the air inside the enclosure
stays at or below 70 °C. The corner runs of review A2
(`hardware/sim/failsafe/`) take the diode between two ends: the maximum
forward voltages of the table, which are equal in the two sheets, and three
times the typical leakage of the Nexperia figure, which the Hongjiacheng
curves stay under. So those corners cover the new part as they covered the
old one. The limit that matters for safety, how long a detector can hold
after the keep-alive stops, is set by the bleed resistors with a diode that
does not leak at all, and does not depend on the maker. What neither sheet
gives is a maximum leakage when hot; that stays the bench item it was
([design](../design/hardware-design.md), section 22).

"Preferred extended" is JLCPCB's flag and JLCPCB can withdraw it. If it
does, this type pays 3.07 USD again and nothing else changes.

**Checks run for the swaps.** No board exists; these are the checks that ran.

- `make -C hardware design-check`: netlist, design tables, BOM and the committed schematic outputs agree.
- `make` in `hardware/zoneflow-a/` with the autorouter forbidden (`ROUTE_FLAGS=--stored-only`): netlist parity, ERC, DRC with schematic parity and every board check pass on the stored routing.
- The board file differs from the one before in 41 lines, each a `Value`, `Spec`, `LCSC`, `MPN` or `Manufacturer` field of one of the 13 parts. No Gerber, drill or placement file changed.
- `make cpl-check`: 312 placements of the 8-zone build and 267 of the 4-zone build against JLCPCB's library footprints for the new numbers, none misfits. The new BAT54S lands pin 1 on pin 1 with the same 180 degree correction, 0.06 mm at worst. JLCPCB's library **symbol** for it names the pins as a common-anode pair, which is not what the maker's sheet draws; the footprint numbers the pads as every SOT-23 does. One more reason to look at the placement preview and to probe D7 on the first board.
- The fail-safe simulation builds its decks from the netlist. All 1,440 corner decks and the four scenario decks are byte for byte what they were before the swap. `failsafe.py corners` run again (2,104 runs): no run changed its verdict, and every line of the summary is the same but for one coil current of zero that prints as -1.04e-05 mA where it printed -1.11e-05. 197 rows differ from the committed files by one unit in the last printed digit (1 ms, 1 mV, 0.01 mA). That is not this change: the committed files were from the netlist of review A2, before the over-voltage detector U13. Three of those rows run on that netlist give the committed figures, and on the netlist before and after the swap they give one and the same new figure. The result files are now those of the present netlist.
- `hardware/sim/isense.cir` with 330 pF, as above.


**Values that could share a type.** Merging two basic values saves nothing
on an Economic order and 1.53 USD on a Standard one. I looked at the pairs
that serve one purpose and merged none:

- **Pull-ups of 3.3 kohm (five controls) into 2.2 kohm.** The encoder would get 1.5 mA, inside its 1 to 10 mA. But these pull-ups are in the fail-safe bridge analysis: a control net bridged to the keep-alive pin is modelled as 13.3 kohm to 3V3 (`hardware/sim/failsafe/faults.py`). A change means those runs again.
- **Pull-ups of 4.7 kohm on the Qwiic bus into 2.2 kohm.** Boards plugged in bring their own pull-ups, and the sum must stay above 1 kohm. The third 4.7 kohm part, R92, is in the gate-drive read-back.
- **22 nF on the three sensor inputs into 10 nF.** It would halve the filter time on the flow input for no saving that counts.
- **SS34 and SS210, M7.** Different voltage ratings for different places.
- **LED resistors.** Already one value, 680 ohm; the red fault LED uses the 1 kohm that 19 other places use.


## Five ways to cut cost

Ordered from least work to most. Each saving is the model run with the
change, against the base case, for the 8-zone build.

<!-- zfcost:begin cuts -->

| Scenario | Change | Saves USD per controller at 5 | Saves USD per controller at 25 | Saves USD per controller at 100 |
|---|---|---|---|---|
| `selftap` | Self-tapping screws in place of the seven heat-set inserts | 1.67 | 0.67 | 0.58 |
| `revb` | The revision B package: seven extended types removed by changes to the circuit or the layout | 6.38 | 1.76 | 0.50 |
| `needs_decision` | Four more extended types whose replacement costs function or margin; each needs a decision | 3.32 | 0.66 | 0.00 |
| `issue31` | Issue #31: no output snubbers; zone LED in series with the opto-triac LED | 0.34 | 0.36 | 0.32 |

<!-- zfcost:end -->

<!-- zfcost:begin batch -->

| Build | 5 units | 25 units | Saved 5 to 25 | 100 units | Saved 25 to 100 |
|---|---|---|---|---|---|
| 8-zone | 117.60 | 69.00 | 48.60 | 52.79 | 16.21 |
| 4-zone | 110.82 | 62.87 | 47.96 | 47.48 | 15.39 |

<!-- zfcost:end -->

1. **Order more at once** (no design change). Going from 5 to 25 boards
   saves 49 USD per controller, more than all the design changes together.
   Five friends ordering one batch of 25 pay less each than one person
   ordering five.
2. **Self-tapping screws** (`fastener = "selftap"` in the enclosure model, no
   design change): 0.58 to 1.67 USD. The printing page says this suits a unit
   that is opened a few times.
3. **Fewer extended part types.** What can be done on revision A is done:
   eight types in two passes, [above](#fewer-extended-part-types), worth
   8.39 USD per controller at 5 boards and 1.50 USD at 100 against the board
   as designed. The [revision B package](#the-revision-b-package) is seven
   more, 6.38 USD at 5 and 0.50 USD at 100, for ten parts more and a new
   layout. Four further types each wait for a
   [decision](#decisions-waiting-for-the-owner): together 3.32 USD at 5 and
   nothing at 100, where Standard charges every type alike.
4. **Part-count reduction,
   [issue #31](https://github.com/syntroniks/zoneflow/issues/31)**: the two
   candidates priced here, dropping the output snubbers and putting each zone
   LED in series with its opto-triac's LED, remove 27 parts and 54 joints and
   save **0.32 to 0.36 USD**. The second of the two does not work as it
   stands: an indicator of up to 2.4 V in series with an opto LED of up to
   1.5 V and an expander pin at 0.57 V needs 4.5 V, and the LED supply is
   3.19 V at its lowest. Even the typical figures, 1.9, 1.18 and 0.28 V, add
   up to more than 3.3 V. It would need the LED supply to come from the 5 V
   bus. Removing small passives saves little money,
   because each costs well under a cent and their part types stay on the
   board. Issue #31 is worth doing for assembly risk and board space; for cost
   the candidates that count are the big parts. The issue's "opto-triacs"
   candidate removes nine MOC3023, 2.12 USD per controller at 100 before the
   price of whatever drives the triacs instead; it also removes a protection
   layer and needs an explicit decision.

The 4-zone build saves 5.31 to 7.22 USD over the 8-zone build, far less than
ordering ten boards in place of five.

## Against other controllers

Retail prices of the open controllers the
[prior-art survey](../design/prior-art.md) cites:

<!-- zfcost:begin retail -->

| Controller | Retail price | Source |
|---|---|---|
| OpenSprinkler 4 AC (8 zones) | 239 EUR incl. VAT | docs/design/prior-art.md (opensprinklershop.de) |
| OpenSprinkler 3.4 AC (8 zones) | 150 USD at pre-order, without power supply; opensprinkler.com lists 135 to 203 USD across its range | hackster.io news on the v3.4 launch; opensprinkler.com/product/opensprinkler (search summaries, 2026-11-11) |
| OpenSprinkler Bee 3.0 (3 latching DC zones) | 62 to 72 USD | docs/design/prior-art.md |
| Smart Garden, JGAguado (4 relay zones) | 34.99 USD, sold out since 2025-06-03 | docs/design/prior-art.md |

<!-- zfcost:end -->

A builder's cost is not a retail price: a seller adds labour, testing,
certification, support, returns, a margin and, in the EU, VAT. With that in
mind:

- **At 5 units**, an 8-zone ZoneFlow costs about 118 USD landed, 32 USD
  under the 150 USD of an OpenSprinkler 3.4 AC, which arrives built and
  supported.
- **At 25 units**, 69 USD; **at 100**, 53 USD. That is materials and fees
  only; it leaves room under the OpenSprinkler prices for the costs a seller
  carries, but nothing here says how much.
- The cheaper open controllers switch relays or latching DC valves, with
  fewer protection layers and no display; they are not like for like.

## Sensitivity

<!-- zfcost:begin sensitivity -->

| 8-zone, USD per controller | 5 units | 25 units | 100 units |
|---|---|---|---|
| Base case | 117.60 | 69.00 | 52.79 |
| US duty 0 % | 91.96 | 53.30 | 40.90 |
| US duty 25 % | 110.27 | 64.51 | 49.39 |
| US duty 60 % | 135.91 | 80.21 | 61.28 |
| Display panel price x 0.5 | 115.18 | 66.94 | 51.16 |
| Display panel price x 1.5 | 120.01 | 71.05 | 54.41 |
| ESP32-S3 module price x 0.75 | 115.78 | 67.40 | 51.51 |
| ESP32-S3 module price x 1.25 | 119.41 | 70.60 | 54.06 |

<!-- zfcost:end -->

Duty moves the answer most: each 10 points of duty is about 3.4 USD per
controller at 100 units and 7.3 USD at 5. The display panel and the module
are the two parts most likely to change price or source, and neither moves
the total by more than 2.5 USD within the ranges shown.

## Supply risks

- **The display panel** (HS20HS072RX, C5329582) is a single-source part from
  a small maker, sold through JLCPCB's shop, with 334 in stock on 2026-11-12,
  three 100-unit batches, and 126 fewer than two days before. The fascia and
  carrier are drawn for it. A second panel would need its own outline check.
- **The triac** T810-600B-TR has 2,882 in stock, about three 100-unit
  batches, 502 fewer than a day before. It has no second source in the BOM.
- **The 48 V TVS** SMBJ48CA is now the Hongjiacheng part, which has 2,730 in
  stock: under two 100-unit batches, because a board takes fourteen. The
  Littelfuse part that it replaced, C151264, fits the same pads, has 5,618
  in stock and pays the 3.07 USD fee. The plan for a batch above 190 boards
  is to check the stock first and to order with the Littelfuse number if it
  is short.
- **The supervisor** TLV803EA29DBZR has 1,206 in stock. The TLV803SDBZR,
  C132016, with the same threshold on the same pads, has 34,990.
- **The buck converter** LM5164DDAR fell from 1,651 to 852 in a day.
- **Extended parts in general**: 38 of the 88 types are extended, and an
  extended part can go out of stock at JLCPCB between ordering and
  production. Pre-ordering them is possible but ties up money.
- **Tariffs** changed several times in 2026 (see above). A change of 25 points
  moves an 8-zone controller by 8 to 18 USD.
- **Prices are one day's prices.** JLCPCB's price page says prices vary with
  exchange rates; it adjusted board prices in August 2026.

## What I could not price

- **An actual JLCPCB assembly quote.** It needs a login and the files
  uploaded. The assembly fees are the published rules applied to the joint
  and part-type counts, with the open points listed under
  [assembly](#assembly).
- **The X-ray fee**, if the module's ground pad triggers it.
- **Certification tests** for selling: no lab was asked.
- **Screws and labels**: priced by assumption, not from a listing.
- **The cell's 1-piece price**: Digi-Key's table starts at 20.
- **EU duty for the tariff heading** of an assembled controller board.
- **Weights**: the parts and the packing were not weighed; freight uses
  assumed masses.

## Assumptions

Each is in `tools/cost/data/assumptions.toml` and can be changed there or with
`uv run zfcost table --set name=value`.

<!-- zfcost:begin assumptions -->

| Name | Value | What it means |
|---|---|---|
| `attrition_fields` | `true` | The JLCPCB parts search returns two unlabelled quantities per part, leastPatchNumber and lossNumber. They are read as the minimum quantity JLCPCB buys for an assembly order and the extra parts it adds for attrition: order = max(needed + loss, least). JLCPCB's FAQ says both rules exist but gives no numbers. Set false to order exactly what is fitted. |
| `tier_by_order_qty` | `true` | A part's price break is chosen by the quantity ordered for the whole batch, as on the JLCPCB parts search. |
| `service` | `auto` | JLCPCB assembly service: 'auto' uses Economic up to 50 boards (its limit) and Standard above; 'economic' or 'standard' forces one. Economic above 50 is priced as several orders of at most 50. |
| `tht_by` | `builder` | Who solders the six through-hole parts (four headers, C1, K1). 'builder': bought loose with the order and soldered by hand, as hardware-layout.md section 12 plans. 'assembler': JLCPCB's manual assembly fee per joint plus its hand-soldering labour per order, and Standard service because C1 and K1 are on the bottom side. Neither has been asked of JLCPCB. |
| `pcb_hole` | `0p3` | The smallest hole of the layout is 0.3 mm since its review (finding L9 of hardware-layout-review.md), so the default price applies. '0p2' prices the board as it was before, with twelve 0.2 mm vias in the ground pad of the module. |
| `pcb_qty_round_up` | `true` | The board quantity is the smallest quoted quantity at or above the number of controllers. Extra bare boards are spares. |
| `us_tariff_rate` | `0.35` | Duty rate on the China-made goods (parts, board, assembly). 0.35 is what JLCPCB says it collects on PCBs under DDP (Section 301 25 % plus 10 %). It changes often; the page shows 0, 0.25, 0.35 and 0.60. |
| `tariff_on_freight` | `false` | US duty is assessed on the goods' value, not on the international freight. |
| `components_g_per_board` | `45.0` | Mass of the parts on one assembled board including relay, capacitor, module and headers, not weighed. Used for the freight only. |
| `loose_g_per_unit` | `40.0` | Display panel, four plugs and spare fuse per controller, packed. Not weighed. |
| `packing_g_per_order` | `250.0` | Box and padding per shipment. |
| `spare_fuses_per_unit` | `1` | One spare 3 A fuse (0452003.MRL, the replacement named in the BOM) in each box. The fuse holders on the board come with fuses. |
| `us_distributor_shipping` | `8.0` | Shipping per order for the coin cells from a US distributor. Not quoted. |
| `petg_waste_factor` | `1.1` | Purge, skirt and failed prints over the model mass. |
| `screw_usd_each` | `0.08` | M3 stainless machine screw, bought by the hundred. Not quoted. |
| `label_usd_each` | `0.05` | One 28 x 89 mm address label from a sheet. Not quoted. |
| `fastener` | `insert` | 'insert' uses seven heat-set inserts; 'selftap' uses none (docs/enclosure/printing.md). |
| `labour_usd_per_hour` | `0.0` | Money per hour of the builder's time. 0 leaves time out of the money totals; the page lists hours separately. |
| `minutes_tht_solder` | `10.0` | Hand-soldering the six through-hole parts (26 joints) of one board. |
| `minutes_enclosure` | `25.0` | Pressing seven inserts, fitting the light pipes, display, board and fascia (docs/enclosure/assembly.md). |
| `minutes_flash_test` | `20.0` | Flashing firmware and the per-board checks of the first-power-up page, per board after the first. |
| `minutes_per_order` | `120.0` | Ordering, checking the assembler's placement preview, unpacking: per batch. |

<!-- zfcost:end -->

## Sources

All read on 2026-11-11 unless a date is given.

- JLCPCB parts search, per LCSC number, on 2026-11-12: stock, price breaks,
  library, minimum and attrition quantities
  (`tools/cost/data/jlcpcb-parts.csv`, and `replaced-parts.csv` and
  `revb-parts.csv` beside it). The same search with the library filter set
  to basic (351 parts) and to preferred (1,235 parts), read whole on
  2026-11-11 and again on 2026-11-12 for the part swaps: the same two counts
  on both days.
- For the swaps of 2026-11-12: Zhuhai Hongjiacheng, "SMBJ series", rev 1.1,
  and "SMCJ series", rev 2.2, the sheets LCSC serves for C19077591,
  C19077560 and C41411765; Littelfuse SMBJ and SMCJ series sheets, for
  C151264, C83270 and C151287; Brightking P6SMB series, revision of
  2014-07-21, for C78395; Hubei KENTO KT-0603R approval sheet A.0 of
  2018-12-06, for C2286; TI SLVSES2J as LCSC's copy ZHCSJX8J, for C1852114;
  Hongjiacheng SRV05-4, rev 2.2, for C7420376, and ST USBLC6-2, Doc ID 11265
  rev 5; Advanced Monolithic Systems AMS1117, for C6186; TI TCA9539 for the
  sink current limits; UNI-ROYAL chip resistor arrays, for C29718. JLCEDA
  library footprints for the three new numbers that are placed.
- For the swaps: Zhuhai Hongjiacheng, "BAT54 thru BAT54S", rev 2.0, the sheet
  LCSC serves for C7420333. Fenghua, general specification of multilayer
  chip capacitors, the sheet LCSC serves for C1604, C57112 and C53987.
  Samsung Electro-Mechanics MLCC catalogue, the sheet for C1589 and C1664.
  NationStar NCD0603Y2 and Lite-On LTST-C190KGKT sheets for the LED that was
  not swapped. JLCEDA library footprints for the five new numbers.
- JLCPCB online quote page, cart.jlcpcb.com/quote: board prices and US
  freight.
- JLCPCB, [PCB assembly price](https://jlcpcb.com/help/article/pcb-assembly-price),
  last updated 2026-10-22. Read again on 2026-11-12: unchanged.
- JLCPCB, [PCB assembly capabilities](https://jlcpcb.com/capabilities/pcb-assembly-capabilities).
- JLCPCB, [PCB assembly FAQs](https://jlcpcb.com/help/article/pcb-assembly-faqs),
  last updated 2026-09-09, read again on 2026-11-12:
  preferred extended parts exempt from the Economic feeder fee.
- JLCPCB, [US tariff policy FAQ](https://jlcpcb.com/help/article/us-tariff-policy-faq),
  last updated 2026-10-29.
- Congressional Research Service, fact sheet R49481, "Active Presidential
  Tariff Actions in 2026", 2026-10-08.
- European Commission,
  [guidance on the temporary flat fee for low-value imports](https://taxation-customs.ec.europa.eu/news/guidance-and-legal-text-temporary-flat-fee-low-value-imports-which-will-apply-until-1-july-2028-2026-06-08_en),
  2026-06-08.
- [47 CFR 15.23](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-A/section-15.23),
  home-built devices.
- FCC KDB 996369 on modular transmitters, as quoted in module makers'
  integration guides; FCC ID 2AC7Z-ESPS3WROOM1 test reports at fcc.report.
- Espressif certificate index, ESP32-S3-WROOM-1 CE certificate.
- Digi-Key P189-ND (Panasonic CR2032); Polymaker PETG; ruthex M3 inserts at
  3DJake; Hunter 526500; 18/8 irrigation wire at three US sellers; Rigol
  DHO804 at US dealers: prices from search-result summaries, not from the
  pages themselves.
- The board: [pull request #39](https://github.com/syntroniks/zoneflow/pull/39),
  branch `hw/rev-a-layout` at 7ec8e75.

## Redoing it

```sh
cd tools/cost
uv run zfcost fetch        # new JLCPCB parts snapshot (network)
uv run zfcost fetch --side # the replaced and the revision B part numbers
uv run zfcost table --qty 5 50 --set us_tariff_rate=0.25
uv run zfcost render ../../docs/hardware/cost.md
uv run pytest -q
```

The board and freight quotes, the price list and the other sourced figures
are in `tools/cost/data/sourced.toml`, each with its source and date; the
[tool's README](https://github.com/syntroniks/zoneflow/tree/main/tools/cost)
says how to refresh them. CI fails if this page and the data disagree.
