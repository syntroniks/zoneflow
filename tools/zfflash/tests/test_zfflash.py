import json
import subprocess
from pathlib import Path

import pytest
from conftest import BOARD, FILES, OTHER, FakeHttp, release_files, sha

from zfflash import cli, flasher, ports
from zfflash.device import Device
from zfflash.http import Response
from zfflash.release import ReleaseError, open_release

FACTORY, APP, WWW = (f"zoneflow-standalone-1.2.3-{k}.bin" for k in ("factory", "app", "www"))


def run(env, *argv, cache: Path):
    return cli.main([*argv], env) if "--cache-dir" in argv else cli.main([*argv, "--cache-dir", str(cache)], env)


# ---- ports ------------------------------------------------------------------


def test_port_choice():
    assert ports.choose("COM3", lambda: []) == "COM3"
    assert ports.choose(None, lambda: [OTHER, BOARD]) == BOARD.device
    with pytest.raises(ports.PortError, match="no ESP32-S3 serial port"):
        ports.choose(None, lambda: [OTHER])
    two = ports.Port("/dev/ttyACM1", "x", 0x303A, 0x1001)
    with pytest.raises(ports.PortError, match="more than one"):
        ports.choose(None, lambda: [BOARD, two])


def test_native_usb_wins_over_bridges():
    bridge = ports.Port("/dev/ttyUSB0", "CP2102", 0x10C4, 0xEA60)
    assert ports.candidates([bridge, BOARD]) == [BOARD]
    assert ports.candidates([bridge]) == [bridge]


# ---- release verification ---------------------------------------------------


def test_fetch_verifies_every_file(release_dir, make_env, tmp_path):
    env = make_env()
    assert run(env, "fetch", "--source", str(release_dir), cache=tmp_path / "c") == 0
    assert env.console.out.getvalue().count("ok  ") == 3


def test_flash_refuses_a_file_that_does_not_match_the_checksums(release_dir, make_env, tmp_path):
    (release_dir / FACTORY).write_bytes(b"X" * 5000)  # tampered after SHA256SUMS was written
    env = make_env()
    assert run(env, "flash", "factory", "-y", "--source", str(release_dir), cache=tmp_path / "c") == 1
    assert "published checksum" in env.console.err.getvalue()
    assert env.flasher_fake.calls == []  # nothing was written
    assert not (tmp_path / "c" / "1.2.3" / FACTORY).exists()  # and the bad copy was not kept


def test_flash_refuses_file_not_in_sums(release_dir, make_env, tmp_path):
    lines = (release_dir / "SHA256SUMS").read_text().splitlines()
    (release_dir / "SHA256SUMS").write_text("\n".join(x for x in lines if FACTORY not in x) + "\n")
    # release.json is still listed in SHA256SUMS, so only the factory entry is gone
    env = make_env()
    assert run(env, "flash", "factory", "-y", "--source", str(release_dir), cache=tmp_path / "c") == 1
    assert "not listed" in env.console.err.getvalue()
    assert env.flasher_fake.calls == []


def test_release_json_must_match_sums(release_dir, make_env, tmp_path):
    (release_dir / "release.json").write_text(json.dumps({"schema": 1, "version": "9", "flavours": []}))
    env = make_env()
    assert run(env, "fetch", "--source", str(release_dir), cache=tmp_path / "c") == 1
    assert "does not match SHA256SUMS" in env.console.err.getvalue()


def test_pinned_sums_digest(release_dir):
    good = sha((release_dir / "SHA256SUMS").read_bytes())
    open_release(str(release_dir), FakeHttp(), sums_sha256=good)
    with pytest.raises(ReleaseError, match="does not have SHA-256"):
        open_release(str(release_dir), FakeHttp(), sums_sha256="0" * 64)


def test_release_over_http_downloads_and_verifies(tmp_path):
    base = "https://example.test/rel/"
    http = FakeHttp({("GET", base + n): Response(200, d) for n, d in release_files().items()})
    rel = open_release(base.rstrip("/"), http, tmp_path)
    assert rel.fetch(APP).read_bytes() == FILES[APP]
    http.routes[("GET", base + WWW)] = Response(200, b"not the web ui")
    with pytest.raises(ReleaseError, match="Nothing was flashed"):
        rel.fetch(WWW)


def test_default_source_is_the_project_site(make_env, tmp_path):
    http = FakeHttp()
    env = make_env(http=http)
    assert cli.main(["fetch", "--cache-dir", str(tmp_path)], env) == 1
    assert http.calls[0]["url"] == "https://syntroniks.github.io/zoneflow/firmware/releases/latest/SHA256SUMS"
    http = FakeHttp()
    cli.main(["fetch", "--release", "v1.2.3", "--cache-dir", str(tmp_path)], make_env(http=http))
    assert http.calls[0]["url"] == "https://github.com/syntroniks/zoneflow/releases/download/v1.2.3/SHA256SUMS"


# ---- flashing ---------------------------------------------------------------


def test_flash_factory_writes_whole_flash_at_zero(release_dir, make_env, tmp_path):
    env = make_env()
    assert run(env, "flash", "factory", "-y", "--source", str(release_dir), cache=tmp_path / "c") == 0
    (call,) = env.flasher_fake.calls
    assert call == ("write", BOARD.device, 460800, "esp32s3", [(0, FACTORY, FILES[FACTORY])])
    assert "replaces everything" in env.console.out.getvalue()


def test_flash_asks_first_and_cancels(release_dir, make_env, tmp_path):
    env = make_env(interactive=True, answers=["n"])
    assert run(env, "flash", "factory", "--source", str(release_dir), cache=tmp_path / "c") == 1
    assert env.flasher_fake.calls == [] and "cancelled" in env.console.err.getvalue()
    env = make_env(interactive=True, answers=["y"])
    assert run(env, "flash", "factory", "--source", str(release_dir), cache=tmp_path / "c") == 0
    assert len(env.flasher_fake.calls) == 1


def test_flash_needs_yes_when_not_interactive(release_dir, make_env, tmp_path):
    env = make_env(interactive=False)
    assert run(env, "flash", "factory", "--source", str(release_dir), cache=tmp_path / "c") == 1
    assert "--yes" in env.console.err.getvalue() and env.flasher_fake.calls == []


def test_flash_app_blanks_otadata_then_writes_ota_0_and_www(release_dir, make_env, tmp_path):
    env = make_env()
    assert run(env, "flash", "app", "www", "-y", "--port", "COM7", "--baud", "115200", "--source", str(release_dir),
               cache=tmp_path / "c") == 0  # fmt: skip
    assert env.flasher_fake.calls == [
        ("erase", "COM7", 115200, "esp32s3", 0x1E000, 0x2000),
        ("write", "COM7", 115200, "esp32s3", [(0x20000, APP, FILES[APP])]),
        ("write", "COM7", 115200, "esp32s3", [(0x820000, WWW, FILES[WWW])]),
    ]
    assert "no rollback" in env.console.out.getvalue()


def test_factory_cannot_be_combined(release_dir, make_env, tmp_path):
    env = make_env()
    assert run(env, "flash", "factory", "app", "-y", "--source", str(release_dir), cache=tmp_path / "c") == 1
    assert env.flasher_fake.calls == []


def test_flash_without_a_board_says_so(release_dir, make_env, tmp_path):
    env = make_env(ports=[OTHER])
    assert run(env, "flash", "factory", "-y", "--source", str(release_dir), cache=tmp_path / "c") == 1
    assert "no ESP32-S3 serial port" in env.console.err.getvalue()


def test_erase_nvs_uses_the_partition_from_the_release(release_dir, make_env, tmp_path):
    env = make_env()
    assert run(env, "erase-nvs", "-y", "--source", str(release_dir), cache=tmp_path / "c") == 0
    assert env.flasher_fake.calls == [("erase", BOARD.device, 460800, "esp32s3", 0x9000, 0x15000)]
    assert "setup code" in env.console.out.getvalue()


def test_esptool_arguments(monkeypatch, tmp_path):
    seen = []
    monkeypatch.setattr(subprocess, "run", lambda cmd, check: seen.append(cmd) or subprocess.CompletedProcess(cmd, 0))
    f = flasher.EsptoolFlasher()
    f.write("/dev/ttyACM0", 460800, "esp32s3", [(0x0, tmp_path / "f.bin")])
    f.erase_region("/dev/ttyACM0", 460800, "esp32s3", 0x9000, 0x15000)
    assert seen[0][1:] == [
        "-m", "esptool", "--chip", "esp32s3", "--port", "/dev/ttyACM0", "--baud", "460800", "write-flash",
        "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep", "0x0", str(tmp_path / "f.bin"),
    ]  # fmt: skip
    assert seen[1][-3:] == ["erase-region", "0x9000", "0x15000"]
    monkeypatch.setattr(subprocess, "run", lambda cmd, check: subprocess.CompletedProcess(cmd, 2))
    with pytest.raises(flasher.FlashError, match="exit code 2"):
        f.erase_region("p", 1, "esp32s3", 0, 1)


# ---- REST: backup, restore, update -------------------------------------------

API = "http://zoneflow-ab12.local/api/v1"
BACKUP = {"format": "zoneflow-backup", "format_version": 1, "created_utc": 1, "firmware_version": "1.2.3",
          "hostname": "zoneflow-ab12", "config": {"schema": 1}}  # fmt: skip


def login_route(token="tok123"):
    def handler(headers, data):
        ok = json.loads(data) == {"password": "pw"}
        return (
            Response(200, json.dumps({"token": token, "csrf": "c", "idle_timeout_s": 60}).encode())
            if ok
            else Response(401, b'{"error":{"code":"unauthorized","message":"bad password"}}')
        )

    return handler


def test_device_url_forms():
    h = FakeHttp()
    assert Device.at("zoneflow-ab12.local", h).base == API
    assert Device.at("https://10.0.0.5/", h).base == "https://10.0.0.5/api/v1"
    assert Device.at("http://x/api/v1", h).base == "http://x/api/v1"


def test_backup_without_login_when_reads_are_open(make_env, tmp_path):
    http = FakeHttp({("GET", f"{API}/config/backup"): Response(200, json.dumps(BACKUP).encode())})
    out = tmp_path / "b.json"
    env = make_env(http=http)
    assert cli.main(["backup", "--host", "zoneflow-ab12.local", "--out", str(out)], env) == 0
    assert json.loads(out.read_text())["format"] == "zoneflow-backup"
    assert "Authorization" not in http.calls[0]["headers"]
    assert "never contains WiFi credentials" in env.console.out.getvalue()
    assert cli.main(["backup", "--host", "zoneflow-ab12.local", "--out", str(out)], env) == 1  # no overwrite
    assert "exists" in env.console.err.getvalue()


def test_backup_logs_in_when_the_controller_protects_reads(make_env, tmp_path):
    def backup(headers, data):
        if headers.get("Authorization") == "Bearer tok123":
            return Response(200, json.dumps(BACKUP).encode())
        return Response(401, b'{"error":{"code":"unauthorized","message":"sign in"}}')

    http = FakeHttp({("GET", f"{API}/config/backup"): backup, ("POST", f"{API}/auth/login"): login_route()})
    env = make_env(http=http, interactive=True)
    assert cli.main(["backup", "--host", "zoneflow-ab12.local", "--out", str(tmp_path / "b.json")], env) == 0
    assert [c["method"] for c in http.calls] == ["GET", "POST", "GET"]


def test_backup_from_a_script_takes_the_password_from_the_environment(make_env, tmp_path, monkeypatch):
    monkeypatch.setenv("ZF_PASSWORD", "pw")
    http = FakeHttp({("GET", f"{API}/config/backup"): Response(200, json.dumps(BACKUP).encode()),
                     ("POST", f"{API}/auth/login"): login_route()})  # fmt: skip
    env = make_env(http=http, interactive=False)
    assert cli.main(["backup", "--host", "zoneflow-ab12.local", "--out", str(tmp_path / "b.json")], env) == 0
    assert http.calls[-1]["headers"]["Authorization"] == "Bearer tok123"


def test_restore_logs_in_and_posts_the_file(make_env, tmp_path):
    f = tmp_path / "b.json"
    f.write_text(json.dumps(BACKUP))
    http = FakeHttp({("POST", f"{API}/auth/login"): login_route(),
                     ("POST", f"{API}/config/restore"): Response(200, b'{"restored":true}')})  # fmt: skip
    env = make_env(http=http, interactive=True, answers=["y"])
    assert cli.main(["restore", "--host", "zoneflow-ab12.local", "--file", str(f)], env) == 0
    post = http.calls[-1]
    assert post["headers"]["Authorization"] == "Bearer tok123"
    assert post["headers"]["Content-Type"] == "application/json"
    assert json.loads(post["data"]) == BACKUP


def test_restore_reports_the_device_error(make_env, tmp_path):
    f = tmp_path / "b.json"
    f.write_text(json.dumps(BACKUP))
    err = b'{"error":{"code":"unsupported_schema","message":"newer firmware","path":"schema"}}'
    http = FakeHttp(
        {("POST", f"{API}/auth/login"): login_route(), ("POST", f"{API}/config/restore"): Response(400, err)}
    )
    env = make_env(http=http, interactive=True)
    assert cli.main(["restore", "--host", "zoneflow-ab12.local", "--file", str(f), "-y"], env) == 1
    assert "unsupported_schema: newer firmware (schema)" in env.console.err.getvalue()


def test_restore_rejects_a_file_that_is_not_a_backup(make_env, tmp_path):
    f = tmp_path / "x.json"
    f.write_text('{"format":"something-else"}')
    http = FakeHttp()
    env = make_env(http=http)
    assert cli.main(["restore", "--host", "h", "--file", str(f), "-y"], env) == 1
    assert "not a ZoneFlow backup" in env.console.err.getvalue()
    assert http.calls == []  # nothing was sent


def test_wrong_password_is_reported(make_env, tmp_path, monkeypatch):
    f = tmp_path / "b.json"
    f.write_text(json.dumps(BACKUP))
    http = FakeHttp({("POST", f"{API}/auth/login"): login_route()})
    env = make_env(http=http, interactive=True, password="wrong")
    assert cli.main(["restore", "--host", "zoneflow-ab12.local", "--file", str(f), "-y"], env) == 1
    assert "login failed" in env.console.err.getvalue()


def test_ota_uploads_the_checked_image(release_dir, make_env, tmp_path):
    http = FakeHttp({("POST", f"{API}/auth/login"): login_route(),
                     ("POST", f"{API}/firmware/upload"): Response(202, b'{"accepted":true}')})  # fmt: skip
    env = make_env(http=http, interactive=True, answers=["y"])
    assert run(env, "ota", "--host", "zoneflow-ab12.local", "--source", str(release_dir), cache=tmp_path / "c") == 0
    up = http.calls[-1]
    assert up["data"] == FILES[APP] and up["headers"]["Content-Type"] == "application/octet-stream"
    assert up["headers"]["Authorization"] == "Bearer tok123"
    assert "rolls back" in env.console.out.getvalue()


def test_ota_force_and_refusal(release_dir, make_env, tmp_path):
    busy = Response(409, b'{"error":{"code":"zones_active","message":"a zone is running"}}')
    http = FakeHttp({("POST", f"{API}/auth/login"): login_route(), ("POST", f"{API}/firmware/upload"): busy})
    env = make_env(http=http, interactive=True)
    assert (
        run(env, "ota", "--host", "zoneflow-ab12.local", "-y", "--source", str(release_dir), cache=tmp_path / "c") == 1
    )
    assert "zones_active" in env.console.err.getvalue() and "--force" in env.console.err.getvalue()
    run(
        env, "ota", "--host", "zoneflow-ab12.local", "-y", "--force", "--source", str(release_dir), cache=tmp_path / "c"
    )
    assert http.calls[-1]["url"] == f"{API}/firmware/upload?force=true"


def test_ota_www_goes_to_assets(release_dir, make_env, tmp_path):
    http = FakeHttp({("POST", f"{API}/auth/login"): login_route(),
                     ("POST", f"{API}/firmware/assets"): Response(202, b'{"accepted":true}')})  # fmt: skip
    env = make_env(http=http, interactive=True)
    assert run(env, "ota", "--host", "zoneflow-ab12.local", "--image", "www", "-y", "--source", str(release_dir),
               cache=tmp_path / "c") == 0  # fmt: skip
    assert http.calls[-1]["data"] == FILES[WWW]
