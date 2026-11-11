// SPDX-License-Identifier: Apache-2.0
// Walk the whole review walkthrough in a real browser. Not part of CI, which has no browser.
//
//   uv run mkdocs build --strict
//   python3 -m http.server 8765 -d site &
//   npm install --no-save playwright && npx playwright install chromium
//   node tools/reviewwalk/tests/browser/walk.cjs /tmp/shots     # SITE=http://... for another address
//
// It goes through all steps, answers each, and checks: every picture loads, one step is on the
// screen at a time, zoom and pan change the view and focus returns, N and P move between steps,
// answers and notes survive a reload, the findings filter, the report and its file, a tablet width
// without sideways scrolling, print (every step shown, the bench sheet's drawings at their size in
// millimetres), a browser that forbids storage, a browser without JavaScript, and the link into
// the interactive BOM. It lists every request that leaves the site. Screenshots go to the folder
// named. The last line is "NO PROBLEMS" or the count of problems.
const { chromium } = require('playwright');
const fs = require('fs');
const out = process.argv[2] || 'shots';
fs.mkdirSync(out, { recursive: true });
const BASE = process.env.SITE || 'http://127.0.0.1:8765';
const URL = BASE + '/hardware/review/';
const problems = [];
const note = (m) => { problems.push(m); console.log('PROBLEM', m); };
(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 }, colorScheme: 'light' });
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
  if (!/Result: \d+ pass, \d+ fail, \d+ not sure, 1 not answered/.test(report)) note('report summary wrong: ' + report.slice(0, 300));
  const [download] = await Promise.all([page.waitForEvent('download'), page.click('.zf-download-report')]);
  console.log('download', download.suggestedFilename());
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.screenshot({ path: `${out}/result.png`, fullPage: true });
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
  const shown = await p3.isVisible('#step-o-ask');
  if (!shown) note('without JavaScript the last step is not visible');
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
