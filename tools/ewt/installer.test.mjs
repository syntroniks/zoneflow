// Checks the installer page's logic against a real release.json.
//   node --test tools/ewt/installer.test.mjs                       (uses the fixture below)
//   ZF_RELEASE_JSON=dist/release.json node --test tools/ewt/installer.test.mjs   (CI: the dry-run output)
import test from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { readFileSync } from "node:fs";

const lib = createRequire(import.meta.url)("../../docs/assets/installer-lib.js");
const fixture = {
  schema: 1,
  version: "1.2.3",
  tools: { esptool: "5.6.0" },
  flavours: [
    {
      id: "standalone",
      chip: "esp32s3",
      improv: false,
      next_step: "ble-or-softap",
      flash: { size: "16MB", mode: "dio", freq: "80m" },
      partitions: [],
      images: {
        factory: { file: "f.bin", offset: 0 },
        bootloader: { file: "b.bin", offset: 0 },
        "partition-table": { file: "p.bin", offset: 0x8000 },
        otadata: { file: "o.bin", offset: 0x1e000 },
        app: { file: "a.bin", offset: 0x20000 },
        www: { file: "w.bin", offset: 0x820000 },
      },
    },
  ],
};
const release = process.env.ZF_RELEASE_JSON ? JSON.parse(readFileSync(process.env.ZF_RELEASE_JSON, "utf8")) : fixture;
const base = "https://example.test/firmware/releases/latest/";

test("standalone does not promise Improv", () => {
  const s = lib.nextStep(lib.find(release, "standalone"));
  assert.equal(s.improv, false);
  assert.match(s.text, /does not support Improv/);
  assert.match(s.text, /Bluetooth LE/);
});

test("an unknown next step is not guessed", () => {
  const s = lib.nextStep({ next_step: "carrier-pigeon", improv: false });
  assert.equal(s.link, "");
  assert.doesNotMatch(s.text, /Bluetooth/);
});

test("commands come from release.json", () => {
  const f = lib.find(release, "standalone");
  const c = lib.commands(release, "standalone", base);
  assert.ok(c.factory.includes(`write-flash 0x0 ${f.images.factory.file}`));
  assert.ok(c.factory.startsWith(`uvx esptool@${release.tools.esptool} --chip esp32s3`));
  assert.ok(c.download.includes(base + f.images.factory.file));
  assert.ok(c.verify.includes(f.images.factory.file));
  // every per-image offset printed is the offset in release.json, in ascending order
  const offsets = c.partsTable.map((r) => parseInt(r.offset, 16));
  assert.deepEqual(offsets, [...offsets].sort((a, b) => a - b));
  for (const row of c.partsTable) {
    assert.equal(parseInt(row.offset, 16), f.images[row.name].offset);
    assert.ok(c.parts.includes(`${row.offset} ${row.file}`));
  }
  assert.ok(c.parts.includes(`--flash-size ${f.flash.size}`));
});

test("unknown flavour throws", () => {
  assert.throws(() => lib.commands(release, "nope", base));
});
