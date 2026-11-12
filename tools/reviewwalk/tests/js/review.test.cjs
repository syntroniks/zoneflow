// SPDX-License-Identifier: Apache-2.0
// The logic of docs/hardware/review/review.js, without a browser:
//   node --test tools/reviewwalk/tests/js/review.test.cjs
const test = require("node:test");
const assert = require("node:assert/strict");
const path = require("node:path");

const R = require(path.resolve(__dirname, "../../../../docs/hardware/review/review.js"));

function step(id, title, kind, extra) {
  return Object.assign({
    id, title, kind, group: "kicad", group_title: "In KiCad",
    designators: ["U1"], nets: ["GND"],
    position: { x_mm: 21.5, y_mm: 33, kicad_x_mm: 111.5, kicad_y_mm: 127 },
    pictures: [{ file: "hardware/zoneflow-a/output/review/" + id + "-pad.svg", view: "top", box_mm: [16, 29, 27, 38] }],
    files: ["hardware/zoneflow-a/design/layout.py"],
    remedies: [{ id: "add-vias", text: "Add the vias in `design/layout.py`." }],
    source: "Layout review, section 17, item 7", findings: ["L2"],
  }, extra || {});
}

const steps = [
  step("a", "First step", "check"),
  step("b", "Second step", "check"),
  step("c", "Third step", "decision", { group: "print", group_title: "On the 1:1 print", position: null, remedies: [] }),
];
const BOARD = "0123456789abcdef".repeat(4);
const meta = {
  issue: 33, repo: "syntroniks/zoneflow", revision: "A", board: BOARD,
  board_file: "hardware/zoneflow-a/zoneflow-a.kicad_pcb", commit: "a".repeat(40), steps,
};

test("answers and notes are set, changed and cleared", () => {
  const s = R.emptyState("b1");
  R.setAnswer(s, "a", "pass");
  R.setNote(s, "a", "looked");
  assert.deepEqual(s.answers.a, { answer: "pass", note: "looked" });
  R.setAnswer(s, "a", "");
  assert.deepEqual(s.answers.a, { answer: "", note: "looked" });
  R.setNote(s, "a", "");
  assert.equal(s.answers.a, undefined);
  R.setAnswer(s, "b", "maybe");
  assert.equal(s.answers.b, undefined);
});

test("a check is passed or failed, a decision is accepted or rejected", () => {
  const s = R.emptyState(BOARD);
  R.setAnswer(s, "a", "pass");
  R.setAnswer(s, "b", "fail");
  R.setAnswer(s, "c", "fail");
  assert.equal(R.decisionOf(steps[0], s), "pass");
  assert.equal(R.decisionOf(steps[1], s), "fail");
  assert.equal(R.decisionOf(steps[2], s), "rejected");
  R.setAnswer(s, "c", "pass");
  assert.equal(R.decisionOf(steps[2], s), "accepted");
  R.setAnswer(s, "c", "unsure");
  assert.equal(R.decisionOf(steps[2], s), "not_sure");
  R.setAnswer(s, "c", "");
  assert.equal(R.decisionOf(steps[2], s), "not_answered");
});

test("the summary counts only steps that exist, by what each answer is called", () => {
  const s = R.emptyState("b1");
  R.setAnswer(s, "a", "pass");
  R.setAnswer(s, "c", "pass");
  R.setAnswer(s, "gone", "fail");
  const sum = R.summarize(steps, s);
  assert.deepEqual(sum.decisions, { pass: 1, accepted: 1, fail: 0, rejected: 0, not_sure: 0, not_answered: 1 });
  assert.equal(sum.answered, 2);
  assert.equal(sum.allPass, false);
  assert.equal(R.summaryText(sum), "2 of 3 steps answered: 1 pass, 1 accepted, 1 not answered.");
  assert.equal(R.summaryText(R.summarize(steps, R.emptyState(""))), "No step answered yet.");
  R.setAnswer(s, "b", "pass");
  assert.equal(R.summarize(steps, s).allPass, true);
  assert.equal(R.summarize([], s).allPass, false);
});

test("a remedy is kept for a failed step only", () => {
  const s = R.emptyState(BOARD);
  R.setAnswer(s, "a", "fail");
  assert.deepEqual(R.remedyOf(steps[0], s), { choice: "agent", text: "" });
  R.setRemedy(s, "a", "add-vias", "");
  assert.deepEqual(R.remedyOf(steps[0], s), { choice: "listed", id: "add-vias", text: "Add the vias in `design/layout.py`." });
  R.setRemedy(s, "a", "own", "  Use a part with a bigger pad. ");
  assert.deepEqual(R.remedyOf(steps[0], s), { choice: "own", text: "Use a part with a bigger pad." });
  R.setRemedy(s, "a", "own", "   ");
  assert.equal(R.remedyOf(steps[0], s).choice, "agent"); // own words that are empty are no remedy
  R.setRemedy(s, "a", "no-such-remedy", "");
  assert.equal(R.remedyOf(steps[0], s).choice, "agent");
  R.setRemedy(s, "a", "add-vias", "");
  R.setAnswer(s, "a", "pass");
  assert.equal(s.answers.a.remedy, undefined);
});

test("stored data that this page did not write is not trusted", () => {
  assert.deepEqual(R.parseState("not json", "b1").answers, {});
  assert.deepEqual(R.parseState(null, "b1").answers, {});
  assert.deepEqual(R.parseState('{"version":2,"answers":{"a":{"answer":"pass"}}}', "b1").answers, {});
  const s = R.parseState(JSON.stringify({
    version: 1, board: "old",
    answers: {
      a: { answer: "pass", note: 5 }, b: { answer: "<script>", note: "n" }, c: "x",
      d: { answer: "fail", note: "y".repeat(5000), remedy: "<b>", remedyText: 7 },
      e: { answer: "fail", note: "", remedy: "own", remedyText: "z".repeat(3000) },
    },
  }), "b1");
  assert.deepEqual(s.answers.a, { answer: "pass", note: "" });
  assert.deepEqual(s.answers.b, { answer: "", note: "n" });
  assert.equal(s.answers.c, undefined);
  assert.equal(s.answers.d.note.length, 4000);
  assert.equal(s.answers.d.remedy, undefined);
  assert.equal(s.answers.e.remedyText.length, 1000);
  assert.equal(s.board, "old");
});

test("a state survives being stored and read back", () => {
  const s = R.emptyState("b1");
  R.setAnswer(s, "a", "fail");
  R.setNote(s, "a", "pad 1 is on GND");
  R.setRemedy(s, "a", "own", "swap the pads");
  assert.deepEqual(R.parseState(JSON.stringify(s), "b1"), s);
});

test("the next open step wraps round", () => {
  const s = R.emptyState("b1");
  R.setAnswer(s, "b", "pass");
  assert.equal(R.nextOpen(steps, s, 0), 2);
  assert.equal(R.nextOpen(steps, s, 2), 0);
  R.setAnswer(s, "a", "pass");
  R.setAnswer(s, "c", "pass");
  assert.equal(R.nextOpen(steps, s, 0), -1);
});

test("the report names what failed, with the note, and says what it is not", () => {
  const s = R.emptyState(meta.board);
  R.setAnswer(s, "a", "pass");
  R.setAnswer(s, "b", "fail");
  R.setNote(s, "b", "Only five vias.\nCounted twice.");
  const report = R.buildReport(meta, s, "2026-11-12");
  assert.match(report, /^## Pre-order review of ZoneFlow rev A\n/);
  assert.match(report, /Date: 2026-11-12\. Board file `zoneflow-a\.kicad_pcb`, SHA-256 `0123456789abcdef`\./);
  assert.match(report, /\*\*Result: 1 pass, 0 accepted, 1 fail, 0 rejected, 0 not sure, 1 not answered\.\*\*/);
  assert.match(report, /Not every step is passed or accepted\. The board is not ready to order\./);
  assert.match(report, /### Failed and rejected \(1\)\n\n\*\*In KiCad\*\*\n\n- \[ \] Second step \(fail\)\n {2}> Only five vias\. Counted twice\./);
  assert.match(report, /### Not answered \(1\)\n\n\*\*On the 1:1 print\*\*\n\n- \[ \] Third step/);
  assert.match(report, /### Passed and accepted \(1\)\n\n\*\*In KiCad\*\*\n\n- \[x\] First step \(pass\)/);
  assert.match(report, /Nothing was built or measured\./);
  assert.ok(!report.includes("Not sure ("));
});

test("a report with every step passed still leaves the decision to the owner", () => {
  const s = R.emptyState(meta.board);
  steps.forEach((x) => R.setAnswer(s, x.id, "pass"));
  const report = R.buildReport(meta, s, "2026-11-12");
  assert.match(report, /Every step is passed or accepted\. What is still needed before an order: the owner's decision\./);
  assert.match(report, /- \[x\] Third step \(accepted\)/);
  assert.ok(!report.includes("### Failed"));
});

function reviewed() {
  const s = R.emptyState(meta.board);
  R.setAnswer(s, "a", "pass");
  R.setNote(s, "a", "Six vias.");
  R.setAnswer(s, "b", "fail");
  R.setNote(s, "b", "Only five vias.\nCounted twice.");
  R.setRemedy(s, "b", "add-vias", "");
  R.setAnswer(s, "c", "fail");
  R.setRemedy(s, "c", "own", "Move the part 2 mm up.");
  return s;
}

test("the hand-off data carries what an agent needs, and less for a passed step", () => {
  const doc = R.buildHandoffData(meta, reviewed(), "2026-11-12");
  assert.equal(doc.schema, "zoneflow-review-handoff");
  assert.equal(doc.schema_version, 1);
  assert.deepEqual(doc.board, { revision: "A", file: "hardware/zoneflow-a/zoneflow-a.kicad_pcb", sha256: BOARD });
  assert.equal(doc.site_commit, "a".repeat(40));
  assert.deepEqual(doc.summary, { steps: 3, pass: 1, accepted: 0, fail: 1, rejected: 1, not_sure: 0, not_answered: 0 });
  const [a, b, c] = doc.decisions;
  assert.deepEqual(a, { step: "a", title: "First step", group: "kicad", group_title: "In KiCad", kind: "check", decision: "pass", note: "Six vias." });
  assert.equal(b.decision, "fail");
  assert.deepEqual(b.designators, ["U1"]);
  assert.deepEqual(b.nets, ["GND"]);
  assert.deepEqual(b.position, { x_mm: 21.5, y_mm: 33, kicad_x_mm: 111.5, kicad_y_mm: 127 });
  assert.deepEqual(b.files, ["hardware/zoneflow-a/design/layout.py"]);
  assert.equal(b.source, "Layout review, section 17, item 7");
  assert.deepEqual(b.findings, ["L2"]);
  assert.deepEqual(b.remedy, { choice: "listed", id: "add-vias", text: "Add the vias in `design/layout.py`." });
  assert.equal(c.kind, "decision");
  assert.equal(c.decision, "rejected");
  assert.equal(c.position, null);
  assert.deepEqual(c.remedy, { choice: "own", text: "Move the part 2 mm up." });
});

test("the hand-off file instructs the agent, holds the data and says it again as text", () => {
  const file = R.buildHandoffFile(meta, reviewed(), "2026-11-12");
  assert.match(file, /^# ZoneFlow rev A: hand-off of the pre-order review, 2026-11-12\n/);
  assert.match(file, /\*\*1 pass, 0 accepted, 1 fail, 1 rejected, 0 not sure, 0 not answered\.\*\*/);
  for (const must of [
    "- **Repository:** `syntroniks/zoneflow`. **Board:** revision A, `hardware/zoneflow-a/zoneflow-a.kicad_pcb`.",
    "the board file with SHA-256 `" + BOARD + "`",
    "built from commit** `" + "a".repeat(40) + "`",
    "read `CLAUDE.md` at the root of the repository before anything else",
    "uv run --project tools/reviewwalk reviewwalk record zoneflow-rev-a-review-handoff-2026-11-12.md",
    "docs/design/reviews/2026-11-12-pre-order.md",
    "comment on issue #33",
    "open one pull request with that one change",
    "Do not guess",
    "**Change nothing for a passed or accepted step.**",
    "**Never order boards,**",
    "`docs/hardware/review/handoff.schema.json`, version 1",
  ]) assert.ok(file.includes(must), must);
  // Changes first, the passed steps last and in short.
  assert.match(file, /## Needs a change \(2\)\n\n### Second step\n\n- \*\*Step:\*\* `b`, In KiCad\n- \*\*Decision:\*\* fail\n- \*\*Owner's note:\*\* Only five vias\. Counted twice\.\n- \*\*Remedy:\*\* Chosen from the list \(`add-vias`\): Add the vias in `design\/layout\.py`\.\n- \*\*Parts:\*\* U1\n- \*\*Nets:\*\* `GND`\n- \*\*Position:\*\* \(21\.5, 33\) mm from the lower left corner of the board\. KiCad's default display shows \(111\.5, 127\)\n- \*\*Files a change would be made in:\*\* `hardware\/zoneflow-a\/design\/layout\.py`\n/);
  assert.match(file, /### Third step\n\n- \*\*Step:\*\* `c`, On the 1:1 print\n- \*\*Decision:\*\* rejected\n- \*\*Owner's note:\*\* none\n- \*\*Remedy:\*\* The owner's own: Move the part 2 mm up\./);
  assert.match(file, /## Not sure: ask the owner \(0\)\n\nThe owner was sure of every answer given\./);
  assert.match(file, /## Passed and accepted: change nothing \(1\)\n\n- pass: First step \(`a`\)\. Note: Six vias\.\n/);
  assert.ok(file.indexOf("# Decisions as data") < file.indexOf("# Decisions as text"));
});

test("a hand-off file goes out and comes back in with the same answers", () => {
  const before = reviewed();
  R.setAnswer(before, "a", "unsure");
  const file = R.buildHandoffFile(meta, before, "2026-11-12");
  const parsed = R.parseHandoff(file);
  assert.deepEqual(parsed.errors, []);
  const result = R.importHandoff(meta, parsed.doc);
  assert.equal(result.boardDiffers, false);
  assert.deepEqual(result.unknown, []);
  assert.equal(result.imported, 3);
  assert.deepEqual(result.state.answers, before.answers);
  assert.equal(result.state.board, BOARD);
  // And what comes back gives the same file again.
  assert.equal(R.buildHandoffFile(meta, result.state, "2026-11-12"), file);
  // The JSON alone is taken as well, and so is a file with Windows line ends.
  const json = JSON.stringify(parsed.doc);
  assert.deepEqual(R.importHandoff(meta, R.parseHandoff(json).doc).state.answers, before.answers);
  assert.deepEqual(R.importHandoff(meta, R.parseHandoff(file.replace(/\n/g, "\r\n")).doc).state.answers, before.answers);
});

test("a file of another board is taken in with a warning to give", () => {
  const doc = R.buildHandoffData(meta, reviewed(), "2026-11-12");
  doc.board.sha256 = "f".repeat(64);
  const result = R.importHandoff(meta, R.parseHandoff(JSON.stringify(doc)).doc);
  assert.equal(result.boardDiffers, true);
  assert.equal(result.board, "f".repeat(64));
  assert.equal(result.state.board, "f".repeat(64)); // the answers remember which board they were given for
  assert.equal(result.imported, 3);
});

test("steps the page does not have are left out and named, and a remedy it no longer offers keeps its words", () => {
  const doc = R.buildHandoffData(meta, reviewed(), "2026-11-12");
  doc.decisions.push({ step: "gone", title: "x", group: "kicad", group_title: "In KiCad", kind: "check", decision: "pass", note: "" });
  doc.decisions[1].remedy = { choice: "listed", id: "old-remedy", text: "Do the old thing." };
  const result = R.importHandoff(meta, doc);
  assert.deepEqual(result.unknown, ["gone"]);
  assert.equal(result.state.answers.gone, undefined);
  assert.equal(result.state.answers.b.remedy, "own");
  assert.equal(result.state.answers.b.remedyText, "Do the old thing.");
});

test("what is not a hand-off file is refused with a reason", () => {
  assert.match(R.parseHandoff("# Notes\n\nNothing here.").errors[0], /not a hand-off file/);
  assert.match(R.parseHandoff("```json\n{\"schema\": \"something-else\"}\n```").errors[0], /not a hand-off file/);
  assert.match(R.parseHandoff("{ not json").errors[0], /not a hand-off file/);
  const doc = R.buildHandoffData(meta, reviewed(), "2026-11-12");
  assert.match(R.parseHandoff(JSON.stringify(Object.assign({}, doc, { schema_version: 2 }))).errors[0], /schema version 2/);
  assert.match(R.parseHandoff(JSON.stringify(Object.assign({}, doc, { board: {} }))).errors[0], /which board file/);
  assert.match(R.parseHandoff(JSON.stringify(Object.assign({}, doc, { decisions: "x" }))).errors[0], /no list of decisions/);
  const bad = JSON.parse(JSON.stringify(doc));
  bad.decisions[0].decision = "splendid";
  assert.match(R.parseHandoff(JSON.stringify(bad)).errors[0], /Decision 1/);
  assert.equal(R.parseHandoff(JSON.stringify(bad)).doc, null);
});

test("a selected shape is described by what the picture says of it and nothing else", () => {
  assert.equal(R.describeShape({ layer: "F.Cu", pad: "U1.9", net: "GND", tag: "path" }), "Pad 9 of U1, net GND, on F.Cu.");
  assert.equal(R.describeShape({ layer: "F.Cu", pad: "H3.", net: "", tag: "path" }), "A pad of H3 without a number, no net, on F.Cu.");
  assert.equal(R.describeShape({ layer: "B.Cu", pad: null, net: "/KA", tag: "path", stroked: true }), "Track, net /KA, on B.Cu.");
  assert.equal(R.describeShape({ layer: "B.Cu", pad: null, net: "/KA", tag: "circle" }), "Via, net /KA. Selected on B.Cu.");
  assert.equal(R.describeShape({ layer: "In1.Cu", pad: null, net: "GND", zone: true, tag: "path" }), "Filled zone, net GND, on In1.Cu.");
  assert.equal(R.describeShape({ layer: "F.Fab", pad: null, net: null, ref: "D25", tag: "path" }), "Outline or text of D25, on F.Fab.");
  assert.equal(R.describeShape({ layer: "Edge.Cuts", pad: null, net: null, ref: "", tag: "path" }), "A drawing of the board itself, on Edge.Cuts.");
  assert.equal(R.describeShape(null), "Nothing of the board at this point.");
  assert.equal(R.describeShape({ layer: "" }), "Nothing of the board at this point.");
});

test("addresses inside the page", () => {
  assert.deepEqual(R.parseHash("#step-k-u1-vias"), { panel: "steps", step: "k-u1-vias" });
  assert.deepEqual(R.parseHash("#finding-L5"), { panel: "findings", finding: "L5" });
  assert.deepEqual(R.parseHash("#group-order"), { panel: "steps", group: "order" });
  assert.deepEqual(R.parseHash("#result"), { panel: "result" });
  assert.deepEqual(R.parseHash(""), { panel: "" });
  assert.deepEqual(R.parseHash("#how-this-page-is-made"), { panel: "" });
});

test("zoom keeps the point under the pointer and the picture in its frame", () => {
  const fit = { scale: 1, x: 0, y: 0, w: 800, h: 600 };
  const z = R.zoomAt(fit, 2, 200, 150);
  assert.equal(z.scale, 2);
  assert.equal((200 - z.x) / z.scale, 200); // the point of the picture that was under the pointer still is
  assert.equal((150 - z.y) / z.scale, 150);
  assert.equal(R.zoomAt(fit, 0.5, 400, 300).scale, 1); // not smaller than fitted
  assert.equal(R.zoomAt(fit, 100, 400, 300).scale, 12); // and not without end
  const far = R.clampView({ scale: 2, x: 500, y: -5000 }, 800, 600, z);
  assert.equal(far.x, 0);
  assert.equal(far.y, -600);
});
