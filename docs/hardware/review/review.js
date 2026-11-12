// SPDX-License-Identifier: Apache-2.0
// The pre-order review walkthrough: one step at a time, answers kept in this browser, a report at
// the end. Nothing is sent anywhere. The page is complete without this file; see page.py.
//
// The first half is logic with no page in it, and tools/reviewwalk/tests/js tests it under Node.
(function (root, factory) {
  var api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.ZFReview = api;
    if (root.document) api.start(root);
  }
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  var STORAGE_KEY = "zoneflow.review.rev-a.v1";
  var ANSWERS = ["pass", "fail", "unsure"];
  var HANDOFF_SCHEMA = "zoneflow-review-handoff";
  var HANDOFF_VERSION = 1;
  // An answer is kept as pass, fail or unsure. What it is called depends on the step: a check is
  // passed or failed, a decision is accepted or rejected.
  var DECISION = {
    check: { pass: "pass", fail: "fail", unsure: "not_sure", "": "not_answered" },
    decision: { pass: "accepted", fail: "rejected", unsure: "not_sure", "": "not_answered" },
  };
  var ANSWER_OF = { pass: "pass", accepted: "pass", fail: "fail", rejected: "fail", not_sure: "unsure", not_answered: "" };
  var WORD = { pass: "pass", fail: "fail", accepted: "accepted", rejected: "rejected", not_sure: "not sure", not_answered: "not answered" };
  var COUNTS = ["pass", "accepted", "fail", "rejected", "not_sure", "not_answered"];

  function emptyState(board) {
    return { version: 1, board: board || "", answers: {} };
  }

  function clean(a) {
    var answer = ANSWERS.indexOf(a.answer) >= 0 ? a.answer : "";
    var out = { answer: answer, note: typeof a.note === "string" ? a.note.slice(0, 4000) : "" };
    // A remedy belongs to a failed or rejected step only.
    if (answer === "fail" && typeof a.remedy === "string" && /^[a-z0-9][a-z0-9-]*$/.test(a.remedy)) {
      out.remedy = a.remedy;
      if (a.remedy === "own") out.remedyText = typeof a.remedyText === "string" ? a.remedyText.slice(0, 1000) : "";
    }
    return out;
  }

  // Anything that is not a state this page wrote becomes an empty one: stored data is not trusted.
  function parseState(text, board) {
    var state = emptyState(board);
    var data;
    try { data = JSON.parse(text); } catch (e) { return state; }
    if (!data || data.version !== 1 || typeof data.answers !== "object" || data.answers === null) return state;
    state.board = typeof data.board === "string" ? data.board : "";
    Object.keys(data.answers).forEach(function (id) {
      var a = data.answers[id];
      if (!a || typeof a !== "object") return;
      a = clean(a);
      if (a.answer || a.note) state.answers[id] = a;
    });
    return state;
  }

  function put(state, id, a) {
    a = clean(a);
    if (a.answer || a.note) state.answers[id] = a; else delete state.answers[id];
    return state;
  }

  function entry(state, id) {
    var a = state.answers[id] || {};
    return { answer: a.answer || "", note: a.note || "", remedy: a.remedy || "", remedyText: a.remedyText || "" };
  }

  function setAnswer(state, id, answer) {
    var a = entry(state, id);
    a.answer = answer;
    return put(state, id, a);
  }

  function setNote(state, id, note) {
    var a = entry(state, id);
    a.note = String(note || "");
    return put(state, id, a);
  }

  // remedy: "" lets the agent propose, "own" takes the owner's text, anything else is the id of a listed remedy.
  function setRemedy(state, id, remedy, text) {
    var a = entry(state, id);
    a.remedy = String(remedy || "");
    a.remedyText = String(text || "");
    return put(state, id, a);
  }

  function answerOf(state, id) {
    return (state.answers[id] && state.answers[id].answer) || "";
  }

  function decisionOf(step, state) {
    return (DECISION[step.kind] || DECISION.check)[answerOf(state, step.id)];
  }

  function remedyOf(step, state) {
    var a = entry(state, step.id);
    var listed = (step.remedies || []).filter(function (r) { return r.id === a.remedy; })[0];
    if (listed) return { choice: "listed", id: listed.id, text: listed.text };
    if (a.remedy === "own" && a.remedyText.trim()) return { choice: "own", text: a.remedyText.trim() };
    return { choice: "agent", text: "" };
  }

  // Counts over the steps of the page. An answer for a step that no longer exists is not counted.
  function summarize(steps, state) {
    var s = { total: steps.length, pass: 0, fail: 0, unsure: 0, open: 0, decisions: {} };
    COUNTS.forEach(function (k) { s.decisions[k] = 0; });
    steps.forEach(function (step) {
      var a = answerOf(state, step.id);
      if (a) s[a] += 1; else s.open += 1;
      s.decisions[decisionOf(step, state)] += 1;
    });
    s.answered = s.total - s.open;
    s.allPass = s.total > 0 && s.pass === s.total;
    return s;
  }

  function summaryText(s) {
    if (!s.answered) return "No step answered yet.";
    var parts = [];
    COUNTS.forEach(function (k) {
      if (s.decisions[k] || k === "pass") parts.push(s.decisions[k] + " " + WORD[k]);
    });
    return s.answered + " of " + s.total + " steps answered: " + parts.join(", ") + ".";
  }

  function nextOpen(steps, state, fromIndex) {
    for (var k = 1; k <= steps.length; k++) {
      var i = (fromIndex + k) % steps.length;
      if (!answerOf(state, steps[i].id)) return i;
    }
    return -1;
  }

  function oneLine(text) {
    return String(text).replace(/\s+/g, " ").trim();
  }

  // The report as Markdown, for a comment on the prototype issue.
  function buildReport(meta, state, date) {
    var steps = meta.steps;
    var s = summarize(steps, state);
    var d = s.decisions;
    var lines = ["## Pre-order review of ZoneFlow rev A", ""];
    lines.push("Date: " + date + ". Board file `zoneflow-a.kicad_pcb`, SHA-256 `" + String(meta.board).slice(0, 16) + "`.");
    lines.push("Made with the review walkthrough: " + s.total + " steps.");
    lines.push("");
    lines.push("**Result: " + d.pass + " pass, " + d.accepted + " accepted, " + d.fail + " fail, " + d.rejected + " rejected, " +
               d.not_sure + " not sure, " + d.not_answered + " not answered.**");
    lines.push("");
    if (s.allPass) {
      lines.push("Every step is passed or accepted. What is still needed before an order: the owner's decision.");
    } else {
      lines.push("Not every step is passed or accepted. The board is not ready to order.");
    }
    lines.push("");

    function section(title, want) {
      var mine = steps.filter(function (step) { return want.indexOf(decisionOf(step, state)) >= 0; });
      if (!mine.length) return;
      lines.push("### " + title + " (" + mine.length + ")", "");
      var group = "";
      mine.forEach(function (step) {
        if (step.group_title !== group) { group = step.group_title; lines.push("**" + group + "**", ""); }
        var decision = decisionOf(step, state);
        var note = oneLine(entry(state, step.id).note);
        var done = decision === "pass" || decision === "accepted";
        lines.push("- " + (done ? "[x] " : "[ ] ") + step.title + (want.length > 1 ? " (" + WORD[decision] + ")" : "") +
                   (note ? "\n  > " + note : ""));
      });
      lines.push("");
    }

    section("Failed and rejected", ["fail", "rejected"]);
    section("Not sure", ["not_sure"]);
    section("Not answered", ["not_answered"]);
    section("Passed and accepted", ["pass", "accepted"]);
    lines.push("Nothing was built or measured. Each answer is one person's look at design files, a print, a datasheet or");
    lines.push("an order page. It does not show that the board works.");
    return lines.join("\n") + "\n";
  }

  // The hand-off to a coding agent ---------------------------------------------------------------

  // The decisions as data: docs/hardware/review/handoff.schema.json, version 1.
  function buildHandoffData(meta, state, date) {
    var s = summarize(meta.steps, state);
    var summary = { steps: s.total };
    COUNTS.forEach(function (k) { summary[k] = s.decisions[k]; });
    return {
      schema: HANDOFF_SCHEMA,
      schema_version: HANDOFF_VERSION,
      repository: meta.repo,
      issue: meta.issue,
      board: { revision: meta.revision, file: meta.board_file, sha256: meta.board },
      site_commit: meta.commit,
      reviewed_on: date,
      summary: summary,
      decisions: meta.steps.map(function (step) {
        var decision = decisionOf(step, state);
        var d = { step: step.id, title: step.title, group: step.group, group_title: step.group_title, kind: step.kind,
                  decision: decision, note: entry(state, step.id).note.trim() };
        if (decision === "pass" || decision === "accepted") return d;
        // What an agent acts on carries all it needs.
        d.designators = step.designators;
        d.nets = step.nets;
        d.position = step.position;
        d.pictures = step.pictures;
        d.files = step.files;
        d.source = step.source;
        d.findings = step.findings;
        if (decision === "fail" || decision === "rejected") d.remedy = remedyOf(step, state);
        return d;
      }),
    };
  }

  function num(v) {
    return String(Number(v));
  }

  function remedyLine(d) {
    var r = d.remedy;
    if (r.choice === "agent") return "Not chosen. The agent proposes one.";
    return (r.choice === "own" ? "The owner's own" : "Chosen from the list (`" + r.id + "`)") + ": " + oneLine(r.text);
  }

  // One decision that is not passed. reviewwalk/handoff.py writes the same text into the record.
  function detailLines(d) {
    var lines = ["### " + d.title, "", "- **Step:** `" + d.step + "`, " + d.group_title, "- **Decision:** " + WORD[d.decision],
                 "- **Owner's note:** " + (oneLine(d.note) || "none")];
    if (d.remedy) lines.push("- **Remedy:** " + remedyLine(d));
    if (d.designators.length) lines.push("- **Parts:** " + d.designators.join(", "));
    if (d.nets.length) lines.push("- **Nets:** " + d.nets.map(function (n) { return "`" + n + "`"; }).join(", "));
    if (d.position) {
      lines.push("- **Position:** (" + num(d.position.x_mm) + ", " + num(d.position.y_mm) + ") mm from the lower left corner of " +
                 "the board. KiCad's default display shows (" + num(d.position.kicad_x_mm) + ", " + num(d.position.kicad_y_mm) + ")");
    }
    if (d.files.length) {
      lines.push("- **Files a change would be made in:** " + d.files.map(function (f) { return "`" + f + "`"; }).join(", "));
    }
    if (d.pictures.length) lines.push("- **Pictures:** " + d.pictures.map(function (p) { return "`" + p.file + "`"; }).join(", "));
    lines.push("- **Comes from:** " + oneLine(d.source) + (d.findings.length ? ". Findings " + d.findings.join(", ") : ""));
    lines.push("");
    return lines;
  }

  function readableLines(doc) {
    var lines = [];
    [["Needs a change", ["fail", "rejected"], "No step failed and none was rejected."],
     ["Not sure: ask the owner", ["not_sure"], "The owner was sure of every answer given."],
     ["Not answered", ["not_answered"], "Every step has an answer."]].forEach(function (part) {
      var mine = doc.decisions.filter(function (d) { return part[1].indexOf(d.decision) >= 0; });
      lines.push("## " + part[0] + " (" + mine.length + ")", "");
      if (!mine.length) lines.push(part[2], "");
      mine.forEach(function (d) { lines = lines.concat(detailLines(d)); });
    });
    var done = doc.decisions.filter(function (d) { return d.decision === "pass" || d.decision === "accepted"; });
    lines.push("## Passed and accepted: change nothing (" + done.length + ")", "");
    done.forEach(function (d) {
      var note = oneLine(d.note);
      lines.push("- " + WORD[d.decision] + ": " + d.title + " (`" + d.step + "`)" + (note ? ". Note: " + note : ""));
    });
    lines.push("");
    return lines;
  }

  function handoffName(date) {
    return "zoneflow-rev-a-review-handoff-" + date + ".md";
  }

  // One Markdown file, complete in itself: instructions for the agent, the decisions as JSON, the same as text.
  function buildHandoffFile(meta, state, date) {
    var doc = buildHandoffData(meta, state, date);
    var s = doc.summary;
    var lines = [
      "# ZoneFlow rev " + meta.revision + ": hand-off of the pre-order review, " + date, "",
      "The review walkthrough of the ZoneFlow documentation wrote this file in the owner's browser. It holds the owner's",
      "decisions on the " + s.steps + " steps of the pre-order review of the revision " + meta.revision + " board. It is complete " +
      "in itself: give it to a", "coding agent that has the repository.", "",
      "**" + s.pass + " pass, " + s.accepted + " accepted, " + s.fail + " fail, " + s.rejected + " rejected, " + s.not_sure +
      " not sure, " + s.not_answered + " not answered.**", "",
      "## For the agent: read this first", "",
      "- **Repository:** `" + meta.repo + "`. **Board:** revision " + meta.revision + ", `" + meta.board_file + "`.",
      "- **What was reviewed:** the board file with SHA-256 `" + meta.board + "`.",
      "- **The walkthrough page was built from commit** `" + meta.commit + "`.",
      "- **Rules:** read `CLAUDE.md` at the root of the repository before anything else and follow it: the Git rules, the",
      "  honesty rules and the hardware flow. Then read `docs/design/system-spec.md`. No board has been made. Do not write",
      "  that anything works or is tested unless you ran the check, and name the check.", "",
      "Do these in order.", "",
      "1. **Check that you have the board that was reviewed.** `sha256sum " + meta.board_file + "` must give the hash above.",
      "   If it does not, check out the commit that has it, or stop and say so. Do not act on a review of another board.",
      "2. **Record the review.** Save this file in the repository's root and run",
      "   `uv run --project tools/reviewwalk reviewwalk record " + handoffName(date) + "`. It checks the decisions against",
      "   the schema and the board, and writes `docs/design/reviews/" + date + "-pre-order.md` with the JSON beside it. Commit",
      "   those in a pull request of their own, without this file. Then post a comment on issue #" + meta.issue + " with the",
      "   counts and with the failed, rejected and unsure steps.",
      "3. **For each step under \"Needs a change\"** open one pull request with that one change. Use the remedy the owner",
      "   chose. Where the owner chose none, propose one in the pull request and say why. The files named are where the change",
      "   is made. Never edit a generated file by hand. A change to the board needs `make` in `hardware/zoneflow-a/` and",
      "   the generated files committed with it, the pictures in `output/review/` among them. Name the step in the pull request.",
      "4. **For each step under \"Not sure\" and \"Not answered\"** ask the owner on issue #" + meta.issue + ". Do not guess,",
      "   and change nothing for these steps.",
      "5. **Change nothing for a passed or accepted step.**",
      "6. **Never order boards,** and do not change any text that tells readers not to order. Ordering is the owner's decision.",
      "7. **After your changes are merged** the board file has another hash. Tell the owner which steps to look at again. The",
      "   walkthrough page takes this file back in and warns that the board has changed.", "",
      "The JSON follows `docs/hardware/review/handoff.schema.json`, version " + HANDOFF_VERSION + ". Positions are in millimetres " +
      "from the lower left", "corner of the board seen from the component side, x to the right and y up.", "",
      "# Decisions as data", "", "```json", JSON.stringify(doc, null, 1), "```", "",
      "# Decisions as text", "",
    ];
    return lines.concat(readableLines(doc)).join("\n");
  }

  // The rules of the schema that matter for taking a file back in. reviewwalk record checks all of them.
  function parseHandoff(text) {
    var candidates = [];
    text = String(text || "");
    if (/^\s*\{/.test(text)) {
      candidates.push(text);
    } else {
      var re = /^```json[ \t]*\r?\n([\s\S]*?)\r?\n```[ \t]*$/gm;
      var m;
      while ((m = re.exec(text))) candidates.push(m[1]);
    }
    var doc = null;
    candidates.forEach(function (c) {
      var d;
      try { d = JSON.parse(c); } catch (e) { return; }
      if (!doc && d && typeof d === "object" && d.schema === HANDOFF_SCHEMA) doc = d;
    });
    if (!doc) return { doc: null, errors: ["This is not a hand-off file: it has no JSON block with \"schema\": \"" + HANDOFF_SCHEMA + "\"."] };
    var errors = [];
    if (doc.schema_version !== HANDOFF_VERSION) {
      errors.push("The file is of schema version " + String(doc.schema_version) + ". This page reads version " + HANDOFF_VERSION + ".");
    }
    if (!doc.board || typeof doc.board.sha256 !== "string" || !/^[0-9a-f]{64}$/.test(doc.board.sha256)) {
      errors.push("The file does not say which board file was reviewed.");
    }
    if (!Array.isArray(doc.decisions)) {
      errors.push("The file has no list of decisions.");
    } else {
      doc.decisions.forEach(function (d, i) {
        if (!d || typeof d !== "object" || typeof d.step !== "string" || !Object.prototype.hasOwnProperty.call(ANSWER_OF, d.decision) ||
            (d.note !== undefined && typeof d.note !== "string")) {
          errors.push("Decision " + (i + 1) + " has no step, or a decision that is not one of " + COUNTS.join(", ") + ".");
        }
      });
    }
    return { doc: errors.length ? null : doc, errors: errors };
  }

  // The answers a hand-off file holds, for the steps this page has.
  function importHandoff(meta, doc) {
    var state = emptyState(doc.board.sha256);
    var known = {};
    meta.steps.forEach(function (s) { known[s.id] = s; });
    var result = { state: state, imported: 0, unknown: [], boardDiffers: doc.board.sha256 !== meta.board, board: doc.board.sha256 };
    doc.decisions.forEach(function (d) {
      var step = known[d.step];
      if (!step) { result.unknown.push(d.step); return; }
      var a = { answer: ANSWER_OF[d.decision], note: d.note || "" };
      var r = d.remedy;
      if (a.answer === "fail" && r && typeof r === "object") {
        if (r.choice === "listed" && step.remedies.some(function (x) { return x.id === r.id; })) {
          a.remedy = r.id;
        } else if (r.choice !== "agent" && typeof r.text === "string" && r.text.trim()) {
          // The owner's own words, or a listed remedy that this page no longer offers: keep the words.
          a.remedy = "own";
          a.remedyText = r.text;
        }
      }
      put(state, d.step, a);
      if (state.answers[d.step]) result.imported += 1;
    });
    return result;
  }

  // What a selected shape of a picture is, from the attributes the picture generator gave it.
  function describeShape(shape) {
    if (!shape || !shape.layer) return "Nothing of the board at this point.";
    var on = " on " + shape.layer;
    var net = shape.net ? "net " + shape.net : "no net";
    if (shape.pad !== undefined && shape.pad !== null) {
      var dot = shape.pad.indexOf(".");
      var ref = shape.pad.slice(0, dot), number = shape.pad.slice(dot + 1);
      return (number ? "Pad " + number + " of " + ref : "A pad of " + ref + " without a number") + ", " + net + "," + on + ".";
    }
    if (shape.net !== undefined && shape.net !== null) {
      if (shape.zone) return "Filled zone, " + net + "," + on + ".";
      if (shape.tag === "circle") return "Via, " + net + ". Selected" + on + ".";
      return (shape.stroked ? "Track, " : "Copper shape, ") + net + "," + on + ".";
    }
    if (shape.ref) return "Outline or text of " + shape.ref + "," + on + ".";
    return "A drawing of the board itself," + on + ".";
  }

  function parseHash(hash) {
    var h = String(hash || "").replace(/^#/, "");
    if (h.indexOf("step-") === 0) return { panel: "steps", step: h.slice(5) };
    if (h.indexOf("finding-") === 0) return { panel: "findings", finding: h.slice(8) };
    if (h.indexOf("group-") === 0) return { panel: "steps", group: h.slice(6) };
    if (h === "steps" || h === "findings" || h === "result" || h === "start") return { panel: h };
    return { panel: "" };
  }

  // Zoom about a point of the view, kept between the fitted size and 12 times that.
  function zoomAt(view, factor, cx, cy) {
    var scale = Math.min(12, Math.max(1, view.scale * factor));
    var k = scale / view.scale;
    return clampView({ scale: scale, x: cx - (cx - view.x) * k, y: cy - (cy - view.y) * k }, view.w, view.h, view);
  }

  function clampView(v, w, h, old) {
    var out = { scale: v.scale, x: v.x, y: v.y, w: w !== undefined ? w : old.w, h: h !== undefined ? h : old.h };
    // The picture may not leave the frame: at scale 1 it sits still.
    out.x = Math.min(0, Math.max(out.w - out.w * out.scale, out.x));
    out.y = Math.min(0, Math.max(out.h - out.h * out.scale, out.y));
    return out;
  }

  // ---------------------------------------------------------------------------------------------
  // The page

  function start(win) {
    var doc = win.document;
    function ready(fn) {
      if (doc.readyState === "loading") doc.addEventListener("DOMContentLoaded", fn); else fn();
    }
    ready(function () {
      var rootEl = doc.getElementById("zf-review");
      var metaEl = doc.getElementById("zf-review-meta");
      if (!rootEl || !metaEl) return;
      var meta;
      try { meta = JSON.parse(metaEl.textContent); } catch (e) { return; }
      run(win, doc, rootEl, meta);
    });
  }

  function run(win, doc, rootEl, meta) {
    var steps = meta.steps;
    var index = {};
    steps.forEach(function (s, i) { index[s.id] = i; });
    var $ = function (sel, el) { return (el || rootEl).querySelector(sel); };
    var $$ = function (sel, el) { return Array.prototype.slice.call((el || rootEl).querySelectorAll(sel)); };

    // Storage that may be missing, full or forbidden. The page then keeps the answers in memory.
    var canStore = true;
    var state;
    try {
      state = parseState(win.localStorage.getItem(STORAGE_KEY), meta.board);
    } catch (e) {
      canStore = false;
      state = emptyState(meta.board);
    }
    // Answers that were given for another board file than the one this page shows.
    function otherBoard() {
      return !!state.board && state.board !== meta.board && Object.keys(state.answers).length > 0;
    }
    function save() {
      if (!canStore) return;
      try { win.localStorage.setItem(STORAGE_KEY, JSON.stringify(state)); } catch (e) { canStore = false; storageNote(); }
    }
    function storageNote() {
      var note = $(".zf-storage-note");
      var parts = [];
      if (!canStore) {
        parts.push("This browser does not let the page store your answers. They are kept until you close or reload this tab. " +
                   "Save the hand-off file before you leave: it brings them back.");
      }
      if (otherBoard()) {
        parts.push("These answers were given for another board file (SHA-256 " + state.board.slice(0, 12) + ") than the one this " +
                   "page shows (" + String(meta.board).slice(0, 12) + "). Check them against the pictures here. Until you " +
                   "confirm them, the page writes no hand-off file.");
      }
      $(".zf-storage-text", note).textContent = parts.join(" ");
      $(".zf-confirm-board", note).hidden = !otherBoard();
      note.hidden = !parts.length;
    }
    $(".zf-confirm-board").addEventListener("click", function () {
      state.board = meta.board;
      save();
      storageNote();
      renderResult();
    });
    // The first answer on an empty page is an answer about the board the page shows.
    function touch() {
      if (!Object.keys(state.answers).length) state.board = meta.board;
    }

    rootEl.classList.add("zf-js");
    var panels = $$(".zf-panel");
    var tabs = $$(".zf-tabs a");
    var articles = $$(".zf-step");
    var current = 0;

    function showPanel(name, focus) {
      panels.forEach(function (p) { p.hidden = p.getAttribute("data-panel") !== name; });
      tabs.forEach(function (a) {
        var on = a.getAttribute("href") === "#" + name;
        if (on) a.setAttribute("aria-current", "page"); else a.removeAttribute("aria-current");
      });
      if (name === "result") renderResult();
      if (focus) {
        var h = $('.zf-panel[data-panel="' + name + '"] h2');
        if (h) { h.setAttribute("tabindex", "-1"); h.focus({ preventScroll: true }); }
        rootEl.scrollIntoView({ block: "start" });
      }
    }

    function showStep(i, focus) {
      current = Math.max(0, Math.min(steps.length - 1, i));
      var id = steps[current].id;
      var group = articles[current].getAttribute("data-group");
      articles.forEach(function (a, k) { a.hidden = k !== current; });
      $$(".zf-group").forEach(function (g) {
        // The introduction of a group is shown with its first step.
        var first = steps.findIndex(function (s, k) { return articles[k].getAttribute("data-group") === g.getAttribute("data-group"); });
        g.hidden = !(g.getAttribute("data-group") === group && first === current);
      });
      $$(".zf-list a").forEach(function (a) {
        if (a.getAttribute("data-step") === id) {
          a.setAttribute("aria-current", "step");
          if (a.scrollIntoView) a.scrollIntoView({ block: "nearest" });
        } else {
          a.removeAttribute("aria-current");
        }
      });
      $(".zf-prev").disabled = current === 0;
      $(".zf-next").textContent = current === steps.length - 1 ? "To the result" : "Next step";
      if (focus) {
        articles[current].focus({ preventScroll: true });
        $(".zf-progress").scrollIntoView({ block: "start" });
      }
    }

    function route(focus) {
      var r = parseHash(win.location.hash);
      if (r.step !== undefined && index[r.step] !== undefined) {
        showPanel("steps");
        showStep(index[r.step], focus);
      } else if (r.group) {
        var i = articles.findIndex(function (a) { return a.getAttribute("data-group") === r.group; });
        showPanel("steps");
        showStep(i < 0 ? 0 : i, focus);
      } else if (r.finding) {
        showPanel("findings");
        setFilter("all");
        var card = doc.getElementById("finding-" + r.finding);
        if (card) { card.scrollIntoView({ block: "start" }); card.classList.add("zf-flash"); }
      } else if (r.panel === "steps") {
        showPanel("steps", focus);
        var open = nextOpen(steps, state, steps.length - 1);
        showStep(open < 0 ? 0 : open);
      } else if (r.panel) {
        showPanel(r.panel, focus);
      } else {
        showPanel("start");
        showStep(0);
      }
    }

    function go(hash) {
      if (win.location.hash === hash) route(true); else win.location.hash = hash;
    }

    // Answers ------------------------------------------------------------------------------------
    function paint() {
      var s = summarize(steps, state);
      var bar = $(".zf-bar");
      bar.setAttribute("aria-valuenow", String(s.answered));
      bar.setAttribute("aria-valuetext", summaryText(s));
      ["pass", "unsure", "fail"].forEach(function (k) {
        $(".zf-bar-" + k).style.width = (100 * s[k] / s.total) + "%";
      });
      $(".zf-progress-text").textContent = summaryText(s);
      $$(".zf-list a").forEach(function (a) {
        var step = steps[index[a.getAttribute("data-step")]];
        var ans = answerOf(state, step.id);
        a.setAttribute("data-answer", ans);
        $(".zf-nav-state", a).textContent = ans ? " (" + WORD[decisionOf(step, state)] + ")" : "";
      });
      $$(".zf-nav-group").forEach(function (g) {
        var links = $$("a", g);
        var done = links.filter(function (a) { return a.getAttribute("data-answer"); }).length;
        var title = $(".zf-nav-title", g);
        title.setAttribute("data-count", done + "/" + links.length);
      });
      articles.forEach(function (article) {
        var id = article.getAttribute("data-step");
        var ans = answerOf(state, id);
        article.setAttribute("data-answer", ans);
        $$('input[type="radio"]', article).forEach(function (r) { r.checked = r.value === ans; });
        $(".zf-clear-one", article).hidden = !ans;
        // The remedy is asked for when the step failed or was rejected, and only then.
        var a = state.answers[id] || {};
        var select = $(".zf-remedy-select", article);
        $(".zf-remedy", article).hidden = ans !== "fail";
        select.value = a.remedy && $$("option", select).some(function (o) { return o.value === a.remedy; }) ? a.remedy : "";
        $(".zf-remedy-own", article).hidden = select.value !== "own";
        if (doc.activeElement !== $(".zf-remedy-text", article)) $(".zf-remedy-text", article).value = a.remedyText || "";
      });
    }

    articles.forEach(function (article) {
      var id = article.getAttribute("data-step");
      var note = $(".zf-note", article);
      note.value = (state.answers[id] && state.answers[id].note) || "";
      var select = $(".zf-remedy-select", article);
      var own = $(".zf-remedy-text", article);
      article.addEventListener("change", function (ev) {
        if (ev.target.type === "radio") { touch(); setAnswer(state, id, ev.target.value); save(); paint(); }
      });
      note.addEventListener("input", function () { touch(); setNote(state, id, note.value); save(); });
      select.addEventListener("change", function () {
        setRemedy(state, id, select.value, own.value);
        save();
        paint();
        if (select.value === "own") own.focus();
      });
      own.addEventListener("input", function () { setRemedy(state, id, "own", own.value); save(); });
      $(".zf-clear-one", article).addEventListener("click", function () { setAnswer(state, id, ""); save(); paint(); storageNote(); });
    });

    $(".zf-prev").addEventListener("click", function () { if (current > 0) go("#step-" + steps[current - 1].id); });
    $(".zf-next").addEventListener("click", function () {
      if (current < steps.length - 1) go("#step-" + steps[current + 1].id); else go("#result");
    });
    $(".zf-begin").addEventListener("click", function (ev) { ev.preventDefault(); go("#step-" + steps[0].id); });

    // On the window and in the capture phase: the site's theme uses N and P for "next page" and
    // "previous page". While the steps are on the screen these keys belong to the steps.
    function take(ev) { ev.preventDefault(); ev.stopImmediatePropagation(); }
    win.addEventListener("keydown", function (ev) {
      if (ev.defaultPrevented || ev.altKey || ev.ctrlKey || ev.metaKey) return;
      var t = ev.target;
      if (t && (t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable ||
                (t.tagName === "INPUT" && t.type !== "radio"))) return;
      if (lightbox.open || $('.zf-panel[data-panel="steps"]').hidden) return;
      // N and P work wherever the focus is, outside a text field.
      if (ev.key === "n" || ev.key === "N") { take(ev); $(".zf-next").click(); return; }
      if (ev.key === "p" || ev.key === "P") { take(ev); $(".zf-prev").click(); return; }
      // Arrow keys move between the radio buttons of a group: leave them to the browser there.
      if (t && t.tagName === "INPUT" && t.type === "radio") return;
      if (ev.key === "ArrowRight") { take(ev); $(".zf-next").click(); }
      if (ev.key === "ArrowLeft") { take(ev); $(".zf-prev").click(); }
    }, true);

    // Findings -----------------------------------------------------------------------------------
    function setFilter(severity) {
      $$(".zf-filter").forEach(function (b) {
        b.setAttribute("aria-pressed", String(b.getAttribute("data-severity") === severity));
      });
      $$(".zf-finding").forEach(function (card) {
        card.hidden = severity !== "all" && card.getAttribute("data-severity") !== severity;
      });
    }
    $$(".zf-filter").forEach(function (b) {
      b.addEventListener("click", function () { setFilter(b.getAttribute("data-severity")); });
    });

    // Result -------------------------------------------------------------------------------------
    function el(tag, text, cls) {
      var e = doc.createElement(tag);
      if (text) e.textContent = text;
      if (cls) e.className = cls;
      return e;
    }
    function renderResult() {
      var s = summarize(steps, state);
      var summary = $(".zf-result-summary");
      summary.textContent = summaryText(s) + (s.allPass ? " Every step is passed or accepted." : "");
      summary.setAttribute("data-state", s.allPass ? "pass" : s.fail ? "fail" : "open");
      var box = $(".zf-result-lists");
      box.textContent = "";
      [[["fail", "rejected"], "Failed and rejected", "fail"], [["not_sure"], "Not sure", "unsure"],
       [["not_answered"], "Not answered", "open"]].forEach(function (part) {
        var mine = steps.filter(function (step) { return part[0].indexOf(decisionOf(step, state)) >= 0; });
        if (!mine.length) return;
        box.appendChild(el("h3", part[1] + " (" + mine.length + ")"));
        var ul = el("ul", "", "zf-result-list zf-result-" + part[2]);
        mine.forEach(function (step) {
          var li = el("li");
          var a = el("a", step.title);
          a.href = "#step-" + step.id;
          li.appendChild(a);
          var decision = decisionOf(step, state);
          li.appendChild(el("span", " " + step.group_title + (part[0].length > 1 ? ", " + WORD[decision] : ""), "zf-result-group"));
          var note = oneLine((state.answers[step.id] || {}).note || "");
          if (note) li.appendChild(el("blockquote", note));
          if (decision === "fail" || decision === "rejected") {
            var r = remedyOf(step, state);
            li.appendChild(el("p", "Remedy: " + (r.choice === "agent" ? "not chosen, the agent proposes one." : oneLine(r.text).replace(/`/g, "")),
                              "zf-result-remedy"));
          }
          ul.appendChild(li);
        });
        box.appendChild(ul);
      });
      var blocked = otherBoard();
      $(".zf-handoff-copy").disabled = blocked;
      $(".zf-handoff-save").disabled = blocked;
      if (blocked) handoffStatus.textContent = "Confirm the answers against this board first. The note above the steps has the button.";
      else if (/^Confirm the answers/.test(handoffStatus.textContent)) handoffStatus.textContent = "";
    }

    function copyText(area, statusEl) {
      function fallback() {
        area.focus();
        area.select();
        var ok = false;
        try { ok = doc.execCommand("copy"); } catch (e) { ok = false; }
        statusEl.textContent = ok ? "Copied." : "Could not copy. The text is selected: copy it with Ctrl+C.";
      }
      if (win.navigator.clipboard && win.navigator.clipboard.writeText) {
        win.navigator.clipboard.writeText(area.value).then(function () { statusEl.textContent = "Copied."; }, fallback);
      } else {
        fallback();
      }
    }
    function saveText(text, name, statusEl) {
      try {
        var url = win.URL.createObjectURL(new win.Blob([text], { type: "text/markdown" }));
        var a = doc.createElement("a");
        a.href = url;
        a.download = name;
        doc.body.appendChild(a);
        a.click();
        a.remove();
        win.setTimeout(function () { win.URL.revokeObjectURL(url); }, 1000);
        statusEl.textContent = "Saved as " + name + ".";
      } catch (e) {
        statusEl.textContent = "Could not save a file. Copy the text instead.";
      }
    }

    var report = doc.getElementById("zf-report");
    var status = $(".zf-report-status");
    function today() {
      var d = new Date();
      return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0");
    }
    $(".zf-make-report").addEventListener("click", function () {
      report.value = buildReport(meta, state, today());
      $(".zf-copy-report").disabled = false;
      $(".zf-download-report").disabled = false;
      status.textContent = "The report is written below. It is not sent anywhere.";
      report.focus();
      report.select();
    });
    $(".zf-copy-report").addEventListener("click", function () { copyText(report, status); });
    $(".zf-download-report").addEventListener("click", function () {
      saveText(report.value, "zoneflow-rev-a-review-" + today() + ".md", status);
    });

    // The hand-off file for a coding agent, and the way back in ---------------------------------
    var handoffArea = doc.getElementById("zf-handoff-file");
    var handoffStatus = $(".zf-handoff-status");
    function writeHandoff() {
      handoffArea.value = buildHandoffFile(meta, state, today());
      return handoffArea.value;
    }
    $(".zf-handoff-copy").addEventListener("click", function () {
      writeHandoff();
      $(".zf-handoff-show").open = true;
      copyText(handoffArea, handoffStatus);
    });
    $(".zf-handoff-save").addEventListener("click", function () {
      saveText(writeHandoff(), handoffName(today()), handoffStatus);
    });
    $(".zf-handoff-show").addEventListener("toggle", function () {
      if ($(".zf-handoff-show").open && !otherBoard()) writeHandoff();
    });

    var importArea = doc.getElementById("zf-import-text");
    var importStatus = $(".zf-import-status");
    function say(lines, state) {
      importStatus.textContent = "";
      importStatus.setAttribute("data-state", state);
      lines.forEach(function (line) { importStatus.appendChild(el("p", line)); });
    }
    function takeIn(text) {
      var parsed = parseHandoff(text);
      if (!parsed.doc) { say(parsed.errors, "fail"); return; }
      var result = importHandoff(meta, parsed.doc);
      var have = Object.keys(state.answers).length;
      if (have && !win.confirm("Replace the " + have + " answers on this page with the " + result.imported + " in the file?")) {
        say(["Nothing was changed."], "open");
        return;
      }
      state = result.state;
      save();
      articles.forEach(function (article) {
        var a = state.answers[article.getAttribute("data-step")] || {};
        $(".zf-note", article).value = a.note || "";
        $(".zf-remedy-text", article).value = a.remedyText || "";
      });
      var lines = [result.imported + " answers were taken from the file, dated " + String(parsed.doc.reviewed_on) + "."];
      if (result.unknown.length) {
        lines.push("The file has " + result.unknown.length + " steps that this page does not have. They were left out: " +
                   result.unknown.join(", ") + ".");
      }
      if (result.boardDiffers) {
        lines.push("Warning: the file is a review of another board file (SHA-256 " + result.board.slice(0, 12) + ") than the one this " +
                   "page shows (" + String(meta.board).slice(0, 12) + "). The board has changed since. Check each answer against " +
                   "the pictures here, then confirm them with the button above the steps.");
      }
      say(lines, result.boardDiffers ? "warn" : "pass");
      storageNote();
      paint();
      renderResult();
    }
    $(".zf-import-go").addEventListener("click", function () {
      if (!importArea.value.trim()) { say(["Paste the hand-off file into the field first, or choose the file."], "open"); return; }
      takeIn(importArea.value);
    });
    $(".zf-import-file").addEventListener("change", function (ev) {
      var file = ev.target.files && ev.target.files[0];
      if (!file) return;
      if (file.size > 2000000) { say(["The file is too large to be a hand-off file."], "fail"); return; }
      var reader = new win.FileReader();
      reader.onload = function () { importArea.value = String(reader.result); takeIn(importArea.value); };
      reader.onerror = function () { say(["Could not read the file."], "fail"); };
      reader.readAsText(file);
      ev.target.value = "";
    });

    $(".zf-reset").addEventListener("click", function () {
      if (!win.confirm("Delete all your answers and notes on this page?")) return;
      state = emptyState(meta.board);
      if (canStore) { try { win.localStorage.removeItem(STORAGE_KEY); } catch (e) { canStore = false; } }
      $$(".zf-note").forEach(function (n) { n.value = ""; });
      $$(".zf-remedy-text").forEach(function (n) { n.value = ""; });
      report.value = "";
      handoffArea.value = "";
      storageNote();
      paint();
      renderResult();
      status.textContent = "All answers deleted.";
    });

    // Pictures: select one to enlarge it, then drag, wheel or use the keys --------------------------
    var lightbox = doc.getElementById("zf-lightbox");
    var frame = $(".zf-lb-view", lightbox);
    var content = $(".zf-lb-content", lightbox);
    var view = { scale: 1, x: 0, y: 0, w: 1, h: 1 };
    var opener = null;
    function apply() {
      content.style.transform = "translate(" + view.x + "px," + view.y + "px) scale(" + view.scale + ")";
    }
    function fit() {
      view = { scale: 1, x: 0, y: 0, w: content.offsetWidth, h: content.offsetHeight };
      apply();
    }
    function zoom(factor, cx, cy) {
      view.w = content.offsetWidth;
      view.h = content.offsetHeight;
      view = zoomAt(view, factor, cx === undefined ? view.w / 2 : cx, cy === undefined ? view.h / 2 : cy);
      apply();
    }
    function pan(dx, dy) {
      view = clampView({ scale: view.scale, x: view.x + dx, y: view.y + dy }, content.offsetWidth, content.offsetHeight, view);
      apply();
    }
    function local(ev) {
      var r = frame.getBoundingClientRect();
      var c = content.getBoundingClientRect();
      // Position in the unscaled picture's box, which starts where the content box starts at scale 1.
      return { x: ev.clientX - r.left - (r.width - content.offsetWidth) / 2, y: ev.clientY - r.top - (r.height - content.offsetHeight) / 2, c: c };
    }
    // A picture drawn from the board file carries, on each shape, the net, the part and the pad that
    // the board file gives it. Enlarged, the picture is put into the page as SVG, so that its layers
    // can be shown and hidden and a shape can be selected. The page adds nothing of its own to that.
    var tools = $(".zf-lb-tools", lightbox);
    var layerBox = $(".zf-lb-layers", lightbox);
    var info = $(".zf-lb-info", lightbox);
    var picked = null;
    var HINT = "Select a pad, a track or a zone to read its part and net from the board file.";
    function unpick() {
      if (picked) picked.classList.toggle("zf-picked", false);
      picked = null;
    }
    function interactive(svg) {
      svg.setAttribute("width", "100%");
      svg.setAttribute("height", "100%");
      content.textContent = "";
      content.appendChild(svg);
      layerBox.textContent = "";
      Array.prototype.forEach.call(svg.querySelectorAll("g[data-layer]"), function (g) {
        var label = el("label", "", "zf-lb-layer");
        var box = doc.createElement("input");
        box.type = "checkbox";
        box.checked = true;
        box.addEventListener("change", function () {
          g.style.display = box.checked ? "" : "none";
          if (picked && g.contains(picked)) pick(null);
        });
        label.appendChild(box);
        label.appendChild(doc.createTextNode(" " + g.getAttribute("data-layer")));
        layerBox.appendChild(label);
      });
      info.textContent = HINT;
      tools.hidden = false;
    }
    function pick(target) {
      unpick();
      var layer = target && target.closest ? target.closest("g[data-layer]") : null;
      if (!layer) { info.textContent = target === null ? HINT : describeShape(null); return; }
      var netEl = target.closest("[data-n]");
      var refEl = target.closest("[data-r]");
      info.textContent = describeShape({
        layer: layer.getAttribute("data-layer"),
        pad: target.getAttribute("data-p"),
        net: netEl ? netEl.getAttribute("data-n") : null,
        ref: refEl ? refEl.getAttribute("data-r") : null,
        zone: target.hasAttribute("data-zone"),
        tag: target.tagName.toLowerCase(),
        stroked: target.getAttribute("fill") === "none",
      });
      picked = target;
      picked.classList.toggle("zf-picked", true);
    }
    $$(".zf-zoom").forEach(function (button) {
      button.addEventListener("click", function () {
        opener = button;
        content.textContent = "";
        tools.hidden = true;
        picked = null;
        var copy = button.firstElementChild.cloneNode(true);
        if (copy.tagName === "IMG") copy.loading = "eager"; else $$("img", copy).forEach(function (img) { img.loading = "eager"; });
        content.appendChild(copy);
        var cap = button.parentNode.querySelector("figcaption");
        $(".zf-lb-caption", lightbox).textContent = cap ? cap.textContent : "";
        if (lightbox.showModal) lightbox.showModal(); else lightbox.setAttribute("open", "");
        fit();
        frame.focus();
        var src = button.getAttribute("data-svg");
        if (!src || !win.fetch || !win.DOMParser) return;
        // The same file the picture shows, read again as text. If that fails, the picture stays as it is.
        win.fetch(src).then(function (r) { return r.ok ? r.text() : Promise.reject(new Error("not found")); }).then(function (text) {
          var parsed = new win.DOMParser().parseFromString(text, "image/svg+xml");
          var svg = parsed.documentElement;
          if (opener !== button || !lightbox.open || !svg || svg.tagName.toLowerCase() !== "svg" || parsed.querySelector("parsererror") ||
              svg.querySelector("script, foreignObject")) return;
          interactive(doc.importNode(svg, true));
        }).catch(function () { tools.hidden = true; });
      });
    });
    function closeLightbox() {
      if (lightbox.close) lightbox.close(); else lightbox.removeAttribute("open");
    }
    lightbox.addEventListener("close", function () { if (opener) opener.focus(); });
    $(".zf-lb-close", lightbox).addEventListener("click", closeLightbox);
    $(".zf-lb-in", lightbox).addEventListener("click", function () { zoom(1.5); });
    $(".zf-lb-out", lightbox).addEventListener("click", function () { zoom(1 / 1.5); });
    $(".zf-lb-fit", lightbox).addEventListener("click", fit);
    lightbox.addEventListener("click", function (ev) { if (ev.target === lightbox) closeLightbox(); });
    frame.addEventListener("wheel", function (ev) {
      ev.preventDefault();
      var p = local(ev);
      zoom(ev.deltaY < 0 ? 1.25 : 0.8, p.x, p.y);
    }, { passive: false });
    frame.addEventListener("keydown", function (ev) {
      var step = 60;
      if (ev.key === "+" || ev.key === "=") zoom(1.5);
      else if (ev.key === "-") zoom(1 / 1.5);
      else if (ev.key === "0") fit();
      else if (ev.key === "ArrowLeft") pan(step, 0);
      else if (ev.key === "ArrowRight") pan(-step, 0);
      else if (ev.key === "ArrowUp") pan(0, step);
      else if (ev.key === "ArrowDown") pan(0, -step);
      else return;
      ev.preventDefault();
    });
    var pointers = {};
    var pinch = 0;
    var pressed = null;
    frame.addEventListener("pointerdown", function (ev) {
      pointers[ev.pointerId] = { x: ev.clientX, y: ev.clientY };
      pressed = Object.keys(pointers).length === 1 ? { x: ev.clientX, y: ev.clientY, moved: false } : null;
      if (frame.setPointerCapture) frame.setPointerCapture(ev.pointerId);
      frame.classList.add("zf-dragging");
    });
    frame.addEventListener("pointermove", function (ev) {
      var p = pointers[ev.pointerId];
      if (!p) return;
      if (pressed && Math.hypot(ev.clientX - pressed.x, ev.clientY - pressed.y) > 5) pressed.moved = true;
      var ids = Object.keys(pointers);
      if (ids.length === 1) {
        pan(ev.clientX - p.x, ev.clientY - p.y);
      }
      pointers[ev.pointerId] = { x: ev.clientX, y: ev.clientY };
      if (ids.length === 2) {
        var a = pointers[ids[0]], b = pointers[ids[1]];
        var d = Math.hypot(a.x - b.x, a.y - b.y);
        if (pinch) {
          var mid = local({ clientX: (a.x + b.x) / 2, clientY: (a.y + b.y) / 2 });
          zoom(d / pinch, mid.x, mid.y);
        }
        pinch = d;
      }
    });
    function release(ev) {
      delete pointers[ev.pointerId];
      pinch = 0;
      if (!Object.keys(pointers).length) frame.classList.remove("zf-dragging");
    }
    // A press that did not move is a selection. What is selected is the shape the browser finds at
    // that point of the picture, which is the one on top that is shown.
    frame.addEventListener("pointerup", function (ev) {
      if (pressed && !pressed.moved && !tools.hidden) {
        var under = doc.elementsFromPoint ? doc.elementsFromPoint(ev.clientX, ev.clientY) : [doc.elementFromPoint(ev.clientX, ev.clientY)];
        var target = under.filter(function (e) { return e && content.contains(e) && e !== content; })[0];
        pick(target && target.tagName.toLowerCase() !== "svg" ? target : undefined);
      }
      pressed = null;
    });
    frame.addEventListener("pointerup", release);
    frame.addEventListener("pointercancel", release);
    frame.addEventListener("dblclick", function (ev) {
      // In a picture whose shapes can be selected, two presses are two selections, not a zoom.
      if (!tools.hidden) return;
      var p = local(ev);
      if (view.scale > 1.01) fit(); else zoom(3, p.x, p.y);
    });

    win.addEventListener("hashchange", function () { route(true); });
    storageNote();
    paint();
    route(false);
  }

  return {
    STORAGE_KEY: STORAGE_KEY, emptyState: emptyState, parseState: parseState, setAnswer: setAnswer, setNote: setNote,
    setRemedy: setRemedy, answerOf: answerOf, decisionOf: decisionOf, remedyOf: remedyOf, summarize: summarize,
    summaryText: summaryText, nextOpen: nextOpen, buildReport: buildReport, buildHandoffData: buildHandoffData,
    buildHandoffFile: buildHandoffFile, handoffName: handoffName, parseHandoff: parseHandoff, importHandoff: importHandoff,
    describeShape: describeShape, parseHash: parseHash, zoomAt: zoomAt, clampView: clampView, start: start,
  };
});
