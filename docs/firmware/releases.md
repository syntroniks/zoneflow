# Releases and updating

!!! warning "Nothing here has been tried on a board"
    No ZoneFlow board exists. Every step below describes what the firmware and the tools
    are written to do. [What is verified](#what-is-verified) lists the checks that ran and
    those that need a board.

A release is a Git tag `vMAJOR.MINOR.PATCH` (or `vMAJOR.MINOR.PATCH-prerelease`). CI builds the
standalone firmware from that tag, with the web interface packed in, and the
[ESPHome package](esphome.md)'s factory image, and attaches the files to a
[GitHub release](https://github.com/syntroniks/zoneflow/releases). The firmware reports the
tag as its version (`GET /api/v1/info`, the display's About screen and the MQTT discovery
message).

## What a release contains

| File | Use |
|---|---|
| `zoneflow-standalone-<v>-factory.bin` | The whole 16 MB flash for a blank board, written at offset 0: bootloader, partition table, OTA data, application and web interface, with `0xff` everywhere else. The [web installer](web-installer.md) and `zfflash flash factory` write this. |
| `zoneflow-standalone-<v>-app.bin` | The application alone. Upload it in the web interface or the REST API to update a running controller. File names do not matter to the controller; it checks the image header (ESP32-S3, project `zoneflow-fw`). |
| `zoneflow-standalone-<v>-www.bin` | The web interface alone (a LittleFS image of the `www` partition). |
| `...-bootloader.bin`, `...-partition-table.bin`, `...-otadata.bin` | The remaining pieces, for writing by hand at the offsets in `release.json`. |
| `manifest-standalone.json` | The [ESP Web Tools](https://esphome.github.io/esp-web-tools/) manifest the installer page uses. |
| `zoneflow-esphome-<v>-factory.bin` | The ESPHome firmware for a blank board, written at offset 0. ESPHome lays out the partitions; the release checks the image headers only. |
| `zoneflow-esphome-<v>-app.bin` | The ESPHome application alone, for ESPHome's own OTA. Once a controller is adopted, update it from the ESPHome dashboard instead. |
| `manifest-esphome.json` | The installer manifest for the ESPHome firmware. It keeps the Improv WiFi step. |
| `release.json` | All of the above with offsets, sizes and SHA-256, plus the partition table and flash settings. The installer page and `zfflash` read it. |
| `SHA256SUMS` | `sha256sum --check SHA256SUMS` checks every file. |

The factory image starts from a layout that matches `firmware/zoneflow-fw/partitions.csv`:

| Partition | Offset | Size |
|---|---|---|
| `nvs` (settings, WiFi credentials, setup code) | `0x9000` | `0x15000` |
| `otadata` | `0x1e000` | `0x2000` |
| `ota_0` | `0x20000` | `0x400000` |
| `ota_1` | `0x420000` | `0x400000` |
| `www` | `0x820000` | `0x7c0000` |
| `coredump` | `0xfe0000` | `0x20000` |

The bootloader is at `0x0` and the partition table at `0x8000`. The release tooling does not
hold these numbers: it reads them from the build and fails if they differ from `partitions.csv`.

## Where the files are

- **GitHub release:** every file above.
- **Project site:** the same files for the latest release, under
  `https://syntroniks.github.io/zoneflow/firmware/releases/latest/`. WebSerial downloads
  need a same-origin copy: GitHub release assets are served from another origin without the
  CORS headers a browser needs, and GitHub Pages cannot proxy them. So the documentation
  build downloads the newest release, checks it against its `SHA256SUMS`, and puts it into
  the site (`tools/zfrelease`, `stage-site`). The site is rebuilt after each release.
  Until a release exists, the site has no firmware and the installer page says so.

The site serves the newest published release, a pre-release too, because the project has no
stable release yet. The installer page shows when it is a pre-release.

## Check a download

```sh
sha256sum --check SHA256SUMS      # macOS: shasum -a 256 --check SHA256SUMS
```

`SHA256SUMS` comes from the same place as the files, so it catches damage, not a hostile
server. The GitHub release notes also print the SHA-256 of `SHA256SUMS` itself, which you can
compare with the copy on the site. **The images are not signed**, and the controller accepts
any image with a correct header and hash, an older one too (see
[firmware security](../design/firmware-security.md)). Take firmware from this project's
releases only.

## Update from the web interface

**Settings → Firmware update** in the controller's web interface.

1. Save a backup first (**Settings → Backup and restore**).
2. Choose `zoneflow-standalone-<v>-app.bin`, or paste the URL of the asset on the GitHub
   release (HTTPS only; the controller follows GitHub's redirect to its download server).
3. **Watering stops.** Before it writes, the controller enters maintenance mode: every valve
   closes, the queue is cleared and starts are blocked. If a zone is running, the update is
   refused (409 `zones_active`) unless you confirm the forced update. A scheduled start that
   falls during the update is not run.
4. The controller verifies the image, switches to the other slot and restarts after about a
   second. Every session ends at the restart: sign in again.
5. The interface shows the version afterwards, and `GET /api/v1/firmware` shows the running
   slot and the version in the other slot.

A new web interface (`...-www.bin`) is a separate upload; it needs no restart. When a release
changes the interface, update both. An old interface on new firmware (or the reverse) may
miss or mis-show a feature; the REST API is versioned (`/api/v1`).

### From a script

```sh
TOKEN=$(curl -s -X POST http://zoneflow-ab12.local/api/v1/auth/login \
        -H 'Content-Type: application/json' -d '{"password":"..."}' | jq -r .token)
curl -X POST http://zoneflow-ab12.local/api/v1/firmware/upload \
     -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/octet-stream' \
     --data-binary @zoneflow-standalone-0.1.0-app.bin
curl -X POST http://zoneflow-ab12.local/api/v1/firmware/pull \
     -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
     -d '{"url":"https://github.com/syntroniks/zoneflow/releases/download/v0.1.0/zoneflow-standalone-0.1.0-app.bin"}'
```

`uv run zfflash ota --host zoneflow-ab12.local` does the same after checking the file against
the release checksums. Routes: [API reference](api.md#firmware-updates).

## Rollback

Rollback is automatic. There is no button to go back and no API call to pick a slot.

- The bootloader is built with app rollback on. A new image starts in the state *pending
  verification*.
- The firmware confirms the image once its control loop has advanced five times and the
  web server has started. It rolls back at once if the stored configuration was rejected, and
  after 60 seconds if it has not confirmed. A crash or reset before it confirms makes the
  bootloader return to the previous slot.
- Network connectivity is **not** a condition: a router outage during an update must not
  undo it. So a new image that runs but cannot reach WiFi is kept.
- The older image stays in the other slot until the next update overwrites it. To go back on
  purpose, upload an older `app.bin`. Images are not signed and there is no anti-rollback,
  so that works.

Configuration is stored in `nvs`, which an update does not touch. A configuration saved by
newer firmware is refused by older firmware (`unsupported_schema`) when you restore it.

## Update over USB with zfflash

For a controller that no longer starts, or without a network. Needs [uv](https://docs.astral.sh/uv/).

```sh
cd tools/zfflash
uv run zfflash ports                     # find the board
uv run zfflash flash app www             # update, keep settings
uv run zfflash flash factory             # blank board, or start from scratch
uv run zfflash erase-nvs                 # factory reset from outside, keep the firmware
```

- `flash app` erases `otadata` and writes slot `ota_0`, so the new image boots whichever slot
  the board ran before. **A USB update has no rollback**: the board is not left in
  *pending verification*.
- `flash factory` replaces everything, the setup code too.
- `erase-nvs` erases the settings partition. The firmware makes a new setup code and starts
  WiFi setup.
- Everything is checked against `SHA256SUMS` before the first byte is written. `--release v0.1.0`
  picks a GitHub release, `--source DIR` a directory.

### Back up and restore

```sh
uv run zfflash backup  --host zoneflow-ab12.local --out zoneflow-backup.json
uv run zfflash restore --host zoneflow-ab12.local --file zoneflow-backup.json
```

The admin password is read from the `ZF_PASSWORD` environment variable or prompted for. A
backup holds schedules, zones and settings. **It never holds WiFi credentials, the admin
password, the MQTT password or the setup code** (see the
[API reference](api.md)). A restore to a controller with older firmware than the backup is
refused.

## Update over USB with esptool

The [installer page](web-installer.md#flash-by-hand-with-esptool) prints the exact commands for
the latest release. To update only the application, write `app.bin` at `0x20000` after erasing
`otadata` (`erase-region 0x1e000 0x2000`).

## Recover a board that does not start

Everything below follows the [hardware design](../design/hardware-design.md#124-supply-reset-and-boot)
and ESP-IDF documentation. It has not been tried: the rev A board is not built.

1. **Try the normal route first.** The USB-C port is the ESP32-S3's USB Serial/JTAG
   controller. esptool resets the chip into download mode by itself, so a board whose
   firmware crashes still flashes with `zfflash flash factory` or the web installer.
2. **Download mode by hand.** If the port does not appear, or flashing fails at "connecting":
   hold **BOOT** (switch SW2, `GPIO0` to ground), press and release **RESET** (SW1, `EN` to
   ground), then release **BOOT**. The chip waits for esptool. Both switches are on the PCB, not
   on the front panel.
3. **The test header.** J8 is a row of six pads, 2.54 mm: `3V3`, `GND`, `U0TXD`, `U0RXD`, `EN`,
   `BOOT`. With a 3.3 V USB-UART adapter, connect `GND`, `U0TXD` to its RX and `U0RXD` to its TX,
   short `BOOT` to `GND` while you pull `EN` to `GND` and release it, then run esptool on the
   adapter's port. Leave the `3V3` pad unconnected while the board is powered from USB or
   24 VAC: two supplies on one rail is not something the design has analysed.
4. **Power.** Flash with 24 VAC unplugged and no valve wires. Read the USB and 24 VAC warning in
   the hardware design first.
5. **If it boots but misbehaves:** `zfflash erase-nvs` clears the settings and keeps the
   firmware; `zfflash flash factory` clears everything.

A board with no valid firmware (a blank or half-written flash) should show up as a USB serial
device after power-up and accept `zfflash flash factory`. The ROM bootloader, not ZoneFlow
code, provides that.

## What is verified

CI runs these on every pull request that touches the release tooling (workflow `release`), on
the same code path a tag uses, without publishing anything:

| Check | What it shows |
|---|---|
| Firmware build with the packed web interface, version taken from the tag | The images exist and the application image's embedded version equals the release version. |
| Factory image layout, parsed from the output files | The partition table inside the image equals `partitions.csv` and has a valid MD5 entry; the bootloader is at `0x0` with an ESP32-S3 header, a valid SHA-256 and the right flash size; the application is at the `ota_0` offset with chip id ESP32-S3, a valid SHA-256, project `zoneflow-fw`, the release version, and fits its slot; `www` is a LittleFS image of the partition's size; `otadata` is at its partition; every other byte is `0xff`; the OTA file equals the application inside the factory image. |
| Manifest against the ESP Web Tools schema | Valid keys and types; the single part is the factory image at offset 0; no Improv probe, because the firmware has none. |
| `SHA256SUMS` and `release.json` | Match every file. |
| `zfflash` and `zfrelease` unit tests | `zfflash` with serial, port list and HTTP replaced by fakes: esptool arguments, refusal of a file that fails the checksum, REST calls and their authentication. |
| Installer page logic | The offsets and commands the page prints equal those in `release.json`. |
| Vendored ESP Web Tools | Files equal the pinned SHA-256 list and the page loads them with an integrity hash. |

**Needs a board, not checked:** that the bootloader starts the image; that the firmware boots
and keeps valves off; that ESP Web Tools or esptool can talk to the board's USB port and
finish a flash; that Bluetooth and the access point provisioning work after a factory flash;
that an OTA upload, rollback and a USB update behave as described above; the BOOT-button and
J8 recovery.

**Boot in QEMU was tried and is not in CI.** Espressif's QEMU (`esp-develop-9.2.2-20260417`)
runs the ESP32-S3 ROM, the second-stage bootloader, reads the partition table and loads the
application from the factory image. It then stops at start-up: it does not accept the octal PSRAM
setup (`octal_psram: PSRAM chip is not connected`, with `is_octal` and `size_mbytes` set), which
`sdkconfig.defaults` requires, so the firmware aborts before its main task. The release
image logs over USB Serial/JTAG, which QEMU does not provide, so a smoke test of the release
image would see no output either. A test build with a UART console and no PSRAM would run a
different image from the one released, so it would not show that the release boots.

## Cutting a release (maintainers)

1. Make sure `main` has the firmware you want and CI is green.
2. Tag and push (a signed tag is better): `git tag -s v0.1.0 -m "ZoneFlow 0.1.0" && git push origin v0.1.0`.
3. The `release` workflow builds, verifies and creates the GitHub release with generated notes
   (a tag with a `-` in it is a pre-release). Check the notes and the attached files.
4. The workflow then starts the `docs` workflow, which copies the release into the site and
   deploys it. Check `https://syntroniks.github.io/zoneflow/firmware/web-installer/`.
5. If you edit or delete a release by hand, run the `docs` workflow from the Actions tab to
   refresh the site.

To try the workflow without a release, run it from the Actions tab on a branch: it builds
and verifies, uploads the files as a workflow artefact and publishes nothing.
