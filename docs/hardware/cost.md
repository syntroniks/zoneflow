# What a controller costs

!!! warning "An estimate, dated 2026-11-11. Not a quote."
    No ZoneFlow board has been ordered, built or tested. Every number on this
    page is calculated from prices and price lists read on **2026-11-11**,
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
| 8-zone, board landed | 111.40 | 82.58 | 64.79 | 48.57 |
| 8-zone, with enclosure | 121.28 | 89.72 | 70.62 | 53.76 |
| 4-zone, board landed | 104.15 | 74.83 | 58.16 | 42.83 |
| 4-zone, with enclosure | 114.03 | 81.97 | 63.99 | 48.01 |

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
| 8-zone | 606.38 | 897.22 | 1,765.51 | 5,375.55 |
| 4-zone | 570.13 | 819.70 | 1,599.84 | 4,800.73 |

<!-- zfcost:end -->

### Where the money goes

<!-- zfcost:begin breakdown-8 -->

| 8-zone, USD per controller | 5 units | 10 units | 25 units | 100 units |
|---|---|---|---|---|
| Parts fitted by JLCPCB | 38.08 | 34.46 | 31.87 | 25.62 |
| Parts bought loose: headers, C1, K1, plugs, display, spare fuse | 6.45 | 5.73 | 5.57 | 4.43 |
| Coin cell (US distributor) | 2.74 | 1.37 | 0.61 | 0.37 |
| Bare board | 6.56 | 4.05 | 2.55 | 1.65 |
| Assembly fees | 24.88 | 13.12 | 6.06 | 2.97 |
| &nbsp;&nbsp;of which for extended parts | 21.49 | 10.74 | 4.30 | 0.60 |
| Freight to the US | 6.10 | 3.77 | 2.01 | 1.40 |
| US duty | 26.59 | 20.08 | 16.12 | 12.14 |
| Enclosure materials | 9.87 | 7.14 | 5.83 | 5.18 |
| **Total per controller** | **121.28** | **89.72** | **70.62** | **53.76** |
| Assembly service | economic 5 | economic 10 | economic 25 | standard 100 |
| Boards ordered | 5 | 10 | 25 | 100 |

<!-- zfcost:end -->

<!-- zfcost:begin breakdown-4 -->

| 4-zone, USD per controller | 5 units | 10 units | 25 units | 100 units |
|---|---|---|---|---|
| Parts fitted by JLCPCB | 33.25 | 29.26 | 27.50 | 21.79 |
| Parts bought loose: headers, C1, K1, plugs, display, spare fuse | 6.08 | 5.37 | 5.20 | 4.14 |
| Coin cell (US distributor) | 2.74 | 1.37 | 0.61 | 0.37 |
| Bare board | 6.56 | 4.05 | 2.55 | 1.65 |
| Assembly fees | 24.71 | 12.94 | 5.89 | 2.82 |
| &nbsp;&nbsp;of which for extended parts | 21.49 | 10.74 | 4.30 | 0.60 |
| Freight to the US | 6.10 | 3.77 | 2.01 | 1.40 |
| US duty | 24.71 | 18.07 | 14.40 | 10.65 |
| Enclosure materials | 9.87 | 7.14 | 5.83 | 5.18 |
| **Total per controller** | **114.03** | **81.97** | **63.99** | **48.01** |
| Assembly service | economic 5 | economic 10 | economic 25 | standard 100 |
| Boards ordered | 5 | 10 | 25 | 100 |

<!-- zfcost:end -->

### Reading the tables

- **At 5 units, fixed charges per order are the largest share.** The board's
  engineering fee, the assembly setup and stencil, and above all the
  extended-part fees are charged once per order whatever its size:
  142.16 USD. With the duty on them they are about 38 USD per controller,
  32 % of the 121 USD. Parts are 47 USD, 39 %.
- **At 100 units, parts and duty dominate.** Parts are 30 of the 54 USD and
  duty 12. Board and assembly fees together have fallen to 4.62 USD.
- **Extended-part fees are the largest single fixed charge.** JLCPCB's
  Economic assembly charges 3.07 USD for each extended part type that is not
  "preferred"; this board has 35 such types, so every order pays 107.45 USD
  before it fits a part. On five boards that is 21 USD each, 29 USD with
  duty, a quarter of the total. It had 40 until five were replaced:
  [fewer extended part types](#fewer-extended-part-types). Standard assembly, which the model uses
  above 50 boards, charges 1.53 USD for every type, basic or extended.

<!-- zfcost:begin types -->

| Build | Parts placed | Joints | Part types | Basic | Preferred extended | Extended, fee charged |
|---|---|---|---|---|---|---|
| 8-zone | 312 | 850 | 89 | 50 | 4 | 35 |
| 4-zone | 267 | 740 | 89 | 50 | 4 | 35 |

<!-- zfcost:end -->

<!-- zfcost:begin extfee -->

| Units | Service | Extended-part fees, USD per batch | With duty, USD per controller | Share of the total |
|---|---|---|---|---|
| 5 | economic | 107.45 | 29.01 | 24 % |
| 10 | economic | 107.45 | 14.51 | 16 % |
| 25 | economic | 107.45 | 5.80 | 8 % |
| 100 | standard | 59.67 | 0.81 | 1 % |

<!-- zfcost:end -->

- **US duty is the second-largest line at every quantity.** At the 35 % that
  JLCPCB says it collects on boards, it adds 35 % to everything made in
  China, fees included: 27 USD of the 121 at 5 units, 12 of the 54 at 100.
- **The 4-zone build saves only 5.75 to 7.75 USD.** It drops 45 parts, but every
  part type stays (zones 1 to 4 use the same types as 5 to 8), so it pays
  the same per-order fees and the same board.
- **Batch size is the biggest lever a builder controls.** Five boards cost
  51 USD more each than 25 boards.

## Components

**Method.** Every line of
[`hardware/bom-preliminary.csv`](https://github.com/syntroniks/zoneflow/blob/main/hardware/bom-preliminary.csv)
was priced again on 2026-11-11 from the JLCPCB parts search: stock, price
breaks, basic or extended, preferred or not. The snapshot is
`tools/cost/data/jlcpcb-parts.csv`. For a part JLCPCB fits, the number
bought is the number fitted plus JLCPCB's attrition quantity for that part,
and at least its minimum assembly quantity; the price break is the one that
total reaches. Both quantities come from the parts search; reading them as
attrition and minimum is an [assumption](#assumptions). They add 8.46 USD to
a batch of five (1.69 per controller) and 6.68 USD to a batch of a hundred
(under 0.07 per controller): the minimums are 20 or fewer, and the parts they
apply to cost a cent or less.

Which parts JLCPCB fits comes from the [layout](layout.md) as it stands
after its [review](../design/hardware-layout-review.md): its placement files
hold 312 parts for the 8-zone build and 267 for the 4-zone build, all SMT on
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
| T810-600B-TR | 4.71 | 3.75 |
| ESP32-S3-WROOM-1-N16R8 | 4.95 | 3.35 |
| HS20HS072RX | 3.58 | 2.41 |
| 0154003.DRT | 3.05 | 2.37 |
| MOC3023SR2M | 3.75 | 2.12 |
| EC11J1525402 | 3.05 | 1.80 |
| LM5164DDAR | 2.56 | 1.67 |
| SMBJ48CA | 1.82 | 1.49 |
| BSMD2920-100-60V | 2.18 | 1.46 |
| TCA9539PWR | 1.22 | 0.79 |

<!-- zfcost:end -->

The nine output channels carry the three most expensive families of the
board: nine triacs, nine opto-triacs and fourteen 48 V TVS diodes.

**Stock.** No line was out of stock or below what 100 eight-zone controllers
need. The thinnest, as the number of 100-unit batches JLCPCB had in stock:

<!-- zfcost:begin stock -->

| Part | LCSC | Library | JLCPCB stock | Needed for 100 | Batches in stock | Flag |
|---|---|---|---|---|---|---|
| T810-600B-TR | C155588 | extended | 3,384 | 900 | 3.8 | thin: under 5 batches |
| SMBJ48CA | C151264 | extended | 5,782 | 1,402 | 4.1 | thin: under 5 batches |
| HS20HS072RX | C5329582 | extended | 413 | 100 | 4.1 | thin: under 5 batches |
| KF2EDGR-3.81-7P | C441187 | extended | 1,606 | 200 | 8.0 |  |
| KF2EDGK-3.81-7P | C440863 | extended | 1,827 | 200 | 9.1 |  |
| 0154003.DRT | C206913 | extended | 2,514 | 200 | 12.6 |  |
| EC11J1525402 | C209762 | extended | 1,486 | 100 | 14.9 |  |
| 1812L020/60DR | C315900 | extended | 1,590 | 100 | 15.9 |  |
| LM5164DDAR | C477928 | extended | 1,651 | 100 | 16.5 |  |
| MOC3023SR2M | C40701 | extended | 15,015 | 900 | 16.7 |  |

<!-- zfcost:end -->

The display panel had 460 on 2026-11-10 and 413 a day later.

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
53.76 USD per controller and two Economic orders of 50 to 60.42 USD, because
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

Joints are counted from the layout: 850 soldered pins on the 8-zone board and
740 on the 4-zone board, counting each pin once.

**Through-hole parts.** The layout plans to solder C1 and K1 by hand from
the top and leaves all six through-hole parts out of the placement files. The
base case keeps that plan. JLCPCB could fit them by hand for its manual
assembly fee, but C1 and K1 sit on the bottom side, so the order becomes a
double-sided Standard order. Nobody has asked JLCPCB whether it would. Both
options, by the published rules:

<!-- zfcost:begin tht -->

| 8-zone, USD per controller (builder minutes per controller) | 5 units | 25 units | 100 units |
|---|---|---|---|
| Builder solders them | 121.28 (79 min) | 70.62 (60 min) | 53.76 (56 min) |
| JLCPCB fits them (Standard service) | 141.67 (69 min) | 75.16 (50 min) | 54.93 (46 min) |

<!-- zfcost:end -->

**What the published rules do not settle.** The exact assembly price needs a
login and the uploaded files, so these are the published rules applied to the
board, not a quote. The open points: whether the module's ground pad triggers
JLCPCB's X-ray fee (1.64 down to 0.08 USD per part by quantity; not in the
totals); whether Standard also charges the double-sided setup (51.12) when
the bottom side has only through-hole parts; whether the solder-joint count
JLCPCB makes from the files equals the 850 counted here; and how the
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
| Economic extended-part fees, 8-zone | 107.45 | Every order |
| Bare boards, 5 pieces, smallest hole 0.3 mm | 32.80 | Every order; 25.00 of it is the engineering fee |
| Prototype run: 5 eight-zone controllers, landed, with enclosures | 606.38 | Once |

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

## Fewer extended part types

Revision A had 40 part types that pay JLCPCB's Economic feeder fee. On
2026-11-11 each of the 40 was looked up again in JLCPCB's parts library, for a
basic part or a preferred extended part **in the same footprint** that meets
every figure the design relies on. The rule was: no change to what the
circuit does, no footprint change, no new layout. Five types changed, so 35
are left.

The library was read whole, not searched by keyword: the parts search
returned 351 basic parts and 1,235 preferred extended parts that day, and
every candidate below was matched against those two lists.

<!-- zfcost:begin consolidation -->

| Build | Units | Service | Fee-carrying types | Extended-part fees, USD per batch | Saved per batch, USD, with duty and part prices | USD per controller | Saved per controller, USD |
|---|---|---|---|---|---|---|---|
| 8-zone | 5 | economic | 40 to 35 | 122.80 to 107.45 | 20.92 | 125.46 to 121.28 | 4.18 |
| 8-zone | 10 | economic | 40 to 35 | 122.80 to 107.45 | 21.12 | 91.83 to 89.72 | 2.11 |
| 8-zone | 25 | economic | 40 to 35 | 122.80 to 107.45 | 21.74 | 71.49 to 70.62 | 0.87 |
| 8-zone | 100 | standard | 40 to 35 | 65.79 to 59.67 | -0.71 | 53.75 to 53.76 | -0.01 |
| 4-zone | 5 | economic | 40 to 35 | 122.80 to 107.45 | 20.92 | 118.21 to 114.03 | 4.18 |
| 4-zone | 10 | economic | 40 to 35 | 122.80 to 107.45 | 21.12 | 84.08 to 81.97 | 2.11 |
| 4-zone | 25 | economic | 40 to 35 | 122.80 to 107.45 | 21.74 | 64.86 to 63.99 | 0.87 |
| 4-zone | 100 | standard | 40 to 35 | 65.79 to 59.67 | -0.71 | 48.00 to 48.01 | -0.01 |

<!-- zfcost:end -->

The table is the model run twice: once on the board as it is now and once
with the five old part numbers put back
(`tools/cost/data/replaced-parts.csv`, fetched the same day). The fees fall
by 5 x 3.07 = 15.35 USD per Economic order. The batch saves a little more
than that with duty, and a few cents come from part prices. At 100 units
there is no saving, because Standard assembly charges 1.53 USD for every
type, basic or extended; the new parts cost a cent more per board there.

### What was swapped

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

### What was not swapped, and why

**Protection parts, 11 types.** Left alone unless the same maker's part
number is basic, and none is: the fuses 0154003.DRT and 0451.500MRL, the
PTCs 1206L012WR, 1812L020/60DR and BSMD2920-100-60V, the triac
T810-600B-TR, the opto-triac MOC3023SR2M, and the clamps SMCJ48CA, SMBJ48CA,
SMBJ6.0A and USBLC6-2SC6. The design's surge and short-circuit sums rest on
these makers' curves.

**No basic or preferred part of the kind, 13 types.** LM5164DDAR,
SRN6045TA-470M, ESP32-S3-WROOM-1-N16R8, TCA9539PWR, TLV803EA30DBZR,
TPS3701DDCR, TPS2553DBVR, the USB-C, Qwiic and display connectors, the cell
holder, the encoder, and the 6 x 6 mm tactile switch. The two lists hold no
connector, no power inductor and no part of these functions; the two basic
tactile switches are 5.1 x 5.1 mm and 4 x 3 mm.

**A near part exists and fails on a figure, 11 types.**

| Type | Nearest basic or preferred part | Why not |
|---|---|---|
| LDL1117S33R, 3.3 V regulator | AMS1117-3.3, C6186, basic, same SOT-223 pins | Drops 1.1 V at 0.8 A by its listing. On USB power the input falls to 4.1 V, which leaves 0.8 V |
| LM2901DR, quad comparator | LM393, C7955, basic | Two comparators in SOIC-8, 0 to 70 °C. The board needs four, in SOIC-14, from -20 °C |
| 74HC132D, quad Schmitt NAND | 74HC14D, C5605, basic | A hex inverter. It cannot make the latch |
| CL32B225KCJSNNE, 2.2 uF 100 V, 1210 | None | No 1210 capacitor is basic or preferred |
| SR2512JK-073RL, 3 ohm surge resistor, and RLP25FEER100, 0.1 ohm shunt | None | No 2512 resistor is basic or preferred. The basic 0.1 ohm is a 1206 thick-film part, 0.25 W and 800 ppm/K |
| CL21B103KCANNNC, 10 nF 100 V, 0805: nine snubbers and C48 | 10 nF 50 V, C1710, basic | The snubber sees 45 V peak at 32 VAC and up to the 77 V of the clamp. The only basic 100 V part in 0805 is 100 nF, ten times the snubber current |
| 1206W4F330JT5E, 33 ohm, backlight | 22 ohm 1206, C17958, basic | 89 mA into a backlight rated 80 mA. The basic 33 ohm parts are 0805 and 0603: 0.125 W against the 0.16 W it dissipates |
| 1206W4F470KT5E, 4.7 ohm, sensor supply | 1 ohm and 10 ohm 1206, basic | 10 ohm doubles the drop to the sensor terminal. 1 ohm lets five times the current into D24 in a miswire, where the margin is small already (design section 13.5) |
| 4D03WGJ0104T5E, 100 kohm x4 | 10 kohm x4, C29718, basic | Each element limits what one fault can push into a valve pin or the LED supply to 33 uA, far under the 0.25 mA at which the fail-safe analysis counts an opto-triac as fired. 10 kohm lets 0.2 to 0.33 mA through. It would need the fault-injection runs redone and their margins judged again |
| LTST-C190KGKT, yellow-green LED | NCD0603Y2, yellow, C89811, preferred, 129,644 | Another colour, and its forward voltage may be 2.6 V where this one stops at 2.4 V. It is brighter (50 mcd minimum at 15 mA against 18 at 20 mA) and would work. The colour of the front panel is the owner's choice |

**Values that could share a type.** Merging two basic values saves nothing
on an Economic order and 1.53 USD on a Standard one. I looked at the pairs
that serve one purpose and merged none:

- **Pull-ups of 3.3 kohm (five controls) into 2.2 kohm.** The encoder would get 1.5 mA, inside its 1 to 10 mA. But these pull-ups are in the fail-safe bridge analysis: a control net bridged to the keep-alive pin is modelled as 13.3 kohm to 3V3 (`hardware/sim/failsafe/faults.py`). A change means those runs again.
- **Pull-ups of 4.7 kohm on the Qwiic bus into 2.2 kohm.** Boards plugged in bring their own pull-ups, and the sum must stay above 1 kohm. The third 4.7 kohm part, R92, is in the gate-drive read-back.
- **22 nF on the three sensor inputs into 10 nF.** It would halve the filter time on the flow input for no saving that counts.
- **SS34 and SS210, M7.** Different voltage ratings for different places.
- **LED resistors.** Already one value, 680 ohm; the red fault LED uses the 1 kohm that 19 other places use.

### What a design change could still remove

For [issue #31](https://github.com/syntroniks/zoneflow/issues/31). Each row
is one fee-carrying type, 3.07 USD per Economic order.

| Type | Change it needs |
|---|---|
| Yellow-green LED | A decision to make the lights yellow: NCD0603Y2 is preferred extended and fits the footprint and the 680 ohm resistors |
| 100 kohm x4 arrays | 10 kohm arrays, with the fail-safe fault runs redone, or twelve basic 100 kohm 0603 resistors on a changed layout |
| SMBJ48CA, SMCJ48CA, SMBJ6.0A | A second maker qualified for each clamp. Hongjiacheng parts of these names are preferred extended (2,619, 14,082 and 174,679 in stock; the first is thin). Their clamping curves would have to be read against the sums of design sections 4.4, 4.8 and 6.6 |
| USBLC6-2SC6 | SRV05-4 is preferred extended and its pins suit this wiring. The same qualification |
| 10 nF 100 V, 0805 | Dropping the snubbers, the issue's own candidate, removes nine of the ten. The tenth, C48, could then be the basic 100 nF 100 V part |
| 33 ohm 1206 | Two basic 1206 resistors in series, 22 and 10 ohm, on a changed layout |
| 4.7 ohm 1206 | Two basic 10 ohm 1206 resistors in parallel, on a changed layout |
| 6 x 6 mm tactile switch | The basic 5.1 mm switch that SW1 and SW2 use, with longer button caps in the enclosure |
| 0.1 ohm shunt, 3 ohm surge resistor, 2.2 uF 100 V | None found: the ratings are the reason for the parts |

The scenario `needs_decision` in the next section prices the first four rows
(six types) together.

## Four cheap ways to cut cost

Ordered from least work to most. Each saving is the model run with the
change, against the base case, for the 8-zone build.

<!-- zfcost:begin cuts -->

| Scenario | Change | Saves USD per controller at 5 | Saves USD per controller at 25 | Saves USD per controller at 100 |
|---|---|---|---|---|
| `selftap` | Self-tapping screws in place of the seven heat-set inserts | 1.67 | 0.67 | 0.58 |
| `needs_decision` | Six more extended types moved to a basic or preferred part in the same footprint; each needs a decision | 4.97 | 0.99 | 0.00 |
| `issue31` | Issue #31: no output snubbers; zone LED in series with the opto-triac LED | 0.34 | 0.36 | 0.32 |

<!-- zfcost:end -->

<!-- zfcost:begin batch -->

| Build | 5 units | 25 units | Saved 5 to 25 | 100 units | Saved 25 to 100 |
|---|---|---|---|---|---|
| 8-zone | 121.28 | 70.62 | 50.65 | 53.76 | 16.86 |
| 4-zone | 114.03 | 63.99 | 50.03 | 48.01 | 15.99 |

<!-- zfcost:end -->

1. **Order more at once** (no design change). Going from 5 to 25 boards
   saves 51 USD per controller, more than all the design changes together.
   Five friends ordering one batch of 25 pay less each than one person
   ordering five.
2. **Self-tapping screws** (`fastener = "selftap"` in the enclosure model, no
   design change): 0.58 to 1.67 USD. The printing page says this suits a unit
   that is opened a few times.
3. **Fewer extended part types** (BOM change only). The swaps that change
   nothing in the circuit are made: five types,
   [above](#fewer-extended-part-types). Of the seven passives this page
   first named, four are among them; the other three (10 nF 100 V in 0805,
   33 ohm and 4.7 ohm in 1206) have no basic part in their footprint. Six
   more types have a basic or preferred part that fits, and each needs a
   decision first: the colour of the lights, the pull-up arrays of the
   fail-safe circuit, a second maker for three clamps and for the USB
   protection. Together they are 4.97 USD per controller at 5 boards and
   nothing at 100, where Standard charges every type alike.
4. **Part-count reduction,
   [issue #31](https://github.com/syntroniks/zoneflow/issues/31)**: the two
   candidates priced here, dropping the output snubbers and putting each zone
   LED in series with its opto-triac's LED, remove 27 parts and 54 joints and
   save **0.32 to 0.36 USD**. Removing small passives saves little money,
   because each costs well under a cent and their part types stay on the
   board. Issue #31 is worth doing for assembly risk and board space; for cost
   the candidates that count are the big parts. The issue's "opto-triacs"
   candidate removes nine MOC3023, 2.12 USD per controller at 100 before the
   price of whatever drives the triacs instead; it also removes a protection
   layer and needs an explicit decision.

The 4-zone build saves 5.75 to 7.75 USD over the 8-zone build, far less than
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

- **At 5 units**, an 8-zone ZoneFlow costs about 121 USD landed, 29 USD
  under the 150 USD of an OpenSprinkler 3.4 AC, which arrives built and
  supported.
- **At 25 units**, 71 USD; **at 100**, 54 USD. That is materials and fees
  only; it leaves room under the OpenSprinkler prices for the costs a seller
  carries, but nothing here says how much.
- The cheaper open controllers switch relays or latching DC valves, with
  fewer protection layers and no display; they are not like for like.

## Sensitivity

<!-- zfcost:begin sensitivity -->

| 8-zone, USD per controller | 5 units | 25 units | 100 units |
|---|---|---|---|
| Base case | 121.28 | 70.62 | 53.76 |
| US duty 0 % | 94.69 | 54.50 | 41.62 |
| US duty 25 % | 113.68 | 66.02 | 50.29 |
| US duty 60 % | 140.27 | 82.13 | 62.42 |
| Display panel price x 0.5 | 118.86 | 68.57 | 52.13 |
| Display panel price x 1.5 | 123.69 | 72.67 | 55.38 |
| ESP32-S3 module price x 0.75 | 119.61 | 69.17 | 52.63 |
| ESP32-S3 module price x 1.25 | 122.95 | 72.07 | 54.89 |

<!-- zfcost:end -->

Duty moves the answer most: each 10 points of duty is about 3.5 USD per
controller at 100 units and 7.6 USD at 5. The display panel and the module
are the two parts most likely to change price or source, and neither moves
the total by more than 2.5 USD within the ranges shown.

## Supply risks

- **The display panel** (HS20HS072RX, C5329582) is a single-source part from
  a small maker, sold through JLCPCB's shop, with 413 in stock on 2026-11-11,
  four 100-unit batches, and 47 fewer than the day before. The fascia and
  carrier are drawn for it. A second panel would need its own outline check.
- **The triac** T810-600B-TR has 3,384 in stock, under four 100-unit
  batches, and is the costliest line. It has no second source in the BOM.
- **The 48 V TVS** SMBJ48CA has about four 100-unit batches in stock. It is
  a common part from several makers, so this is a matter of choosing a
  second LCSC number, not a redesign.
- **Extended parts in general**: 39 of the 89 types are extended, and an
  extended part can go out of stock at JLCPCB between ordering and
  production. Pre-ordering them is possible but ties up money.
- **Tariffs** changed several times in 2026 (see above). A change of 25 points
  moves an 8-zone controller by 9 to 19 USD.
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

- JLCPCB parts search, per LCSC number: stock, price breaks, library, minimum
  and attrition quantities (`tools/cost/data/jlcpcb-parts.csv`). The same
  search with the library filter set to basic (351 parts) and to preferred
  (1,235 parts), read whole for the part swaps.
- For the swaps: Zhuhai Hongjiacheng, "BAT54 thru BAT54S", rev 2.0, the sheet
  LCSC serves for C7420333. Fenghua, general specification of multilayer
  chip capacitors, the sheet LCSC serves for C1604, C57112 and C53987.
  Samsung Electro-Mechanics MLCC catalogue, the sheet for C1589 and C1664.
  NationStar NCD0603Y2 and Lite-On LTST-C190KGKT sheets for the LED that was
  not swapped. JLCEDA library footprints for the five new numbers.
- JLCPCB online quote page, cart.jlcpcb.com/quote: board prices and US
  freight.
- JLCPCB, [PCB assembly price](https://jlcpcb.com/help/article/pcb-assembly-price),
  last updated 2026-10-22.
- JLCPCB, [PCB assembly capabilities](https://jlcpcb.com/capabilities/pcb-assembly-capabilities).
- JLCPCB, [PCB assembly FAQs](https://jlcpcb.com/help/article/pcb-assembly-faqs):
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
uv run zfcost table --qty 5 50 --set us_tariff_rate=0.25
uv run zfcost render ../../docs/hardware/cost.md
uv run pytest -q
```

The board and freight quotes, the price list and the other sourced figures
are in `tools/cost/data/sourced.toml`, each with its source and date; the
[tool's README](https://github.com/syntroniks/zoneflow/tree/main/tools/cost)
says how to refresh them. CI fails if this page and the data disagree.
