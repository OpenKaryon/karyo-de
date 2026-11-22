#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create checksummed Debian quilt inputs for the ordinary Builder source stage."""
import argparse
from datetime import datetime, timezone
from email.parser import Parser
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import uuid

from prepare_swift import (ROOT_REVISION, SWIFT_REVISION, ARCHIVE_SHA256, ARCHIVE_SIZE,
                           SOURCE_PREFIX, digest, matches)
from verify_patches import capture_patches

NAME = "karyo-darling"
VERSION = "0.1.20260906+git60ba801-1"
MAINTAINER = "Karyo Developers <developers@openkaryon.invalid>"


def check_inputs(snapshot, swift, runtime):
    manifest_path = snapshot / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (manifest.get("schema") != 1 or manifest.get("kind") != "source-snapshot"
            or manifest.get("build_ready") is not True
            or manifest.get("source") != {"url": "https://github.com/darlinghq/darling.git", "revision": ROOT_REVISION}):
        raise ValueError("A complete pinned Darling source snapshot is required")
    archive = snapshot / "source.tar.gz"
    record = manifest.get("archive", {})
    if record.get("file") != archive.name or not matches(archive, record.get("bytes"), record.get("sha256")):
        raise ValueError("Complete source archive checksum/size mismatch")
    recovery = json.loads((swift / "provenance.json").read_text(encoding="utf-8"))
    if (recovery.get("status") != "verified" or recovery.get("native_linux_binaries") is not False
            or recovery.get("compiled_from_source") is not False
            or recovery.get("snapshot", {}).get("root_revision") != ROOT_REVISION
            or recovery.get("snapshot", {}).get("swift_revision") != SWIFT_REVISION
            or recovery.get("archive", {}).get("sha256") != ARCHIVE_SHA256
            or recovery.get("archive", {}).get("bytes") != ARCHIVE_SIZE):
        raise ValueError("Verified pinned Swift binary-input provenance is required")
    license_file = runtime / "swift-LICENSE.txt"
    if digest(license_file) != recovery.get("license", {}).get("sha256"):
        raise ValueError("Swift license differs from recovered input provenance")
    files = {}
    for item in manifest.get("lfs", []):
        if item.get("materialized") is not True or not item.get("path", "").startswith(SOURCE_PREFIX) or item["path"] in files:
            raise ValueError("Unresolved, repeated or unexpected binary input")
        files[item["path"]] = item
    recovered = {item["path"]: item for item in recovery.get("files", [])}
    if len(files) != 44 or len(recovered) != 44 or len(recovery["files"]) != 44 or files.keys() != recovered.keys():
        raise ValueError("Expected exactly44 matching Swift binary inputs")
    for path, item in files.items():
        if any(item.get(key) != recovered[path].get(key) for key in ("sha256", "bytes", "pointer_blob")):
            raise ValueError("Materialized input differs from its recovered source pointer")
    seen = set()
    with tarfile.open(archive, mode="r|gz") as tar:
        for member in tar:
            if member.name not in files:
                continue
            expected = files[member.name]
            if member.name in seen or not member.isfile() or member.size != expected["bytes"]:
                raise ValueError("Unexpected materialized Swift source member")
            value = hashlib.sha256()
            stream = tar.extractfile(member)
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                value.update(block)
            if value.hexdigest() != expected["sha256"]:
                raise ValueError("Materialized Swift bytes do not match the source pointer")
            seen.add(member.name)
    if seen != files.keys():
        raise ValueError("Incomplete materialized Swift source archive")
    patches, _, _ = capture_patches(runtime / "patches")
    return manifest, recovery, patches


def write_overlay(path, files, epoch):
    with path.open("wb") as raw:
        with gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as zipped:
            with tarfile.open(fileobj=zipped, mode="w|", format=tarfile.PAX_FORMAT) as tar:
                for name, data in sorted(files.items()):
                    member = tarfile.TarInfo(name)
                    member.mode = 0o755 if name == "debian/rules" else 0o644
                    member.uid = member.gid = 0
                    member.uname = member.gname = ""
                    member.mtime = epoch
                    member.size = len(data)
                    tar.addfile(member, io.BytesIO(data))


def source_control(parts, control):
    hashes = []
    for path in parts:
        values = {name: hashlib.new(name) for name in ("md5", "sha1", "sha256")}
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                for value in values.values():
                    value.update(block)
        hashes.append((path.name, path.stat().st_size, {key: value.hexdigest() for key, value in values.items()}))
    headers = Parser().parsestr(control.split("\n\n", 1)[0])
    text = ("Format: 3.0 (quilt)\nSource: " + NAME + "\nBinary: karyo-darling-runtime\nArchitecture: amd64\nVersion: " + VERSION
            + "\nMaintainer: " + MAINTAINER + "\nBuild-Depends: " + headers["Build-Depends"]
            + "\nStandards-Version: 4.7.0\nPackage-List:\n karyo-darling-runtime deb misc optional arch=amd64\n")
    for field, key in (("Checksums-Sha1", "sha1"), ("Checksums-Sha256", "sha256"), ("Files", "md5")):
        text += field + ":\n"
        for name, size, values in hashes:
            text += " " + values[key] + " " + str(size) + " " + name + "\n"
    return text


def prepare(snapshot, swift, verification, output):
    snapshot, swift = Path(snapshot).resolve(strict=True), Path(swift).resolve(strict=True)
    runtime = Path(__file__).resolve().parent
    manifest, recovery, patches = check_inputs(snapshot, swift, runtime)
    verification = Path(verification).resolve(strict=True)
    verified_bytes = verification.read_bytes()
    verified = json.loads(verified_bytes)
    if (verified.get("status") != "verified" or verified.get("quilt_zero_fuzz") is not True
            or verified.get("archive") != manifest["archive"]
            or verified.get("source") != manifest["source"]
            or verified.get("checker_sha256") != digest(runtime / "verify_patches.py")
            or verified.get("policy_sha256") != digest(runtime / "patch_policy.py")
            or [(p["file"], p["sha256"]) for p in verified.get("patches", [])] != [(p["file"], p["sha256"]) for p in patches]
            or verified.get("quilt_forward") != [p["file"] for p in patches]
            or verified.get("quilt_reverse") != list(reversed(verified["quilt_forward"]))):
        raise ValueError("Current native zero-fuzz quilt verification is required for source packaging")
    epoch = manifest["source_date_epoch"]
    if type(epoch) is not int or not 0 <= epoch <= 253402300799:
        raise ValueError("Invalid source timestamp")
    inputs = {"debian/control": (runtime / "packaging/control").read_bytes(),
              "debian/rules": (runtime / "packaging/rules").read_bytes(),
              "debian/copyright": (runtime / "packaging/copyright").read_bytes(),
              "debian/source/format": b"3.0 (quilt)\n", "debian/patches/series": b"",
              "debian/karyo/native_audit.py": (runtime / "packaging/native_audit.py").read_bytes(),
              "debian/karyo/swift-LICENSE.txt": (runtime / "swift-LICENSE.txt").read_bytes()}
    date = datetime.fromtimestamp(epoch, timezone.utc).strftime("%a, %d %b %Y %H:%M:%S +0000")
    inputs["debian/changelog"] = (NAME + " (" + VERSION + ") unstable; urgency=medium\n\n"
        "  * Prepare pinned stock source and native Karyo packaging.\n\n -- " + MAINTAINER + "  " + date + "\n").encode()
    inputs["debian/karyo/complete-source-manifest.json"] = (snapshot / "manifest.json").read_bytes()
    inputs["debian/karyo/patch-verification.json"] = verified_bytes
    inputs["debian/karyo/swift-provenance.json"] = (swift / "provenance.json").read_bytes()
    provenance = {"schema": 1, "source": manifest["source"], "source_archive": manifest["archive"],
        "source_snapshot_manifest_sha256": digest(snapshot / "manifest.json"), "modules": manifest["modules"],
        "source_date_epoch": epoch, "swift_inputs": recovery, "patches": [{key: value for key, value in p.items() if key in ("file", "sha256")} for p in patches],
        "packaging_files": {name: hashlib.sha256(data).hexdigest() for name, data in inputs.items()},
        "preparer_sha256": digest(Path(__file__)), "runtime_tested": False}
    inputs["debian/karyo/source-provenance.json"] = (json.dumps(provenance, indent=2) + "\n").encode()
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    job = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    stage = output / (".partial-darling-source-" + job)
    stage.mkdir()
    try:
        original = stage / (NAME + "_" + VERSION.rsplit("-", 1)[0] + ".orig.tar.gz")
        shutil.copyfile(snapshot / "source.tar.gz", original)
        if digest(original) != manifest["archive"]["sha256"]:
            raise ValueError("Source archive changed while copying")
        overlay = stage / (NAME + "_" + VERSION + ".debian.tar.gz")
        write_overlay(overlay, inputs, epoch)
        dsc = stage / (NAME + "_" + VERSION + ".dsc")
        dsc.write_text(source_control((original, overlay), inputs["debian/control"].decode()), encoding="utf-8", newline="\n")
        patch_folder = stage / "patches"
        patch_folder.mkdir()
        for item in patches:
            (patch_folder / item["file"]).write_bytes(item["data"])
        def entry(path):
            return {"path": path.relative_to(stage).as_posix(), "sha256": digest(path)}
        for distribution, suffix in (("trixie", "debian13"), ("resolute", "ubuntu2604")):
            recipe = {"schema": 1, "source": {"name": NAME, "version": VERSION, "dsc": entry(dsc),
                "archives": [entry(original), entry(overlay)]}, "patches": [entry(patch_folder / p["file"]) for p in patches],
                "build": {"revision": "karyo1." + suffix, "distribution": distribution, "maintainer": MAINTAINER,
                          "message": "Build audited stock runtime with common integration fixes.",
                          "binaries": ["karyo-darling-runtime"], "source_date_epoch": epoch}}
            (stage / (distribution + ".json")).write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8")
        (stage / "preparation.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
        final = output / ("karyo-darling-source-" + job)
        if final.parent != output or stage.parent != output:
            raise ValueError("Source preparation escaped its selected output")
        stage.rename(final)
    except BaseException:
        failed = output / (".failed-darling-source-" + job)
        if failed.parent != output or stage.parent != output:
            raise ValueError("Failed preparation escaped its selected output")
        stage.rename(failed)
        raise
    print("Darling Debian source inputs: " + str(final))
    return final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot")
    parser.add_argument("--swift-provenance", required=True)
    parser.add_argument("--patch-verification", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        prepare(args.snapshot, args.swift_provenance, args.patch_verification, args.output)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, tarfile.TarError) as error:
        print("prepare-source: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
