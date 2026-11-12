# SPDX-License-Identifier: Apache-2.0
"""The walkthrough page and the bench sheet as HTML, from the content and the manifest.

Needs Python and the ``markdown`` package, which MkDocs brings. The page works
without JavaScript as one long document: every step, finding and tick box is in
the HTML. ``docs/hardware/review/review.js`` turns it into a guided sequence.
"""

from __future__ import annotations

import csv
import html
import json
import re
from pathlib import Path

import markdown

from . import spec as specmod
from .boardtext import sha256

REPO_URL = "https://github.com/syntroniks/zoneflow"
_LINK = re.compile(r"\]\((doc|out|repo):([^)#\s]+)(#[^)\s]*)?\)")
# The value an answer is stored under, and what it is called on a check and on a decision.
ANSWERS = {"check": (("pass", "Pass"), ("fail", "Fail"), ("unsure", "Not sure")),
           "decision": (("pass", "Accept"), ("fail", "Reject"), ("unsure", "Not sure"))}
SECTIONS = (("check", "What to check"), ("why", "Why it matters"), ("good", "What correct looks like"),
            ("bad", "What a failure looks like"), ("fact", "From the datasheet"), ("how", "How to do it"),
            ("cannot", "What the picture cannot show"))
STATUS_LABEL = {"fixed": "Fixed", "open": "Left for a person", "accepted": "Accepted as it is", "no-defect": "No defect"}


def esc(text) -> str:
    return html.escape(str(text), quote=True)


def plain(text) -> str:
    """Markdown code marks off, for a place that shows text only."""
    return str(text).replace("`", "")


class Builder:
    """Renders for one page. ``root`` is the relative URL from that page to the site root."""

    def __init__(self, spec, manifest, root, board=specmod.BOARD, commit="unknown"):
        self.commit = commit
        self.spec = spec
        self.manifest = manifest
        self.root = root
        self.img = root + "hardware/review/img/"
        self.board_hash = sha256(board)
        self.groups = {g["id"]: g for g in spec["group"]}
        self.steps = [s for g in spec["group"] for s in spec["step"] if s["group"] == g["id"]]
        self.findings = {f["id"]: f for f in spec["finding"]}

    # Text ----------------------------------------------------------------------------------------

    def url(self, kind, path, fragment=""):
        if kind == "repo":
            return f"{REPO_URL}/blob/main/{path}{fragment}"
        if kind == "out":
            return f"{self.root}hardware/zoneflow-a/{path}{fragment}"
        page = path[:-3] if path.endswith(".md") else path
        page = page[:-5] if page.endswith("index") else page + "/"
        return f"{self.root}{page}{fragment}"

    def md(self, text) -> str:
        text = _LINK.sub(lambda m: f"]({self.url(m.group(1), m.group(2), m.group(3) or '')})", str(text).strip())
        return markdown.markdown(text, extensions=["tables", "fenced_code", "sane_lists"])

    def inline(self, text) -> str:
        out = self.md(text)
        return out[3:-4] if out.startswith("<p>") and out.endswith("</p>") and out.count("<p>") == 1 else out

    # Pictures ------------------------------------------------------------------------------------

    def figure(self, owner, fig) -> str:
        if "asset" in fig:
            name, w, h = fig["asset"], 142, 160
        else:
            name = specmod.figure_name(owner, fig)
            meta = self.manifest["figures"][name]
            w, h = meta["width"], meta["height"]
        # A drawn picture can be read again as SVG when it is enlarged: its shapes carry their nets and parts.
        svg = "" if "asset" in fig else f' data-svg="{self.img}{name}"'
        return (f'<figure class="zf-fig"><button type="button" class="zf-zoom"{svg} aria-label="Enlarge the picture: {esc(fig["alt"])}">'
                f'<img src="{self.img}{name}" alt="{esc(fig["alt"])}" width="{w}" height="{h}" loading="lazy"></button>'
                f'<figcaption>{self.inline(fig["caption"])}</figcaption></figure>')

    # Steps ---------------------------------------------------------------------------------------

    def kicad_box(self, step) -> str:
        k = step.get("kicad")
        if not k:
            return ""
        rows = []
        if k.get("find"):
            rows.append(("Find (Ctrl+F)", ", ".join(f"<code>{esc(r)}</code>" for r in k["find"])))
        if k.get("nets"):
            rows.append(("Highlight net", ", ".join(f"<code>{esc(n)}</code>" for n in k["nets"])))
        if k.get("layers"):
            rows.append(("Show layers", ", ".join(esc(n) for n in k["layers"])))
        if k.get("at"):
            x, y = k["at"]
            ox, oy = self.manifest["board"]["kicad_origin"]
            rows.append(("Position", f"({x:g}, {y:g}) in the walkthrough's coordinates. "
                                     f"KiCad's default display shows ({ox + x:g}, {oy - y:g})"))
        body = "".join(f"<tr><th scope=\"row\">{a}</th><td>{b}</td></tr>" for a, b in rows)
        return (f'<table class="zf-kicad"><caption>In KiCad: <code>hardware/zoneflow-a/zoneflow-a.kicad_pcb</code></caption>'
                f"{body}</table>")

    def ibom_link(self, step) -> str:
        i = step.get("ibom")
        if not i:
            return ""
        if "ref" in i:
            query, what = f"ref={esc(i['ref'])}", f"part <code>{esc(i['ref'])}</code>"
        else:
            query, what = f"net={esc(i['net'])}", f"net <code>{esc(i['net'])}</code>"
        return (f'<p class="zf-ibom"><a href="{self.root}hardware/review/find.html?{query}" target="_blank" rel="noopener">'
                f"Open the interactive BOM at {what}</a>. It opens in a new tab and needs no KiCad.</p>")

    def step(self, step, number) -> str:
        group = self.groups[step["group"]]
        sid = step["id"]
        parts = [f'<article class="zf-step" id="step-{sid}" data-step="{sid}" data-group="{group["id"]}" '
                 f'data-title="{esc(step["title"])}" data-group-title="{esc(group["title"])}" tabindex="-1">',
                 f'<header><p class="zf-kicker">{esc(group["title"])} <span aria-hidden="true">/</span> step {number} of '
                 f'{len(self.steps)} <span aria-hidden="true">/</span> about {int(step.get("minutes", 5))} min</p>'
                 f'<h3>{self.inline(step["title"])}</h3></header>', '<div class="zf-cols"><div class="zf-text">']
        for key, title in SECTIONS:
            if not str(step.get(key, "")).strip():
                continue
            extra = self.kicad_box(step) + self.ibom_link(step) if key == "how" else ""
            parts.append(f'<section class="zf-sec zf-{key}"><h4>{title}</h4>{self.md(step[key])}{extra}</section>')
        cards = " ".join(f'<a href="#finding-{f}">{f}</a>' for f in step.get("findings", []))
        parts.append(f'<p class="zf-source"><strong>Where this step comes from.</strong> {self.inline(step["source"])}.'
                     + (f" Finding cards: {cards}." if cards else "") + "</p>")
        parts.append('</div><div class="zf-figs">')
        figs = step.get("figure", [])
        parts.extend(self.figure(sid, fig) for fig in figs)
        if not figs:
            parts.append('<p class="zf-nofig">No picture for this step. The text says why.</p>')
        parts.append("</div></div>")
        kind = step.get("kind", "check")
        radios = "".join(
            f'<label class="zf-radio zf-radio-{value}"><input type="radio" name="answer-{sid}" value="{value}"> {label}</label>'
            for value, label in ANSWERS[kind])
        options = "".join(f'<option value="{esc(r["id"])}">{esc(plain(r["text"]))}</option>' for r in specmod.step_remedies(self.spec, step))
        legend = "Your decision on this step" if kind == "decision" else "Your answer for this step"
        failed = "rejected" if kind == "decision" else "failed"
        parts.append(f'<fieldset class="zf-answer"><legend>{legend}</legend><div class="zf-radios">{radios}'
                     f'<button type="button" class="zf-clear-one" hidden>Clear</button></div>'
                     f'<div class="zf-remedy" hidden><label class="zf-note-label" for="remedy-{sid}">This step {failed}. What should be '
                     f'done about it? This goes to the agent with the hand-off file</label>'
                     f'<select id="remedy-{sid}" class="zf-remedy-select"><option value="">Let the agent propose a remedy</option>'
                     f'{options}<option value="own">Something else, in my own words</option></select>'
                     f'<div class="zf-remedy-own" hidden><label class="zf-note-label" for="remedy-text-{sid}">The remedy in your words</label>'
                     f'<input type="text" id="remedy-text-{sid}" class="zf-remedy-text" maxlength="1000"></div></div>'
                     f'<label class="zf-note-label" for="note-{sid}">Note, for the report</label>'
                     f'<textarea id="note-{sid}" class="zf-note" rows="2" '
                     f'placeholder="What you saw, what you measured, what you want changed"></textarea></fieldset>')
        parts.append("</article>")
        return "".join(parts)

    # Findings ------------------------------------------------------------------------------------

    def finding(self, f) -> str:
        fid = f["id"]
        review = self.url("doc", "design/hardware-layout-review.md", "#" + f["section"]) if f.get("section") else ""
        parts = [f'<article class="zf-finding" id="finding-{fid}" data-severity="{f["severity"].lower()}">',
                 f'<header><span class="zf-sev zf-sev-{f["severity"].lower()}">{f["severity"]}</span>'
                 f'<h3>{fid} <span class="zf-where">{self.inline(f["where"])}</span></h3>'
                 f'<span class="zf-status zf-status-{f["status"]}">{STATUS_LABEL[f["status"]]}</span></header>',
                 f'<dl><dt>What was wrong</dt><dd>{self.inline(f["found"])}</dd>'
                 f'<dt>What was done</dt><dd>{self.inline(f["action"])}</dd>'
                 f'<dt>What checks it now</dt><dd>{self.inline(f["check"])}</dd></dl>']
        for image in f.get("images", []):
            src = f"{self.root}design/hardware-layout-review/{image['file']}"
            parts.append(f'<figure class="zf-fig"><button type="button" class="zf-zoom" aria-label="Enlarge the picture">'
                         f'<img src="{src}" alt="{esc(image["caption"])}" loading="lazy"></button>'
                         f'<figcaption>{self.inline(image["caption"])}</figcaption></figure>')
        parts.extend(self.figure(fid.lower(), fig) for fig in f.get("figure", []))
        links = []
        if f.get("steps"):
            links.append("Steps: " + ", ".join(
                f'<a href="#step-{s}">{esc(next(x["title"] for x in self.steps if x["id"] == s))}</a>' for s in f["steps"]))
        if review:
            links.append(f'<a href="{review}">In the review</a>')
        parts.append(f'<p class="zf-links">{". ".join(links)}.</p></article>')
        return "".join(parts)

    # The page ------------------------------------------------------------------------------------

    def overview(self) -> str:
        calib = self.manifest["overview"]
        items = "".join(f"<li><strong>{esc(b['name'])}.</strong> {self.inline(b['text'])}</li>" for b in self.spec["block"])
        top = f"{self.root}hardware/zoneflow-a/zoneflow-a-top.png"
        return (f'<figure class="zf-fig zf-overview"><button type="button" class="zf-zoom" '
                f'aria-label="Enlarge the picture of the board with its numbered blocks">'
                f'<span class="zf-stack"><img src="{top}" alt="The top side of the board, rendered by KiCad, with eleven numbered boxes" '
                f'width="{calib["width"]}" height="{calib["height"]}">'
                f'<img src="{self.img}overview-overlay.svg" alt="" width="{calib["width"]}" height="{calib["height"]}"></span></button>'
                f"<figcaption>KiCad's render of the top side with the blocks numbered. The render is a drawing from the "
                f"files, not a photograph. KiCad has no model for some parts, which show as bare lands.</figcaption></figure>"
                f'<ol class="zf-blocks">{items}</ol>')

    def walkthrough(self) -> str:
        spec, steps = self.spec, self.steps
        minutes = sum(int(g.get("minutes", 0)) for g in spec["group"])
        counts = {s: sum(1 for f in spec["finding"] if f["severity"] == s) for s in specmod.SEVERITIES}
        fixed = sum(1 for f in spec["finding"] if f["status"] == "fixed")
        # What the page's script needs for the report and for the hand-off to a coding agent: every step
        # with the parts, nets, position, pictures, files and remedies an implementer would need.
        origin = self.manifest["board"]["kicad_origin"]
        meta = {"issue": spec["meta"]["issue"], "repo": spec["meta"]["repo"], "revision": "A", "board": self.board_hash,
                "board_file": "hardware/zoneflow-a/zoneflow-a.kicad_pcb", "commit": self.commit,
                "steps": [specmod.step_context(spec, s, origin) for s in steps]}
        # "</" may not appear inside a script element.
        meta_json = json.dumps(meta).replace("</", "<\\/")
        out = [f'<link rel="stylesheet" href="{self.root}hardware/review/review.css">',
               f'<div id="zf-review" class="zf-review" data-board="{self.board_hash}">',
               f'<script type="application/json" id="zf-review-meta">{meta_json}</script>',
               '<nav class="zf-tabs" aria-label="Parts of the walkthrough"><a href="#start">Start here</a>'
               f'<a href="#steps">The {len(steps)} steps</a><a href="#findings">What the reviewers found</a>'
               '<a href="#result">Result</a></nav>']

        # Orientation
        group_rows = "".join(
            f'<tr><th scope="row"><a href="#step-{next(s["id"] for s in steps if s["group"] == g["id"])}">{esc(g["title"])}</a></th>'
            f'<td>{sum(1 for s in steps if s["group"] == g["id"])}</td><td>{int(g.get("minutes", 0))} min</td>'
            f'<td>{self.inline(g["tool"])}</td></tr>' for g in spec["group"])
        out.append(f'''<section id="start" class="zf-panel" data-panel="start">
<h2>Start here</h2>
<div class="zf-lead">
<p>This page leads you through the checks that only a person can make before the first ZoneFlow boards are ordered.
You answer one step at a time. Each step shows the place on the board it is about.</p>
<p><strong>Nothing has been built or measured.</strong> Every picture on this page is drawn from the design files.
Every "checked" below means that a program compared numbers in files. A pass from you means that you looked and
found what the step describes. It does not mean the board will work.</p>
</div>
<h3>The board</h3>
<p>A 114 x 110 mm board with four copper layers. It switches nine 24 VAC outputs, eight zones and a master valve,
with a triac each. An ESP32-S3 module runs it. Every part is on the top side except the bulk capacitor and the
relay, which hang under the lower left corner.</p>
{self.overview()}
<h3>What programs have checked, and what they cannot see</h3>
<table class="zf-table">
<thead><tr><th>Check</th><th>What it proves</th><th>What it cannot see</th></tr></thead>
<tbody>
<tr><th scope="row">Netlist parity and electrical rules</th><td>The schematic is the design netlist, pin for pin: 353 components, 200 nets.</td><td>Whether the netlist's pin numbers are the maker's. That is the datasheet group.</td></tr>
<tr><th scope="row">Design rules with schematic parity</th><td>The copper is the schematic. Clearances, widths and holes are inside JLCPCB's published limits. Nothing is unconnected.</td><td>Whether a footprint is the part. A wrong footprint agrees with itself.</td></tr>
<tr><th scope="row">Mechanical contract</th><td>Outline, holes, 26 placed parts and the height zones agree with the enclosure's contract.</td><td>The real size of a part KiCad has no model for.</td></tr>
<tr><th scope="row">Fail-safe layout rules</th><td>278 keep-away distances round the keep-alive net and the detector nodes hold.</td><td>What flux or a whisker does. Nets the rule does not list.</td></tr>
<tr><th scope="row">Placement check, on demand</th><td>Every placement puts JLCPCB's library footprint on the board's pads, pin 1 on pin 1.</td><td>How a part lies in its tape. A part the library does not name pins for.</td></tr>
</tbody></table>
<h3>What the reviewers found</h3>
<p>Three independent reviews were made from the files, each by a reader who had not written what it read. The two
circuit reviews are summarised with <a href="#findings">the findings</a>. The layout review made {len(spec["finding"])}
findings: {counts["Blocking"]} blocking, {counts["Major"]} major, {counts["Minor"]} minor and {counts["Note"]} notes.
{fixed} are fixed in the sources. The blocking one would have had JLCPCB place 26 parts wrong, the nine triacs
among them. Read a few of the cards before you start. They show what kind of fault the process catches, and what
kind it left for you.</p>
<h3>What is asked of you</h3>
<table class="zf-table">
<thead><tr><th>Group</th><th>Steps</th><th>About</th><th>You need</th></tr></thead>
<tbody>{group_rows}</tbody>
<tfoot><tr><th scope="row">All</th><td>{len(steps)}</td><td>{minutes} min</td><td></td></tr></tfoot></table>
<ul>
<li><strong>Answer each step</strong> with Pass, Fail or Not sure, and write a note where you have one. The groups
can be done in any order and on different days.</li>
<li><strong>Your answers stay in this browser.</strong> Nothing is sent anywhere. If the browser cannot store them,
the page says so and still works until you close it.</li>
<li><strong>Pass means</strong> you looked at the place, and it is as the step says under "what correct looks
like". If you are not certain, answer Not sure. That is a useful answer: the agent then asks you and does not guess.</li>
<li><strong>Some steps are decisions.</strong> The review left a few things as they are and asks whether you accept
them. Those steps are answered with Accept or Reject.</li>
<li><strong>When a step fails</strong> you can say what should be done: pick one of the remedies the step offers,
write your own, or leave it to the agent.</li>
<li><strong>Enlarge a picture</strong> to switch its layers on and off, and select a pad, a track or a zone to read
its part and its net from the board file.</li>
<li><strong>At the end</strong> the page writes a report for
<a href="{REPO_URL}/issues/{spec["meta"]["issue"]}">issue #{spec["meta"]["issue"]}</a>, the prototype build, and a
<a href="#result">hand-off file</a> that a coding agent can record and act on.</li>
<li><strong>For the bench,</strong> print the <a href="{self.url("doc", "hardware/review/bench-sheet.md")}">bench
sheet</a>: both sides at true size, the pin 1 table and every step as a tick box.</li>
</ul>
<p class="zf-actions"><a class="md-button md-button--primary zf-begin" href="#steps">Begin with the first step</a></p>
</section>''')

        # Steps
        nav, number = [], 0
        body = []
        for g in spec["group"]:
            mine = [s for s in steps if s["group"] == g["id"]]
            nav.append(f'<li class="zf-nav-group"><span class="zf-nav-title">{esc(g["title"])}</span><ol>')
            body.append(f'<div class="zf-group" id="group-{g["id"]}" data-group="{g["id"]}"><h3 class="zf-group-title">'
                        f'{esc(g["title"])}</h3><div class="zf-group-intro">{self.md(g["intro"])}'
                        f'<p><strong>You need:</strong> {self.inline(g["tool"])}.</p></div></div>')
            for s in mine:
                number += 1
                nav.append(f'<li><a href="#step-{s["id"]}" data-step="{s["id"]}"><span class="zf-dot" aria-hidden="true"></span>'
                           f'<span class="zf-nav-text">{self.inline(s["title"])}</span>'
                           f'<span class="zf-nav-state"></span></a></li>')
                body.append(self.step(s, number))
            nav.append("</ol></li>")
        out.append(f'''<section id="steps" class="zf-panel" data-panel="steps">
<h2>The {len(steps)} steps</h2>
<div class="zf-progress" aria-live="polite"><div class="zf-bar" role="progressbar" aria-label="Steps answered" aria-valuemin="0"
aria-valuemax="{len(steps)}" aria-valuenow="0"><span class="zf-bar-pass"></span><span class="zf-bar-unsure"></span><span class="zf-bar-fail"></span></div>
<p class="zf-progress-text">No step answered yet.</p></div>
<div class="zf-storage-note" hidden><p class="zf-storage-text"></p>
<button type="button" class="md-button zf-confirm-board" hidden>I have checked these answers against this board</button></div>
<div class="zf-layout">
<nav class="zf-list" aria-label="All steps"><ol>{"".join(nav)}</ol></nav>
<div class="zf-stage">{"".join(body)}
<div class="zf-pager"><button type="button" class="md-button zf-prev">Previous step</button>
<span class="zf-keys">Keys: N for the next step, P for the previous one</span>
<button type="button" class="md-button md-button--primary zf-next">Next step</button></div>
</div></div>
</section>''')

        # Findings
        filters = '<button type="button" class="zf-filter" data-severity="all" aria-pressed="true">All ' \
                  f'<span>{len(spec["finding"])}</span></button>' + "".join(
                      f'<button type="button" class="zf-filter" data-severity="{s.lower()}" aria-pressed="false">{s} '
                      f'<span>{counts[s]}</span></button>' for s in specmod.SEVERITIES)
        earlier = "".join(
            f'<article class="zf-earlier"><h4><a href="{self.url(*e["link"].split(":", 1))}">{esc(e["title"])}</a></h4>'
            f'<p>{self.inline(e["what"])} <strong>{esc(e["counts"])}.</strong></p>'
            f'<ul>{"".join(f"<li>{self.inline(p)}</li>" for p in e["points"])}</ul></article>' for e in spec["earlier"])
        out.append(f'''<section id="findings" class="zf-panel" data-panel="findings">
<h2>What the reviewers found</h2>
<p>The {len(spec["finding"])} findings of the <a href="{self.url("doc", "design/hardware-layout-review.md")}">layout
review</a>, one card each. Severity is for the files as the reviewer found them. "Blocking" would have wasted the
order. "Major" would have cost a re-spin, a wrong measurement or a field failure. Where the review has a picture of
before and after, the card shows it. The cards are condensed: the review has the evidence.</p>
<div class="zf-filters" role="group" aria-label="Show findings of one severity">{filters}</div>
<div class="zf-cards">{"".join(self.finding(f) for f in spec["finding"])}</div>
<h3>The two circuit reviews before the layout</h3>
<div class="zf-earlier-list">{earlier}</div>
</section>''')

        # Result
        design = self.url("doc", "design/hardware-design.md")
        out.append(f'''<section id="result" class="zf-panel" data-panel="result">
<h2>Result</h2>
<div class="zf-result-live"><p class="zf-result-summary">Answer the steps first. This part fills in as you go.
It needs JavaScript.</p>
<div class="zf-result-lists"></div></div>
<h3>Before anyone orders</h3>
<ol>
<li><strong>Every step is a pass.</strong> A Fail or a Not sure is settled first: fixed in the sources and built
again, or explained in the issue and accepted by the owner in words.</li>
<li><strong>The sample display panel has been checked.</strong> That is the step
<a href="#step-h-display">"The display panel's flat cable against J7"</a>. It cannot be passed without the panel.</li>
<li><strong>The owner decides.</strong> This walkthrough gives you grounds. It does not give approval. The site keeps
saying "do not order boards" until the owner changes it.</li>
</ol>
<h3>What stays unknown until a board exists</h3>
<p>No answer on this page says the board works. These need a prototype and a bench:</p>
<ul>
<li><strong>The bench list.</strong> <a href="{design}#20-design-risk-register">Hardware design, section 20</a>:
34 risks, each with its test. Triac and module temperature, a shorted output, surge, the keep-alive thresholds,
radio range.</li>
<li><strong>The bring-up plan.</strong> <a href="{design}#21-bring-up-test-plan">Section 21</a>: 18 steps for the
first board. Step 1 begins with an inspection before power. Use it to compare every polarised part with the
assembly drawing.</li>
<li><strong>What the layout review could not verify.</strong>
<a href="{self.url("doc", "design/hardware-layout-review.md", "#16-what-i-could-not-verify")}">Its section 16</a>.
Solderability of the 0.3 mm lands, tape orientation, plating in the holes, the effect of the slots in the 3.3 V
plane.</li>
<li><strong>Three findings with a number to measure:</strong> <a href="#finding-L25">L25</a>, the PTC body at 0.5 A;
<a href="#finding-L18">L18</a>, ADC noise with WiFi on; <a href="#finding-L14">L14</a>, the triac tab joints.</li>
</ul>
<h3>Report for the issue</h3>
<p>The report is Markdown. Copy it or save it, then paste it as a comment on
<a href="{REPO_URL}/issues/{spec["meta"]["issue"]}">issue #{spec["meta"]["issue"]}</a>. The page does not send it.</p>
<p class="zf-actions"><button type="button" class="md-button md-button--primary zf-make-report">Write the report</button>
<button type="button" class="md-button zf-copy-report" disabled>Copy</button>
<button type="button" class="md-button zf-download-report" disabled>Save as a file</button>
<a class="md-button zf-open-issue" href="{REPO_URL}/issues/{spec["meta"]["issue"]}#new_comment_field" target="_blank" rel="noopener">Open
a new comment on issue #{spec["meta"]["issue"]}</a></p>
<p class="zf-report-status" role="status" aria-live="polite"></p>
<label for="zf-report" class="zf-note-label">The report</label>
<textarea id="zf-report" class="zf-report" rows="16" readonly placeholder="Select &quot;Write the report&quot;."></textarea>
<h3 id="hand-off">Hand off to an agent</h3>
<p>One file with everything a coding agent needs to record this review in the repository and to make the changes it
calls for. It starts with instructions for the agent. Then come your decisions as data, with the parts, nets, positions
and source files of every step that is not passed, and the remedy you chose. Then the same as text. Passed and
accepted steps are listed in short, so that the record is complete. The page does not send the file: you give it to
the agent.</p>
<p class="zf-actions"><button type="button" class="md-button md-button--primary zf-handoff-copy">Copy the hand-off file</button>
<button type="button" class="md-button zf-handoff-save">Save the hand-off file</button></p>
<p class="zf-report-status zf-handoff-status" role="status" aria-live="polite"></p>
<details class="zf-handoff-show"><summary>Read the file before you hand it over</summary>
<label for="zf-handoff-file" class="zf-note-label">The hand-off file</label>
<textarea id="zf-handoff-file" class="zf-report" rows="18" readonly></textarea></details>
<p>An agent, or you, records the file in the repository with one command. It checks the decisions against
<a href="{self.root}hardware/review/handoff.schema.json">the schema</a> and against the board file, and writes a dated
record under <code>docs/design/reviews/</code> with the data beside it:</p>
<pre><code>uv run --project tools/reviewwalk reviewwalk record zoneflow-rev-a-review-handoff-YYYY-MM-DD.md</code></pre>
<h3 id="bring-in">Bring answers in from a hand-off file</h3>
<p>To go on with a review on another machine, or to look again after an agent has changed the board, give the page a
hand-off file. It replaces the answers on this page. If the file is a review of another board file than the one this
page shows, the page says so and keeps the answers for you to check.</p>
<p class="zf-actions"><label class="md-button zf-file-label">Choose a hand-off file
<input type="file" class="zf-import-file" accept=".md,.markdown,.json,.txt,text/markdown,application/json,text/plain"></label></p>
<label for="zf-import-text" class="zf-note-label">Or paste the file here</label>
<textarea id="zf-import-text" class="zf-report" rows="5" placeholder="Paste the whole hand-off file, or its JSON block alone."></textarea>
<p class="zf-actions"><button type="button" class="md-button zf-import-go">Bring the answers in</button></p>
<div class="zf-import-status" role="status" aria-live="polite"></div>
<p class="zf-actions"><button type="button" class="md-button zf-reset">Delete all my answers</button></p>
</section>''')
        out.append('''<dialog id="zf-lightbox" class="zf-lightbox" aria-label="Enlarged picture">
<div class="zf-lb-bar"><span class="zf-lb-caption"></span>
<button type="button" class="zf-lb-out" aria-label="Zoom out">-</button>
<button type="button" class="zf-lb-in" aria-label="Zoom in">+</button>
<button type="button" class="zf-lb-fit">Fit</button>
<button type="button" class="zf-lb-close">Close</button></div>
<div class="zf-lb-tools" hidden><span class="zf-lb-layers" role="group" aria-label="Layers shown"></span>
<span class="zf-lb-info" role="status" aria-live="polite"></span></div>
<div class="zf-lb-view" tabindex="0" aria-label="Picture. Drag to move, wheel or plus and minus to zoom."><div class="zf-lb-content"></div></div>
</dialog>''')
        out.append(f'</div>\n<script src="{self.root}hardware/review/review.js" defer></script>')
        return "\n".join(out)

    # The bench sheet -----------------------------------------------------------------------------

    def bench_sheet(self, pin1_csv) -> str:
        asm = self.manifest["assembly"]
        with Path(pin1_csv).open(newline="", encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
        table = "".join(
            f'<tr><th scope="row">{esc(r["Designator"])}</th><td>{esc(r["Side"])}</td><td>{float(r["Centre X"]):.1f}, '
            f'{float(r["Centre Y"]):.1f}</td><td>{esc(r["Pad"])}</td><td>{esc(r["Pad is at"])}</td><td class="zf-tick"></td></tr>'
            for r in rows)
        out = [f'<link rel="stylesheet" href="{self.root}hardware/review/review.css">', '<div class="zf-sheet">']
        for side in ("top", "bottom"):
            a = asm[side]
            out.append(f'<section class="zf-sheet-page"><h2>{side.capitalize()} side at true size</h2>'
                       f'<img class="zf-true" src="{self.img}assembly-{side}.svg" style="width:{a["width_mm"]:g}mm;height:{a["height_mm"]:g}mm" '
                       f'alt="The {side} side of the board as lands and part outlines, with two 100 mm scale bars"></section>')
        out.append(f'<section class="zf-sheet-page"><h2>Where pin 1 is</h2><p>Every polarised part, from '
                   f'<code>zoneflow-a-pin1.csv</code>: {len(rows)} parts. "Pad is at" is seen from above for the top side '
                   f'and from below for the bottom side. Positions in millimetres from the lower left corner.</p>'
                   f'<table class="zf-pin1"><thead><tr><th>Part</th><th>Side</th><th>Centre x, y</th><th>Pad</th><th>Pad is at</th>'
                   f'<th>Seen</th></tr></thead><tbody>{table}</tbody></table></section>')
        out.append('<section class="zf-sheet-page"><h2>The steps as tick boxes</h2><p>P is pass, F is fail, ? is not sure. For a '
                   'step that asks "accept or reject", P is accept and F is reject. '
                   "The walkthrough has the pictures and the reasons.</p>")
        number = 0
        for g in self.spec["group"]:
            out.append(f'<h3>{esc(g["title"])}</h3><table class="zf-ticks"><thead><tr><th>No.</th><th>Step</th><th>P</th><th>F</th>'
                       f'<th>?</th><th>Note</th></tr></thead><tbody>')
            for s in self.steps:
                if s["group"] != g["id"]:
                    continue
                number += 1
                first = re.split(r"(?<=[.?!])\s", " ".join(str(s["check"]).split()), maxsplit=1)[0]
                out.append(f'<tr><td>{number}</td><td><strong>{self.inline(s["title"])}.</strong> {self.inline(first)}</td>'
                           f'<td class="zf-tick"></td><td class="zf-tick"></td><td class="zf-tick"></td><td class="zf-line"></td></tr>')
            out.append("</tbody></table>")
        out.append("</section></div>")
        return "\n".join(out)


def load_manifest(out_dir=specmod.OUT):
    return json.loads((Path(out_dir) / specmod.MANIFEST).read_text(encoding="utf-8"))
