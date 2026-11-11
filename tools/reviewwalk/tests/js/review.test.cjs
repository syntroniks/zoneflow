// SPDX-License-Identifier: Apache-2.0
// The logic of docs/hardware/review/review.js, without a browser: node --test tools/reviewwalk/tests/js
const test = require("node:test");
const assert = require("node:assert/strict");
const path = require("node:path");

const R = require(path.resolve(__dirname, "../../../../docs/hardware/review/review.js"));

const steps = [
  { id: "a", title: "First step", group: "In KiCad" },
  { id: "b", title: "Second step", group: "In KiCad" },
  { id: "c", title: "Third step", group: "On the 1:1 print" },
];
const meta = { issue: 33, repo: "syntroniks/zoneflow", board: "0123456789abcdef0123456789abcdef", steps };

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

test("the summary counts only steps that exist", () => {
  const s = R.emptyState("b1");
  R.setAnswer(s, "a", "pass");
  R.setAnswer(s, "b", "unsure");
  R.setAnswer(s, "gone", "fail");
  const sum = R.summarize(steps, s);
  assert.deepEqual(
    { pass: sum.pass, fail: sum.fail, unsure: sum.unsure, open: sum.open, answered: sum.answered, allPass: sum.allPass },
    { pass: 1, fail: 0, unsure: 1, open: 1, answered: 2, allPass: false });
  assert.equal(R.summaryText(sum), "2 of 3 steps answered: 1 pass, 1 not sure, 1 not answered.");
  assert.equal(R.summaryText(R.summarize(steps, R.emptyState(""))), "No step answered yet.");
});

test("all pass only when every step is a pass", () => {
  const s = R.emptyState("b1");
  steps.forEach((x) => R.setAnswer(s, x.id, "pass"));
  assert.equal(R.summarize(steps, s).allPass, true);
  assert.equal(R.summarize([], s).allPass, false);
});

test("stored data that this page did not write is not trusted", () => {
  assert.deepEqual(R.parseState("not json", "b1").answers, {});
  assert.deepEqual(R.parseState(null, "b1").answers, {});
  assert.deepEqual(R.parseState('{"version":2,"answers":{"a":{"answer":"pass"}}}', "b1").answers, {});
  const s = R.parseState(JSON.stringify({
    version: 1, board: "old",
    answers: { a: { answer: "pass", note: 5 }, b: { answer: "<script>", note: "n" }, c: "x", d: { answer: "fail", note: "y".repeat(5000) } },
  }), "b1");
  assert.deepEqual(s.answers.a, { answer: "pass", note: "" });
  assert.deepEqual(s.answers.b, { answer: "", note: "n" });
  assert.equal(s.answers.c, undefined);
  assert.equal(s.answers.d.note.length, 4000);
  assert.equal(s.board, "old");
});

test("a state survives being stored and read back", () => {
  const s = R.emptyState("b1");
  R.setAnswer(s, "a", "fail");
  R.setNote(s, "a", "pad 1 is on GND");
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
  assert.match(report, /\*\*Result: 1 pass, 1 fail, 0 not sure, 1 not answered\.\*\*/);
  assert.match(report, /Not every step is a pass\. The board is not ready to order\./);
  assert.match(report, /### Failed \(1\)\n\n\*\*In KiCad\*\*\n\n- \[ \] Second step\n {2}> Only five vias\. Counted twice\./);
  assert.match(report, /### Not answered \(1\)\n\n\*\*On the 1:1 print\*\*\n\n- \[ \] Third step/);
  assert.match(report, /### Passed \(1\)\n\n\*\*In KiCad\*\*\n\n- \[x\] First step/);
  assert.match(report, /Nothing was built or measured\./);
  assert.ok(!report.includes("Not sure ("));
});

test("a report with every step passed still leaves the decision to the owner", () => {
  const s = R.emptyState(meta.board);
  steps.forEach((x) => R.setAnswer(s, x.id, "pass"));
  const report = R.buildReport(meta, s, "2026-11-12");
  assert.match(report, /Every step is a pass\. What is still needed before an order: the owner's decision\./);
  assert.ok(!report.includes("### Failed"));
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
