import hashlib
import json
import re
from pathlib import Path

import pytest
from conftest import CSV, VERSION, image, make_build, partition_table

from zfrelease import images, manifest, package, partitions, release, verify, version

REPO = Path(__file__).resolve().parents[3]


# ---- version ----------------------------------------------------------------


def test_version_from_tag():
    assert version.from_tag("v0.1.0") == "0.1.0"
    assert version.from_tag("refs/tags/v1.2.3-rc.1") == "1.2.3-rc.1"
    assert version.is_prerelease("1.2.3-rc.1") and not version.is_prerelease("1.2.3")


@pytest.mark.parametrize("bad", ["0.1.0", "v1.2", "v1.2.3+build", "v1.2.3-", "vx.y.z", "main", "v1.2.3-" + "a" * 30])
def test_version_rejects(bad):
    with pytest.raises(version.VersionError):
        version.from_tag(bad)


def test_dev_version():
    assert version.dev_version("5B0C4BF123") == "0.0.0-dev.5b0c4bf"
    assert version.is_prerelease(version.dev_version("abcdef0"))


# ---- partitions and images --------------------------------------------------


def test_csv_and_binary_agree():
    assert partitions.parse_binary(partition_table()) == partitions.parse_csv(CSV)


def test_real_partitions_csv_parses():
    parts = partitions.parse_csv((REPO / "firmware/zoneflow-fw/partitions.csv").read_text())
    assert [p.name for p in parts] == ["nvs", "otadata", "ota_0", "ota_1", "www", "coredump"]
    assert partitions.by_name(parts, "www").size == 0x7C0000


def test_csv_needs_explicit_offsets():
    with pytest.raises(partitions.PartitionError, match="offset is empty"):
        partitions.parse_csv("nvs, data, nvs, , 0x6000\n")


def test_csv_rejects_overlap():
    with pytest.raises(partitions.PartitionError, match="overlap"):
        partitions.parse_csv("a,data,nvs,0x9000,0x6000\nb,data,nvs,0xa000,0x6000\n")


def test_csv_size_suffixes():
    (p,) = partitions.parse_csv("a,app,ota_0,0x10000,1M\n")
    assert p.size == 1 << 20


def test_binary_md5_is_checked():
    raw = bytearray(partition_table())
    raw[8] ^= 0x01  # an offset byte of the first entry
    with pytest.raises(partitions.PartitionError, match="MD5"):
        partitions.parse_binary(bytes(raw))


def test_app_image_parse_and_digest():
    app = image(desc=("1.2.3", "zoneflow-fw"))
    assert images.check_chip(app, "esp32s3").chip_id == 9
    desc = images.parse_app_desc(app)
    assert (desc.version, desc.project_name) == ("1.2.3", "zoneflow-fw")
    images.check_digest(app)
    bad = bytearray(app)
    bad[100] ^= 1
    with pytest.raises(images.ImageError, match="SHA-256"):
        images.check_digest(bytes(bad))


def test_wrong_chip_and_no_app_desc():
    with pytest.raises(images.ImageError, match="chip id"):
        images.check_chip(image(chip=5), "esp32s3")
    with pytest.raises(images.ImageError, match="no app description"):
        images.parse_app_desc(image())


# ---- manifest ---------------------------------------------------------------


def test_manifest_schema():
    ok = manifest.build("n", "1", "esp32s3", [("f.bin", 0)], improv=False)
    manifest.validate(ok)
    assert ok["new_install_improv_wait_time"] == 0
    assert "new_install_improv_wait_time" not in manifest.build("n", "1", "esp32s3", [("f.bin", 0)], improv=True)


@pytest.mark.parametrize(
    "mutate",
    [
        lambda m: m.update(new_install_improv_wat_time=0),  # a typo is an error, not a silent no-op
        lambda m: m["builds"][0].update(chipFamily="ESP32-S9"),
        lambda m: m["builds"][0]["parts"][0].update(path="dir/f.bin"),
        lambda m: m["builds"][0]["parts"][0].update(offset="0x0"),
        lambda m: m.update(builds=[]),
        lambda m: m.pop("version"),
    ],
)
def test_manifest_schema_rejects(mutate):
    m = manifest.build("n", "1", "esp32s3", [("f.bin", 0)], improv=False)
    mutate(m)
    with pytest.raises(manifest.ManifestError):
        manifest.validate(m)


# ---- package ----------------------------------------------------------------


def pkg(tmp_path, build_dir, csv_path, version=VERSION):
    out = tmp_path / "dist"
    frag = package.package_standalone(
        build_dir=build_dir, partitions_csv=csv_path, version=version, out_dir=out, commit="abc1234"
    )
    return out, frag


def test_package_writes_files_and_offsets_from_the_build(tmp_path, build_dir, csv_path):
    out, frag = pkg(tmp_path, build_dir, csv_path)
    names = {p.name for p in out.iterdir()}
    assert f"zoneflow-standalone-{VERSION}-factory.bin" in names
    assert f"zoneflow-standalone-{VERSION}-app.bin" in names
    assert "manifest-standalone.json" in names and "flavour-standalone.json" in names
    assert frag["images"]["app"]["offset"] == 0x20000
    assert frag["images"]["www"]["offset"] == 0x60000
    assert (out / frag["images"]["factory"]["file"]).stat().st_size == 4 << 20
    assert (out / frag["images"]["app"]["file"]).read_bytes() == (build_dir / "zoneflow-fw.bin").read_bytes()


def test_package_refuses_offset_that_differs_from_partition_table(tmp_path, csv_path):
    build = make_build(tmp_path, app_offset="0x40000")
    with pytest.raises(package.PackageError, match="app is flashed at 0x40000"):
        pkg(tmp_path, build, csv_path)


def test_package_refuses_version_that_is_not_the_tag(tmp_path, build_dir, csv_path):
    with pytest.raises(package.PackageError, match="version.txt"):
        pkg(tmp_path, build_dir, csv_path, version="9.9.9")


def test_package_refuses_wrong_project(tmp_path, csv_path):
    build = make_build(tmp_path, project="other-fw")
    with pytest.raises(package.PackageError, match="project name"):
        pkg(tmp_path, build, csv_path)


def test_package_refuses_build_without_web_ui(tmp_path, build_dir, csv_path):
    args = json.loads((build_dir / "flasher_args.json").read_text())
    del args["www"]
    del args["flash_files"]["0x60000"]
    (build_dir / "flasher_args.json").write_text(json.dumps(args))
    with pytest.raises(package.PackageError, match="web UI was not packed"):
        pkg(tmp_path, build_dir, csv_path)


def test_package_refuses_table_that_differs_from_csv(tmp_path, build_dir, csv_path):
    csv_path.write_text(CSV.replace("0x20000,  0x20000", "0x20000,  0x10000"))
    with pytest.raises(package.PackageError, match="differs from partitions.csv"):
        pkg(tmp_path, build_dir, csv_path)


# ---- assemble and verify ----------------------------------------------------


def flavours_with(tmp_path: Path, **enabled: bool) -> Path:
    """The repository's flavours.toml with some flavours switched on or off."""
    text = (REPO / "tools/zfrelease/flavours.toml").read_text()
    for fid, on in enabled.items():
        text, n = re.subn(rf"(\[{fid}\]\nenabled = )(true|false)", rf"\g<1>{'true' if on else 'false'}", text)
        assert n == 1, fid
    path = tmp_path / f"flavours-{'-'.join(f'{k}{int(v)}' for k, v in enabled.items())}.toml"
    path.write_text(text)
    return path


@pytest.fixture
def released(tmp_path, build_dir, csv_path):
    out, _ = pkg(tmp_path, build_dir, csv_path)
    release.assemble(
        directory=out,
        version=VERSION,
        tag=f"v{VERSION}",
        commit="abc1234",
        prerelease=False,
        flavours_toml=flavours_with(tmp_path, standalone=True, esphome=False),
    )
    return out


def test_verify_passes_and_reports_each_check(released, csv_path):
    lines: list[str] = []
    verify.verify_directory(released, csv_path, report=lines.append)
    text = "\n".join(lines)
    for expect in (
        "partition table at 0x8000",
        "bootloader at 0x0",
        "application image at 0x20000",
        "LittleFS",
        "Improv",
    ):
        assert expect in text
    assert not (released / "flavour-standalone.json").exists()  # folded into release.json


def rewrite(directory: Path, name: str, fn):
    p = directory / name
    p.write_bytes(fn(bytearray(p.read_bytes())))
    release.write_sums(directory)  # so the failure is the check under test, not the sums


def factory_of(directory: Path) -> str:
    return next(p.name for p in directory.iterdir() if p.name.endswith("-factory.bin"))


def test_verify_detects_stray_data_in_a_gap(released, csv_path):
    def poke(b):
        b[0x45000] = 0x00  # inside ota_1, which must stay blank
        return bytes(b)

    rewrite(released, factory_of(released), poke)
    rel = json.loads((released / "release.json").read_text())
    f = rel["flavours"][0]["images"]["factory"]
    f["sha256"] = hashlib.sha256((released / f["file"]).read_bytes()).hexdigest()
    (released / "release.json").write_text(json.dumps(rel))
    release.write_sums(released)
    with pytest.raises(verify.VerifyError, match="belongs to no image"):
        verify.verify_directory(released, csv_path, report=lambda _: None)


def test_verify_detects_changed_partition_table(released, csv_path):
    csv_path.write_text(CSV.replace("0x60000,  0x20000", "0x60000,  0x18000"))
    with pytest.raises(verify.VerifyError, match="differs from partitions.csv"):
        verify.verify_directory(released, csv_path, report=lambda _: None)


def test_verify_detects_sums_mismatch(released, csv_path):
    (released / "manifest-standalone.json").write_text("{}")
    with pytest.raises(release.ReleaseError, match="does not match SHA256SUMS"):
        verify.verify_directory(released, csv_path, report=lambda _: None)


def test_verify_detects_manifest_pointing_elsewhere(released, csv_path):
    m = json.loads((released / "manifest-standalone.json").read_text())
    m["builds"][0]["parts"][0]["offset"] = 0x20000
    (released / "manifest-standalone.json").write_text(json.dumps(m))
    release.write_sums(released)
    rel = json.loads((released / "release.json").read_text())
    p = released / "manifest-standalone.json"
    rel["flavours"][0]["files"]["manifest-standalone.json"]["sha256"] = hashlib.sha256(p.read_bytes()).hexdigest()
    (released / "release.json").write_text(json.dumps(rel))
    release.write_sums(released)
    with pytest.raises(verify.VerifyError, match="factory image, alone, at offset 0"):
        verify.verify_directory(released, csv_path, report=lambda _: None)


def test_assemble_requires_every_enabled_flavour(tmp_path, build_dir, csv_path):
    out, _ = pkg(tmp_path, build_dir, csv_path)
    toml = flavours_with(tmp_path, standalone=True, esphome=True)
    with pytest.raises(release.ReleaseError, match="esphome.*missing"):
        release.assemble(directory=out, version=VERSION, tag="", commit="", prerelease=False, flavours_toml=toml)


def test_assemble_rejects_fragment_of_disabled_flavour(tmp_path, build_dir, csv_path):
    out, _ = pkg(tmp_path, build_dir, csv_path)
    (out / "flavour-esphome.json").write_text("{}")
    toml = flavours_with(tmp_path, standalone=True, esphome=False)
    with pytest.raises(release.ReleaseError, match="not enabled"):
        release.assemble(directory=out, version=VERSION, tag="", commit="", prerelease=False, flavours_toml=toml)


def test_prebuilt_flavour_packages_and_verifies(tmp_path, csv_path):
    """The path a second flavour takes: a merged image and an OTA image from another build tool."""
    toml = flavours_with(tmp_path, standalone=False, esphome=True)
    factory, app = tmp_path / "f.bin", tmp_path / "a.bin"
    factory.write_bytes(image(payload=300) + b"\xff" * 4096)
    # ESPHome names the application after the node: `zoneflow` in factory.yaml.
    app.write_bytes(image(desc=("2026.10.2", "zoneflow")))
    out = tmp_path / "dist"
    package.package_prebuilt(
        flavour_id="esphome", factory=factory, app=app, version=VERSION, out_dir=out, flavours_toml=toml
    )
    # assemble and verify read the repository list; point them at the temp one.
    release.assemble(directory=out, version=VERSION, tag="", commit="", prerelease=False, flavours_toml=toml)
    m = json.loads((out / "manifest-esphome.json").read_text())
    assert "new_install_improv_wait_time" not in m  # ESPHome keeps Improv


def test_stage_site_checks_sums_and_copies(released, tmp_path):
    site = tmp_path / "site"
    dest = release.stage_site(released, site)
    assert dest == site / "firmware" / "releases" / "latest"
    assert (dest / "release.json").is_file() and (dest / "SHA256SUMS").is_file()
    (released / "manifest-standalone.json").write_text("tampered")
    with pytest.raises(release.ReleaseError, match="does not match"):
        release.stage_site(released, site)


def test_notes_carry_the_sums_digest_and_the_caveat(released):
    text = release.notes(released)
    assert hashlib.sha256((released / "SHA256SUMS").read_bytes()).hexdigest() in text
    assert "No ZoneFlow board has run this firmware" in text
    assert f"zoneflow-standalone-{VERSION}-app.bin" in text
