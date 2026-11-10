# zfflash

A command-line installer for ZoneFlow, for people who cannot use the
[web installer](../../docs/firmware/web-installer.md) (it needs a Chromium browser).
Run it with `uv`; nothing is installed system-wide.

```sh
cd tools/zfflash
uv run zfflash ports                          # which port is the board?
uv run zfflash fetch                          # download the latest release, check every file
uv run zfflash flash factory                  # blank board: whole flash
uv run zfflash flash app www                  # update over USB, keep settings
uv run zfflash erase-nvs                      # factory reset from outside
uv run zfflash backup  --host zoneflow-ab12.local --out zoneflow-backup.json
uv run zfflash restore --host zoneflow-ab12.local --file zoneflow-backup.json
uv run zfflash ota     --host zoneflow-ab12.local          # update through the REST API
```

Every image is checked against the release's `SHA256SUMS` (and against `release.json`)
before anything is written. By default the release comes from the project site
(`https://syntroniks.github.io/zoneflow/firmware/releases/latest/`). Use `--release v0.1.0`
for a GitHub release, or `--source DIR` for a directory you downloaded.
The checksums come from the same place as the files, so they catch damage, not a hostile
server. For that, pass `--sums-sha256 <hex>`, the digest of `SHA256SUMS` printed in the
release notes on GitHub.

The admin password is read from `ZF_PASSWORD` or prompted for, never taken from a flag
(flags show in `ps`).

Details, including what each command does to the board and what has not been tried on a
real one: [Releases and updating](../../docs/firmware/releases.md).

## Tests

```sh
uv run pytest
```

The serial layer (esptool), the port list and HTTP are replaced by fakes; the tests check
the exact esptool arguments, the checksum refusal, the REST calls and their authentication.
No board was involved.
