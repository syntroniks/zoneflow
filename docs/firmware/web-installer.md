# Web installer

!!! warning "No board has run this firmware"
    The installer flashes whatever the latest release contains. No ZoneFlow board has
    been built, so nobody has flashed one with this page, with `zfflash` or with
    `esptool`. What CI checked about the files is listed under
    [what is verified](releases.md#what-is-verified). The firmware has only run in
    unit tests on a PC.

Flash a ZoneFlow controller from your browser over USB-C, with [ESP Web
Tools](https://esphome.github.io/esp-web-tools/) and the WebSerial API. Nothing is
installed on your computer. The files come from the project's
[releases](https://github.com/syntroniks/zoneflow/releases), built and checked by CI.

<div id="zf-installer" markdown="0">

<p id="zf-js-needed"><strong>This page needs JavaScript</strong> to find the latest release.
Without it, download the files from the
<a href="https://github.com/syntroniks/zoneflow/releases">GitHub releases page</a> and use the
<a href="#flash-by-hand-with-esptool">esptool instructions</a>.</p>

<div class="admonition info" id="zf-no-release" hidden>
<p class="admonition-title">No release yet</p>
<p>There is no published firmware release, so there is nothing to install and no install button.
The first release appears here by itself once the project publishes it.
See <a href="../releases/">Releases and updating</a> for how releases are made.</p>
</div>

<div class="admonition warning" id="zf-no-serial" hidden>
<p class="admonition-title">This browser cannot flash over USB</p>
<p>WebSerial is missing. Use a Chromium-based desktop browser (Chrome, Edge), or
<a href="#flash-without-a-browser-zfflash">zfflash</a>, or
<a href="#flash-by-hand-with-esptool">esptool</a>.</p>
</div>

<div class="admonition warning" id="zf-insecure" hidden>
<p class="admonition-title">Not a secure page</p>
<p>WebSerial only works on HTTPS pages and on <code>localhost</code>. Open the installer from
<code>https://syntroniks.github.io/zoneflow/</code>.</p>
</div>

<div id="zf-release" hidden>
<p>Latest release: <strong id="zf-version"></strong>, built <span id="zf-built"></span>.
<a id="zf-notes" href="#">Release notes and checksums</a>.</p>

<div class="admonition warning" id="zf-prerelease" hidden>
<p class="admonition-title">Pre-release</p>
<p>This release is marked as a pre-release. Expect rough edges.</p>
</div>

<p id="zf-flavour-row" hidden><label for="zf-flavour"><strong>Firmware:</strong></label>
<select id="zf-flavour"></select></p>
<p id="zf-flavour-desc"></p>

<div id="zf-button-slot"></div>

<h3 id="zf-next-title">Next step</h3>
<p id="zf-next-text"></p>
<p><a id="zf-next-link" href="#" hidden></a></p>
</div>

<noscript><p>JavaScript is off. Use the <a href="https://github.com/syntroniks/zoneflow/releases">GitHub releases</a>.</p></noscript>
</div>

## Until the first release: build it yourself

No release has been published ([issue #8](https://github.com/syntroniks/zoneflow/issues/8)),
so this page, `zfflash` and the esptool commands below have nothing to install yet. Until
there is one, the only way to put firmware on a board is to build it from the repository.
Nobody has done this with a board: none exists. CI builds each firmware whenever its sources
change.

- **Standalone firmware.** Install ESP-IDF v5.5.6, build the web interface, then build and
  flash. The steps are in
  [`firmware/zoneflow-fw/README.md`](https://github.com/syntroniks/zoneflow/blob/main/firmware/zoneflow-fw/README.md):

    ```sh
    cd firmware/zoneflow-fw/web && npm ci && npm run build
    cd .. && idf.py set-target esp32s3 && idf.py build
    idf.py -p PORT flash
    ```

- **ESPHome package.** In `firmware/esphome`, with [uv](https://docs.astral.sh/uv/):

    ```sh
    uv sync
    uv run esphome run examples/zoneflow-8zone.yaml
    ```

    Use `examples/zoneflow-4zone.yaml` for a 4-zone board.

Read [before you flash](#before-you-flash) first: it applies to these commands too.

## Before you flash

- **Unpower the valves.** Unplug the 24 VAC transformer and the valve wires. USB alone
  powers the logic, and the design keeps every valve output off while the processor is
  being flashed (specification, safety rule 1). That behaviour is designed, not tested:
  there is no board.
- **Do not connect USB while 24 VAC is connected to a transformer that has one leg
  earthed** and your computer is earthed. The board's ground is one transformer leg.
  Read [hardware design, section 5](../design/hardware-design.md) first.
- **The install erases the board.** The factory image covers the whole 16 MB flash, so
  schedules, settings, WiFi credentials, the admin password and the setup code are
  replaced (the board makes a new setup code at its first start). Save a
  [backup](releases.md#back-up-and-restore) first if you want to keep your schedules.
  To update and keep settings, use [Releases and updating](releases.md#update-from-the-web-interface).

## Browser support

| Browser | Works? |
|---|---|
| Chrome, Edge and other Chromium-based browsers on a desktop (Windows, macOS, Linux) | They have WebSerial, which ESP Web Tools needs. Intended target. Not tried against a board. |
| Firefox, Safari | No WebSerial. Use `zfflash` or `esptool`. |
| Phones and tablets | Not supported. Not tried. |

Linux needs permission to open the serial port (usually membership of the `dialout` or
`uucp` group). The ESP32-S3's USB-C port is a USB Serial/JTAG device (vendor ID
`303a`); the browser asks you to pick it.

## If the browser cannot connect

1. Use a USB cable that carries data, and plug it into the board's USB-C port.
2. Close other programs that hold the port (a serial monitor, `idf.py monitor`).
3. Put the chip into download mode by hand: hold **BOOT**, press and release **RESET**,
   release **BOOT**, then try again. On the rev A board both buttons are on the PCB, not
   on the front panel. See [recovering a board](releases.md#recover-a-board-that-does-not-start).

## Flash without a browser: zfflash

`tools/zfflash` is a small Python program for people without a Chromium browser. It checks
every file against the release's SHA-256 sums before it writes anything. It needs
[uv](https://docs.astral.sh/uv/), nothing else.

```sh
git clone https://github.com/syntroniks/zoneflow && cd zoneflow/tools/zfflash
uv run zfflash ports           # which port is the board?
uv run zfflash flash factory   # whole flash of a blank board
```

More in [Releases and updating](releases.md#update-over-usb-with-zfflash). It can also
erase the settings and back up or restore the configuration.

## Flash by hand with esptool

<div id="zf-parts-wrap" markdown="0">
<p>The commands appear here when a release exists. They are generated from the release's
<code>release.json</code>, the data the install manifest comes from, so offsets and file
names cannot differ from what the browser installer uses. Replace <code>PORT</code> with
the serial port (<code>/dev/ttyACM0</code>, <code>COM5</code>, <code>/dev/cu.usbmodem1101</code>).
<code>uvx</code> runs esptool without installing it.</p>

<p>Download and check the factory image:</p>
<pre><code id="zf-cmd-download">(no release yet)</code></pre>
<pre><code id="zf-cmd-verify"></code></pre>

<p>Flash it. One file at offset 0 covers the whole flash:</p>
<pre><code id="zf-cmd-factory"></code></pre>

<div id="zf-parts" hidden>
<p>Or write the five images one by one (download each file from the same place; the offsets
are those of the partition table in <a href="../../design/firmware-architecture/">firmware architecture</a>
and <code>partitions.csv</code>):</p>
<table>
<thead><tr><th>Offset</th><th>Image</th><th>File</th></tr></thead>
<tbody id="zf-parts-body"></tbody>
</table>
<pre><code id="zf-cmd-parts"></code></pre>
</div>

<p>To wipe the flash first:</p>
<pre><code id="zf-cmd-erase"></code></pre>
</div>

## What this page does and does not do

- It loads ESP Web Tools 10.4.0 from this site, not from a CDN. The files are copied
  into the repository (`docs/assets/vendor/esp-web-tools/`), pinned by version and
  SHA-256, and loaded with a subresource-integrity hash. CI fails if they change without
  `tools/ewt/vendor.py update`.
- It sends nothing anywhere. The flashing happens in your browser and the files come from
  this same site.
- For the standalone firmware it does not offer WiFi setup over USB (Improv Serial). That
  firmware does not implement Improv; it provisions over Bluetooth LE and a setup access
  point instead, as described above. The [ESPHome firmware](esphome.md), the second
  choice in the firmware list, does implement Improv: the installer offers WiFi setup
  over the cable after the flash.
- The files on this site are copies of the latest release, made when the site is built.
  The same files, with the same names, are on the GitHub release.

<script type="module" src="../../assets/vendor/esp-web-tools/10.4.0/install-button.js" integrity="sha384-9XfyvAabgkISlB/Xb3OpshGKpZx0TOt5Mmu78SeiNU3so/p4o2s9m66vy6Utdnom"></script>
<script src="../../assets/installer-lib.js"></script>
<script src="../../assets/installer.js"></script>
