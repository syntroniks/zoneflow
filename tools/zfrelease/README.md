# zfrelease

Builds the files of a ZoneFlow firmware release from an ESP-IDF build and checks them.
Used by `.github/workflows/release.yml`; you can run every step locally.

```sh
cd tools/zfrelease
uv run zfrelease version --tag v0.1.0
uv run zfrelease package --tag v0.1.0 --build-dir ../../firmware/zoneflow-fw/build \
    --partitions ../../firmware/zoneflow-fw/partitions.csv --out dist
uv run zfrelease assemble --tag v0.1.0 dist
uv run zfrelease verify dist --partitions ../../firmware/zoneflow-fw/partitions.csv
uv run pytest
```

(`package` checks that the firmware's version equals the tag. Write the version to
`firmware/zoneflow-fw/version.txt` before `idf.py build`; ESP-IDF reads it.)

## What it produces

In one flat directory (the same names on the GitHub release and on the docs site):

| File | Use |
|---|---|
| `zoneflow-standalone-<v>-factory.bin` | One image for the whole flash of a blank board, written at offset 0: bootloader, partition table, OTA data, application, web UI, and `0xff` elsewhere. The installer page and `zfflash` use it. |
| `zoneflow-standalone-<v>-app.bin` | The application alone, for upload in the device's web UI or `POST /api/v1/firmware/upload`. |
| `zoneflow-standalone-<v>-www.bin` | The web UI image alone (`POST /api/v1/firmware/assets`). |
| `...-bootloader.bin`, `...-partition-table.bin`, `...-otadata.bin` | The rest, for flashing by hand with the offsets in `release.json`. |
| `manifest-standalone.json` | ESP Web Tools manifest. |
| `release.json` | Everything above with offsets, sizes and SHA-256. The installer page and `zfflash` read it. |
| `SHA256SUMS` | `sha256sum -c SHA256SUMS` checks the lot. |

Offsets are not written in the tool. `package` takes them from the build's
`flasher_args.json` and stops unless they equal `partitions.csv` and the partition
table the build wrote. `verify` reads the output files only: it parses the partition
table, bootloader and application header inside the factory image.

## Flavours

`flavours.toml` lists them. The ESPHome slot is there and disabled. To add it, see the
comment at the top of that file; `package-prebuilt` takes ESPHome's `firmware.factory.bin`
and `firmware.ota.bin`.

## Limits

`verify` shows the files are consistent and well formed. It does not show that the
firmware boots: that needs a board. See `docs/firmware/releases.md`.
