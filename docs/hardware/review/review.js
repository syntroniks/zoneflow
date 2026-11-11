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
  var WORD = { pass: "pass", fail: "fail", unsure: "not sure" };

  function emptyState(board) {
    return { version: 1, board: board || "", answers: {} };
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
      var answer = ANSWERS.indexOf(a.answer) >= 0 ? a.answer : "";
      var note = typeof a.note === "string" ? a.note.slice(0, 4000) : "";
      if (answer || note) state.answers[id] = { answer: answer, note: note };
    });
    return state;
  }

  function setAnswer(state, id, answer) {
    var a = state.answers[id] || { answer: "", note: "" };
    a.answer = ANSWERS.indexOf(answer) >= 0 ? answer : "";
    if (a.answer || a.note) state.answers[id] = a; else delete state.answers[id];
    return state;
  }

  function setNote(state, id, note) {
    var a = state.answers[id] || { answer: "", note: "" };
    a.note = String(note || "").slice(0, 4000);
    if (a.answer || a.note) state.answers[id] = a; else delete state.answers[id];
    return state;
  }

  function answerOf(state, id) {
    return (state.answers[id] && state.answers[id].answer) || "";
  }

  // Counts over the steps of the page. An answer for a step that no longer exists is not counted.
  function summarize(steps, state) {
    var s = { total: steps.length, pass: 0, fail: 0, unsure: 0, open: 0 };
    steps.forEach(function (step) {
      var a = answerOf(state, step.id);
      if (a) s[a] += 1; else s.open += 1;
    });
    s.answered = s.total - s.open;
    s.allPass = s.total > 0 && s.pass === s.total;
    return s;
  }

  function summaryText(s) {
    if (!s.answered) return "No step answered yet.";
    var parts = [s.pass + " pass"];
    if (s.fail) parts.push(s.fail + " fail");
    if (s.unsure) parts.push(s.unsure + " not sure");
    if (s.open) parts.push(s.open + " not answered");
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
    var lines = ["## Pre-order review of ZoneFlow rev A", ""];
    lines.push("Date: " + date + ". Board file `zoneflow-a.kicad_pcb`, SHA-256 `" + String(meta.board).slice(0, 16) + "`.");
    lines.push("Made with the review walkthrough: " + s.total + " steps.");
    lines.push("");
    lines.push("**Result: " + s.pass + " pass, " + s.fail + " fail, " + s.unsure + " not sure, " + s.open + " not answered.**");
    lines.push("");
    if (s.allPass) {
      lines.push("Every step is a pass. What is still needed before an order: the owner's decision.");
    } else {
      lines.push("Not every step is a pass. The board is not ready to order.");
    }
    lines.push("");

    function section(title, want) {
      var mine = steps.filter(function (step) { return answerOf(state, step.id) === want; });
      if (!mine.length) return;
      lines.push("### " + title + " (" + mine.length + ")", "");
      var group = "";
      mine.forEach(function (step) {
        if (step.group !== group) { group = step.group; lines.push("**" + group + "**", ""); }
        var note = oneLine((state.answers[step.id] || {}).note || "");
        lines.push("- " + (want === "pass" ? "[x] " : "[ ] ") + step.title + (note ? "\n  > " + note : ""));
      });
      lines.push("");
    }

    section("Failed", "fail");
    section("Not sure", "unsure");
    section("Not answered", "");
    section("Passed", "pass");
    lines.push("Nothing was built or measured. Each answer is one person's look at design files, a print, a datasheet or");
    lines.push("an order page. It does not show that the board works.");
    return lines.join("\n") + "\n";
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
    var boardChanged = state.board && state.board !== meta.board && Object.keys(state.answers).length > 0;
    function save() {
      if (!canStore) return;
      try { win.localStorage.setItem(STORAGE_KEY, JSON.stringify(state)); } catch (e) { canStore = false; storageNote(); }
    }
    function storageNote() {
      var note = $(".zf-storage-note");
      var text = "";
      if (!canStore) {
        text = "This browser does not let the page store your answers. They are kept until you close or reload this tab. " +
               "Write the report before you leave.";
      } else if (boardChanged) {
        text = "The board file has changed since you gave these answers. They are kept. Check them against the new " +
               "pictures, or delete them on the result tab and start again.";
      }
      note.textContent = text;
      note.hidden = !text;
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
        var ans = answerOf(state, a.getAttribute("data-step"));
        a.setAttribute("data-answer", ans);
        $(".zf-nav-state", a).textContent = ans ? " (" + WORD[ans] + ")" : "";
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
      });
    }

    articles.forEach(function (article) {
      var id = article.getAttribute("data-step");
      var note = $(".zf-note", article);
      note.value = (state.answers[id] && state.answers[id].note) || "";
      article.addEventListener("change", function (ev) {
        if (ev.target.type === "radio") { setAnswer(state, id, ev.target.value); state.board = meta.board; save(); paint(); }
      });
      note.addEventListener("input", function () { setNote(state, id, note.value); state.board = meta.board; save(); });
      $(".zf-clear-one", article).addEventListener("click", function () { setAnswer(state, id, ""); save(); paint(); });
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
      summary.textContent = summaryText(s) + (s.allPass ? " Every step is a pass." : "");
      summary.setAttribute("data-state", s.allPass ? "pass" : s.fail ? "fail" : "open");
      var box = $(".zf-result-lists");
      box.textContent = "";
      [["fail", "Failed"], ["unsure", "Not sure"], ["", "Not answered"]].forEach(function (pair) {
        var mine = steps.filter(function (step) { return answerOf(state, step.id) === pair[0]; });
        if (!mine.length) return;
        box.appendChild(el("h3", pair[1] + " (" + mine.length + ")"));
        var ul = el("ul", "", "zf-result-list zf-result-" + (pair[0] || "open"));
        mine.forEach(function (step) {
          var li = el("li");
          var a = el("a", step.title);
          a.href = "#step-" + step.id;
          li.appendChild(a);
          li.appendChild(el("span", " " + step.group, "zf-result-group"));
          var note = oneLine((state.answers[step.id] || {}).note || "");
          if (note) li.appendChild(el("blockquote", note));
          ul.appendChild(li);
        });
        box.appendChild(ul);
      });
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
    $(".zf-copy-report").addEventListener("click", function () {
      function fallback() {
        report.focus();
        report.select();
        var ok = false;
        try { ok = doc.execCommand("copy"); } catch (e) { ok = false; }
        status.textContent = ok ? "Copied." : "Could not copy. The report is selected: copy it with Ctrl+C.";
      }
      if (win.navigator.clipboard && win.navigator.clipboard.writeText) {
        win.navigator.clipboard.writeText(report.value).then(function () { status.textContent = "Copied."; }, fallback);
      } else {
        fallback();
      }
    });
    $(".zf-download-report").addEventListener("click", function () {
      try {
        var url = win.URL.createObjectURL(new win.Blob([report.value], { type: "text/markdown" }));
        var a = doc.createElement("a");
        a.href = url;
        a.download = "zoneflow-rev-a-review-" + today() + ".md";
        doc.body.appendChild(a);
        a.click();
        a.remove();
        win.setTimeout(function () { win.URL.revokeObjectURL(url); }, 1000);
        status.textContent = "Saved as " + a.download + ".";
      } catch (e) {
        status.textContent = "Could not save a file. Copy the report instead.";
      }
    });
    $(".zf-reset").addEventListener("click", function () {
      if (!win.confirm("Delete all your answers and notes on this page?")) return;
      state = emptyState(meta.board);
      boardChanged = false;
      if (canStore) { try { win.localStorage.removeItem(STORAGE_KEY); } catch (e) { canStore = false; } }
      $$(".zf-note").forEach(function (n) { n.value = ""; });
      report.value = "";
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
    $$(".zf-zoom").forEach(function (button) {
      button.addEventListener("click", function () {
        opener = button;
        content.textContent = "";
        var copy = button.firstElementChild.cloneNode(true);
        $$("img", button).forEach(function (img) { img.loading = "eager"; });
        if (copy.tagName === "IMG") copy.loading = "eager"; else $$("img", copy).forEach(function (img) { img.loading = "eager"; });
        content.appendChild(copy);
        var cap = button.parentNode.querySelector("figcaption");
        $(".zf-lb-caption", lightbox).textContent = cap ? cap.textContent : "";
        if (lightbox.showModal) lightbox.showModal(); else lightbox.setAttribute("open", "");
        fit();
        frame.focus();
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
    frame.addEventListener("pointerdown", function (ev) {
      pointers[ev.pointerId] = { x: ev.clientX, y: ev.clientY };
      if (frame.setPointerCapture) frame.setPointerCapture(ev.pointerId);
      frame.classList.add("zf-dragging");
    });
    frame.addEventListener("pointermove", function (ev) {
      var p = pointers[ev.pointerId];
      if (!p) return;
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
    frame.addEventListener("pointerup", release);
    frame.addEventListener("pointercancel", release);
    frame.addEventListener("dblclick", function (ev) {
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
    answerOf: answerOf, summarize: summarize, summaryText: summaryText, nextOpen: nextOpen, buildReport: buildReport,
    parseHash: parseHash, zoomAt: zoomAt, clampView: clampView, start: start,
  };
});
