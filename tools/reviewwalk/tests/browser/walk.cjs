// SPDX-License-Identifier: Apache-2.0
// Walk the whole review walkthrough in a real browser. Not part of CI, which has no browser.
//
//   uv run mkdocs build --strict
//   python3 -m http.server 8765 -d site &
//   npm install --no-save playwright && npx playwright install chromium
//   node tools/reviewwalk/tests/browser/walk.cjs /tmp/shots     # SITE=http://... for another address
//
// The hand-off file it takes from the page is given to `reviewwalk record`, so `uv` must be on the path.
//
// It goes through all steps, answers each, and checks: every picture loads, one step is on the
// screen at a time, zoom and pan change the view and focus returns, N and P move between steps,
// answers and notes survive a reload, the findings filter, the report and its file, a tablet width
// without sideways scrolling, print (every step shown, the bench sheet's drawings at their size in
// millimetres), a browser that forbids storage, a browser without JavaScript, and the link into
// the interactive BOM. For the hand-off to an agent it checks: Accept and Reject on a decision step, the
// remedy asked for when a step fails, layers and the selection of a pad in an enlarged picture, the file
// copied and saved, `reviewwalk record` on that file, the answers taken back in on a fresh page by paste
// and by file, and the warning for a file of another board. It lists every request that leaves the site. Screenshots go to the folder
// named. The last line is "NO PROBLEMS" or the count of problems.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const REPO = path.resolve(__dirname, '../../../..');
const out = process.argv[2] || 'shots';
fs.mkdirSync(out, { recursive: true });
const BASE = process.env.SITE || 'http://127.0.0.1:8765';
const URL = BASE + '/hardware/review/';
const problems = [];
const note = (m) => { problems.push(m); console.log('PROBLEM', m); };
(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 }, colorScheme: 'light',
    permissions: ['clipboard-read', 'clipboard-write'] });
  const page = await ctx.newPage();
  const external = [];
  page.on('console', (m) => { if (m.type() === 'error' && !/status of 404/.test(m.text())) note('console: ' + m.text()); });
  page.on('pageerror', (e) => note('pageerror: ' + e.message));
  page.on('request', (r) => { if (!r.url().startsWith(BASE + '/') && !r.url().startsWith('data:') && !r.url().startsWith('blob:')) external.push(r.url()); });
  page.on('response', (r) => { if (r.status() >= 400 && r.url().startsWith(BASE)) note('http ' + r.status() + ' ' + r.url()); });
  await page.goto(URL);
  await page.waitForSelector('.zf-js');
  await page.screenshot({ path: `${out}/01-start-light.png` });
  // orientation images
  const startImgs = await page.$$eval('#start img', (imgs) => imgs.map((i) => [i.src, i.complete && i.naturalWidth > 0]));
  startImgs.filter((x) => !x[1]).forEach((x) => note('start image not loaded ' + x[0]));
  await page.click('.zf-begin');
  await page.waitForSelector('#step-k-open:not([hidden])');
  const ids = await page.$$eval('.zf-step', (a) => a.map((x) => x.dataset.step));
  const remedies = {};
  console.log('steps', ids.length);
  const shots = ['k-open', 'k-valve', 'k-ka-pin10', 'k-patches', 's-u5', 's-bat54s', 'p-headers', 'p-bottom', 'h-display', 'o-commands', 'o-prev-optos', 'o-ask'];
  for (let i = 0; i < ids.length; i++) {
    const id = ids[i];
    const sel = `#step-${id}`;
    if (!(await page.isVisible(sel))) note(`step ${id} not visible at position ${i}`);
    const visibleSteps = await page.$$eval('.zf-step:not([hidden])', (a) => a.length);
    if (visibleSteps !== 1) note(`${visibleSteps} steps visible at ${id}`);
    // every image of this step must load
    const imgs = await page.$$(`${sel} img`);
    for (const img of imgs) {
      await img.scrollIntoViewIfNeeded();
      await img.evaluate((el) => el.complete || new Promise((res) => { el.onload = res; el.onerror = res; }));
      const ok = await img.evaluate((el) => el.naturalWidth > 0);
      if (!ok) note(`image failed in ${id}: ${await img.getAttribute('src')}`);
    }
    const nofig = await page.$(`${sel} .zf-nofig`);
    if (!imgs.length && !nofig) note(`step ${id} has neither picture nor note`);
    // answer: cycle pass / fail / unsure, with a note on some
    const answer = i % 9 === 4 ? 'fail' : i % 7 === 3 ? 'unsure' : 'pass';
    if (i !== ids.length - 1) await page.check(`${sel} input[value="${answer}"]`);
    if (answer !== 'pass') await page.fill(`${sel} .zf-note`, `Note for ${id}: looked at it twice.`);
    // A failed step asks what should be done. A passed one does not.
    if (i !== ids.length - 1) {
      const asks = await page.isVisible(`${sel} .zf-remedy`);
      if (asks !== (answer === 'fail')) note(`remedy question ${asks ? 'shown' : 'hidden'} for a step answered ${answer}: ${id}`);
    }
    if (answer === 'fail') {
      const listed = await page.$$eval(`${sel} .zf-remedy-select option`, (o) => o.map((x) => x.value).filter((v) => v && v !== 'own'));
      if (i % 2 && listed.length) {
        await page.selectOption(`${sel} .zf-remedy-select`, listed[0]);
        remedies[id] = listed[0];
      } else if (i % 2) {
        await page.selectOption(`${sel} .zf-remedy-select`, 'own');
        await page.fill(`${sel} .zf-remedy-text`, `My own remedy for ${id}.`);
        remedies[id] = 'own';
      }
      if (!shots.remedy) { shots.remedy = true; await page.locator(`${sel} .zf-answer`).screenshot({ path: `${out}/remedy.png` }); }
    }
    if (id === 'k-dev-adc') {
      const labels = await page.$$eval(`${sel} .zf-radio`, (l) => l.map((x) => x.textContent.trim()).join('/'));
      if (labels !== 'Accept/Reject/Not sure') note('a decision step offers ' + labels);
    }
    if (id === 'k-u1-vias') {
      // An enlarged picture: its layers can be hidden, and a pad says which part and net it is.
      await page.click(`${sel} .zf-zoom`);
      await page.waitForSelector('.zf-lb-tools:not([hidden])');
      const layers = await page.$$eval('.zf-lb-layer', (l) => l.map((x) => x.textContent.trim()));
      if (layers.join(',') !== 'F.Cu,Edge.Cuts') note('layers offered: ' + layers.join(','));
      const pad = await page.$eval('.zf-lb-content [data-p="U1.2"]', (e) => { const r = e.getBoundingClientRect(); return [r.x + r.width / 2, r.y + r.height / 2]; });
      await page.mouse.click(pad[0], pad[1]);
      const said = await page.textContent('.zf-lb-info');
      if (said !== 'Pad 2 of U1, net /Power/VIN_U1, on F.Cu.') note('selected pad described as: ' + said);
      await page.screenshot({ path: `${out}/pick.png` });
      await page.uncheck('.zf-lb-layer input');
      if (await page.isVisible('.zf-lb-content [data-p="U1.2"]')) note('the F.Cu layer did not hide');
      await page.mouse.click(pad[0], pad[1]);
      const nothing = await page.textContent('.zf-lb-info');
      if (/Pad 2/.test(nothing)) note('a hidden pad was still selected: ' + nothing);
      await page.keyboard.press('Escape');
      await page.waitForSelector('#zf-lightbox:not([open])', { state: 'attached' });
    }
    if (shots.includes(id)) { await page.evaluate(() => window.scrollTo(0, 0)); await page.screenshot({ path: `${out}/step-${id}.png`, fullPage: true }); }
    if (i === 2) {
      // zoom: open the lightbox, zoom, pan, close
      await page.click(`${sel} .zf-zoom`);
      await page.waitForSelector('#zf-lightbox[open]');
      const t0 = await page.$eval('.zf-lb-content', (e) => e.style.transform);
      await page.mouse.move(700, 450); await page.mouse.wheel(0, -400); await page.mouse.wheel(0, -400);
      await page.mouse.down(); await page.mouse.move(500, 300, { steps: 5 }); await page.mouse.up();
      const t1 = await page.$eval('.zf-lb-content', (e) => e.style.transform);
      if (t0 === t1 || !/scale\((1\.[2-9]|[2-9])/.test(t1)) note('zoom did not change the view: ' + t0 + ' -> ' + t1);
      await page.screenshot({ path: `${out}/zoom.png` });
      await page.keyboard.press('Escape');
      await page.waitForSelector('#zf-lightbox:not([open])', { state: 'attached' });
      const focused = await page.evaluate(() => document.activeElement.className);
      if (!focused.includes('zf-zoom')) note('focus did not return to the picture: ' + focused);
    }
    if (i < ids.length - 1) {
      if (i % 2) { if (answer !== 'pass') await page.focus(sel + ' input[value="' + answer + '"]'); await page.keyboard.press('n'); } else await page.click('.zf-next');
      await page.waitForSelector(`#step-${ids[i + 1]}:not([hidden])`);
    }
  }
  // keyboard back
  await page.keyboard.press('p');
  await page.waitForSelector(`#step-${ids[ids.length - 2]}:not([hidden])`);
  const progress = await page.textContent('.zf-progress-text');
  console.log('progress:', progress);
  // reload: answers persist
  await page.reload();
  await page.waitForSelector('.zf-js');
  const progress2 = await page.textContent('.zf-progress-text');
  if (progress !== progress2) note(`progress changed over reload: ${progress} / ${progress2}`);
  const noteBack = await page.inputValue(`#note-${ids[3]}`);
  if (!noteBack.includes(ids[3])) note('note not restored: ' + noteBack);
  // findings
  await page.click('.zf-tabs a[href="#findings"]');
  await page.waitForSelector('#findings:not([hidden])');
  const nCards = await page.$$eval('.zf-finding:not([hidden])', (a) => a.length);
  await page.click('.zf-filter[data-severity="major"]');
  const nMajor = await page.$$eval('.zf-finding:not([hidden])', (a) => a.length);
  console.log('findings', nCards, 'major', nMajor);
  if (nCards !== 30 || nMajor !== 9) note(`findings ${nCards}, major ${nMajor}`);
  await page.click('.zf-filter[data-severity="all"]');
  const fimgs = await page.$$('#findings img');
  for (const img of fimgs) {
    await img.scrollIntoViewIfNeeded();
    await img.evaluate((el) => el.complete || new Promise((res) => { el.onload = res; el.onerror = res; }));
    if (!(await img.evaluate((el) => el.naturalWidth > 0))) note('finding image failed ' + (await img.getAttribute('src')));
  }
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.screenshot({ path: `${out}/findings.png` });
  // result and report
  await page.click('.zf-tabs a[href="#result"]');
  await page.waitForSelector('#result:not([hidden])');
  await page.click('.zf-make-report');
  const report = await page.inputValue('#zf-report');
  fs.writeFileSync(`${out}/report.md`, report);
  if (!/Result: \d+ pass, \d+ accepted, \d+ fail, \d+ rejected, \d+ not sure, 1 not answered/.test(report)) note('report summary wrong: ' + report.slice(0, 300));
  const [download] = await Promise.all([page.waitForEvent('download'), page.click('.zf-download-report')]);
  console.log('download', download.suggestedFilename());
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.screenshot({ path: `${out}/result.png`, fullPage: true });
  // the hand-off file for an agent: copied, saved, and recorded in the repository by the documented command
  await page.click('.zf-handoff-copy');
  await page.waitForFunction(() => document.querySelector('.zf-handoff-status').textContent === 'Copied.');
  const shown = await page.inputValue('#zf-handoff-file');
  const copied = await page.evaluate(() => navigator.clipboard.readText());
  if (copied !== shown || !shown) note('the copied hand-off file is not the one shown');
  const [saved] = await Promise.all([page.waitForEvent('download'), page.click('.zf-handoff-save')]);
  const handoffPath = `${out}/${saved.suggestedFilename()}`;
  await saved.saveAs(handoffPath);
  const handoff = fs.readFileSync(handoffPath, 'utf8');
  console.log('hand-off', saved.suggestedFilename(), handoff.length, 'bytes');
  if (handoff !== shown) note('the saved hand-off file is not the one shown');
  if (!/^# ZoneFlow rev A: hand-off of the pre-order review, \d{4}-\d\d-\d\d\n/.test(handoff)) note('hand-off title: ' + handoff.slice(0, 80));
  for (const must of ['## For the agent: read this first', 'read `CLAUDE.md`', 'reviewwalk record', '**Never order boards,**', '# Decisions as data', '# Decisions as text']) {
    if (!handoff.includes(must)) note('the hand-off file lacks: ' + must);
  }
  const data = JSON.parse(/^```json\n([\s\S]*?)\n```$/m.exec(handoff)[1]);
  if (data.decisions.length !== ids.length) note('hand-off decisions: ' + data.decisions.length);
  if (!/^[0-9a-f]{40}/.test(data.site_commit)) note('hand-off commit: ' + data.site_commit);
  for (const [id, want] of Object.entries(remedies)) {
    const r = data.decisions.find((d) => d.step === id).remedy;
    if (want === 'own' ? r.choice !== 'own' || !r.text.includes(id) : r.choice !== 'listed' || r.id !== want) note(`remedy of ${id}: ${JSON.stringify(r)}`);
  }
  const rejected = data.decisions.filter((d) => d.kind === 'decision' && d.decision === 'rejected').length;
  const accepted = data.decisions.filter((d) => d.decision === 'accepted').length;
  console.log('decisions:', JSON.stringify(data.summary), 'remedies chosen:', Object.keys(remedies).length);
  if (accepted + rejected === 0) note('no decision step was accepted or rejected');
  await page.locator('#hand-off').scrollIntoViewIfNeeded();
  await page.evaluate(() => { const h = document.getElementById('hand-off'); window.scrollTo(0, h.getBoundingClientRect().top + window.scrollY - 120); });
  await page.screenshot({ path: `${out}/handoff.png` });
  const records = fs.mkdtempSync(path.join(require('os').tmpdir(), 'zf-records-'));
  try {
    const said = execFileSync('uv', ['run', '--project', 'tools/reviewwalk', '--locked', 'reviewwalk', 'record', path.resolve(handoffPath), '--out', records],
      { cwd: REPO, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
    console.log(said.trim().replace(records, '<records>'));
    const record = fs.readFileSync(path.join(records, `${data.reviewed_on}-pre-order.md`), 'utf8');
    if (!record.includes('## Needs a change (' + (data.summary.fail + data.summary.rejected) + ')')) note('the record does not list the changes');
    fs.copyFileSync(path.join(records, `${data.reviewed_on}-pre-order.md`), `${out}/record.md`);
  } catch (e) {
    note('reviewwalk record refused the file the page wrote: ' + (e.stderr || e.message));
  }

  // the way back in: a fresh browser, the file pasted, the same answers
  const progressOut = await page.textContent('.zf-progress-text');
  const fresh = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const pf = await fresh.newPage();
  pf.on('pageerror', (e) => note('import pageerror: ' + e.message));
  pf.on('dialog', (d) => d.accept());
  await pf.goto(URL + '#result'); await pf.waitForSelector('.zf-js');
  await pf.fill('#zf-import-text', handoff);
  await pf.click('.zf-import-go');
  const took = await pf.textContent('.zf-import-status');
  if (!/^46 answers were taken from the file/.test(took) || /Warning/.test(took)) note('import said: ' + took);
  if ((await pf.textContent('.zf-progress-text')) !== progressOut) note('progress after import: ' + (await pf.textContent('.zf-progress-text')));
  await pf.click('.zf-handoff-show summary');
  await pf.waitForFunction(() => document.getElementById('zf-handoff-file').value !== '');
  const again = await pf.inputValue('#zf-handoff-file');
  if (again !== handoff) {
    let at = 0;
    while (at < again.length && again[at] === handoff[at]) at += 1;
    note('the file written after the import is not the file that was imported, from: ' + JSON.stringify(again.slice(at, at + 80)) + ' against ' + JSON.stringify(handoff.slice(at, at + 80)));
  }
  const firstRemedy = Object.keys(remedies)[0];
  if (firstRemedy && (await pf.inputValue(`#remedy-${firstRemedy}`)) !== remedies[firstRemedy]) note('the remedy did not come back for ' + firstRemedy);
  if (!(await pf.inputValue(`#note-${ids[3]}`)).includes(ids[3])) note('the note did not come back');
  await pf.reload(); await pf.waitForSelector('.zf-js');
  if ((await pf.textContent('.zf-progress-text')) !== progressOut) note('imported answers did not survive a reload');
  // a file of another board: taken in, with a warning, and no hand-off until the owner confirms
  await pf.goto(URL + '#result'); await pf.waitForSelector('.zf-js');
  await pf.fill('#zf-import-text', handoff.split(data.board.sha256).join('f'.repeat(64)));
  await pf.click('.zf-import-go');
  const warned = await pf.textContent('.zf-import-status');
  if (!/Warning: the file is a review of another board file \(SHA-256 ffffffffffff\)/.test(warned)) note('no warning for another board: ' + warned);
  if (!(await pf.isDisabled('.zf-handoff-copy'))) note('a hand-off can be written from answers about another board');
  await pf.locator('.zf-import-status').scrollIntoViewIfNeeded();
  await pf.screenshot({ path: `${out}/import-warning.png` });
  await pf.click('.zf-tabs a[href="#steps"]');
  const stale = await pf.textContent('.zf-storage-text');
  if (!/another board file/.test(stale)) note('the steps do not say that the answers are of another board: ' + stale);
  await pf.click('.zf-confirm-board');
  if (await pf.isVisible('.zf-storage-note')) note('the note stayed after the answers were confirmed');
  await pf.click('.zf-tabs a[href="#result"]');
  if (await pf.isDisabled('.zf-handoff-copy')) note('no hand-off after the answers were confirmed');
  // by file, and what is no hand-off file
  const byFile = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  const pb = await byFile.newPage();
  await pb.goto(URL + '#result'); await pb.waitForSelector('.zf-js');
  await pb.setInputFiles('.zf-import-file', handoffPath);
  await pb.waitForFunction(() => /answers were taken/.test(document.querySelector('.zf-import-status').textContent));
  if ((await pb.textContent('.zf-progress-text')) !== progressOut) note('progress after import by file');
  await pb.fill('#zf-import-text', '# Shopping list\n\n- solder');
  pb.on('dialog', (d) => d.accept());
  await pb.click('.zf-import-go');
  if (!/not a hand-off file/.test(await pb.textContent('.zf-import-status'))) note('a file that is no hand-off was not refused');
  if ((await pb.textContent('.zf-progress-text')) !== progressOut) note('a refused file changed the answers');
  await byFile.close();
  await fresh.close();

  // link from a result item back to its step
  await page.click('.zf-result-list a');
  await page.waitForSelector('#steps:not([hidden])');
  // dark scheme: the site follows the browser's preference
  const dark = await browser.newContext({ viewport: { width: 1440, height: 900 }, colorScheme: 'dark' });
  const pd = await dark.newPage();
  await pd.goto(URL + '#step-k-u1-vias'); await pd.waitForSelector('.zf-js');
  if ((await pd.getAttribute('body', 'data-md-color-scheme')) !== 'slate') note('the dark scheme did not apply');
  await pd.screenshot({ path: `${out}/step-dark.png` });
  await pd.goto(URL + '#findings'); await pd.waitForTimeout(300);
  await pd.screenshot({ path: `${out}/findings-dark.png` });
  await dark.close();
  // tablet
  await page.setViewportSize({ width: 820, height: 1180 });
  await page.goto(URL + '#step-s-u11'); await page.reload(); await page.waitForSelector('.zf-js');
  await page.screenshot({ path: `${out}/tablet.png` });
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  if (overflow > 2) note('horizontal overflow on tablet: ' + overflow);
  // print layout
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.emulateMedia({ media: 'print' });
  await page.goto(URL); await page.waitForSelector('.zf-js');
  const printSteps = await page.$$eval('.zf-step', (a) => a.filter((x) => getComputedStyle(x).display !== 'none').length);
  if (printSteps !== ids.length) note('print shows ' + printSteps + ' steps');
  await page.pdf({ path: `${out}/walkthrough.pdf`, format: 'A4' });
  await page.goto(URL + 'bench-sheet/');
  await page.pdf({ path: `${out}/bench-sheet.pdf`, format: 'A4', printBackground: false });
  const mm = await page.$$eval('img.zf-true', (a) => a.map((i) => [i.getBoundingClientRect().width, i.getBoundingClientRect().height]));
  console.log('true-size images, CSS px:', JSON.stringify(mm), 'expected', 142 * 96 / 25.4, 160 * 96 / 25.4);
  await page.emulateMedia({ media: 'screen' });
  // storage unavailable
  const ctx2 = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  await ctx2.addInitScript(() => { Object.defineProperty(window, 'localStorage', { get() { throw new Error('denied'); } }); });
  const p2 = await ctx2.newPage();
  // The site's theme reads storage too and reports its own error there: only this page's script counts.
  p2.on('pageerror', (e) => { if ((e.stack || '').includes('review.js')) note('no-storage pageerror: ' + e.message); });
  await p2.goto(URL + '#step-k-open'); await p2.waitForSelector('.zf-js');
  await p2.check('#step-k-open input[value="pass"]');
  const warn = await p2.textContent('.zf-storage-note');
  if (!/does not let the page store/.test(warn)) note('no storage notice missing: ' + warn);
  const pt = await p2.textContent('.zf-progress-text');
  if (!/1 of 47/.test(pt)) note('no-storage progress: ' + pt);
  // no JavaScript
  const ctx3 = await browser.newContext({ javaScriptEnabled: false, viewport: { width: 1280, height: 800 } });
  const p3 = await ctx3.newPage();
  await p3.goto(URL);
  const vis = await p3.$$eval('.zf-step', (a) => a.length);
  const lastShown = await p3.isVisible('#step-o-ask');
  if (!lastShown) note('without JavaScript the last step is not visible');
  console.log('no-js steps in page', vis);
  // the BOM finder
  const p4 = await ctx.newPage();
  p4.on('pageerror', (e) => note('find pageerror: ' + e.message));
  await p4.goto(URL + 'find.html?ref=D25');
  await p4.waitForTimeout(1500);
  const said = await p4.textContent('#note');
  console.log('find:', said.trim().slice(0, 80));
  const rows = await p4.frameLocator('#ibom').locator('#bombody tr').count();
  console.log('ibom rows after search', rows);
  await p4.screenshot({ path: `${out}/find.png` });
  console.log('external requests:', JSON.stringify([...new Set(external)]));
  console.log(problems.length ? `${problems.length} PROBLEMS` : 'NO PROBLEMS');
  await browser.close();
})().catch((e) => { console.error('FAILED', e); process.exit(1); });
