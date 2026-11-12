// SPDX-License-Identifier: Apache-2.0
// Write a hand-off file as the page would: node make_handoff.cjs <meta.json> <answers.json> <date>
// answers.json: { "<step id>": { "answer": "pass|fail|unsure", "note": "", "remedy": "", "remedyText": "" } }
const fs = require("node:fs");
const path = require("node:path");

const R = require(path.resolve(__dirname, "../../../../docs/hardware/review/review.js"));
const meta = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const answers = JSON.parse(fs.readFileSync(process.argv[3], "utf8"));
const state = R.emptyState(meta.board);
for (const [id, a] of Object.entries(answers)) {
  R.setAnswer(state, id, a.answer || "");
  R.setNote(state, id, a.note || "");
  if (a.remedy) R.setRemedy(state, id, a.remedy, a.remedyText || "");
}
process.stdout.write(R.buildHandoffFile(meta, state, process.argv[4]));
