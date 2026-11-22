#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Recover exact Swift LFS inputs without installing an upstream Darling DEB."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT_REVISION = "60ba801decee7a00782f74f6be4c8ffb013f79ff"
SWIFT_REVISION = "471514f4b4985a604b192e1facac9bbc53dedad3"
ARCHIVE_URL = "https://github.com/darlinghq/darling/releases/download/v0.1.20230310_update_sources_11_5/darling_0.1.20230310.jammy_amd64.deb"
ARCHIVE_SHA256 = "a4405582a17d3e6b50b6751dc0d5d741f8e23848b014d1ad20731b2efbe7a55f"
ARCHIVE_SIZE = 123429806
EXPECTED_COUNT = 44
SOURCE_PREFIX = "src/external/swift/"
ARCHIVE_PREFIX = "usr/libexec/darling/usr/lib/swift/"
SHA256 = re.compile(r"[0-9a-f]{64}")
SHA1 = re.compile(r"[0-9a-f]{40}")
POINTER = re.compile(rb"version https://git-lfs.github.com/spec/v1\noid sha256:([0-9a-f]{64})\nsize (0|[1-9][0-9]*)\n?")


def digest(path):
    with Path(path).open("rb") as stream:
        result = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def matches(path, size, checksum):
    path = Path(path)
    return (not path.is_symlink() and path.is_file()
            and path.stat().st_size == size and digest(path) == checksum)


def inventory(manifest):
    if (manifest.get("schema") != 1 or manifest.get("kind") != "source-snapshot"
            or manifest.get("source") != {"url": "https://github.com/darlinghq/darling.git", "revision": ROOT_REVISION}):
        raise ValueError("Expected the exact pinned Darling source snapshot")
    modules = [m for m in manifest.get("modules", []) if m.get("path", "").rstrip("/") == SOURCE_PREFIX.rstrip("/")]
    if len(modules) != 1 or modules[0].get("revision") != SWIFT_REVISION or modules[0].get("url") != "https://github.com/darlinghq/darling-swift.git":
        raise ValueError("Swift source identity differs from the reviewed revision")
    files = {}
    for item in manifest.get("lfs", []):
        path = item.get("path", "")
        if not path.startswith(SOURCE_PREFIX):
            continue
        name = path[len(SOURCE_PREFIX):]
        if (not re.fullmatch(r"libswift[A-Za-z0-9_]*\.dylib", name) or name in files
                or not isinstance(item.get("sha256"), str) or not SHA256.fullmatch(item["sha256"])
                or type(item.get("bytes")) is not int or item["bytes"] <= 0
                or not isinstance(item.get("pointer_blob"), str) or not SHA1.fullmatch(item["pointer_blob"])
                or item.get("materialized") is not False):
            raise ValueError("Unexpected, duplicate or already materialized Swift LFS input")
        files[name] = dict(item)
    if len(files) != EXPECTED_COUNT:
        raise ValueError("Pinned Swift inventory must contain exactly " + str(EXPECTED_COUNT) + " dylibs")
    return files


def check_snapshot(folder, manifest, files):
    record = manifest.get("archive", {})
    checksum = record.get("sha256")
    if (record.get("file") != "source.tar.gz" or not isinstance(checksum, str)
            or not SHA256.fullmatch(checksum) or type(record.get("bytes")) is not int):
        raise ValueError("Invalid source archive record")
    archive = folder / "source.tar.gz"
    if not matches(archive, record["bytes"], checksum):
        raise ValueError("Source snapshot checksum/size mismatch")
    seen = set()
    with tarfile.open(archive, mode="r|gz") as tar:
        for member in tar:
            if not member.name.startswith(SOURCE_PREFIX):
                continue
            name = member.name[len(SOURCE_PREFIX):]
            if name not in files:
                continue
            if name in seen or not member.isfile() or member.size > 1024:
                raise ValueError("Unexpected source pointer member: " + member.name)
            data = tar.extractfile(member).read()
            item = files[name]
            oid = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            pointer = POINTER.fullmatch(data.replace(b"\r\n", b"\n"))
            if (not pointer or oid != item["pointer_blob"] or pointer[1].decode() != item["sha256"]
                    or int(pointer[2]) != item["bytes"]):
                raise ValueError("Swift manifest differs from its pinned source pointer: " + name)
            seen.add(name)
    if seen != files.keys():
        raise ValueError("Source snapshot lacks the complete Swift pointer inventory")


def recover_members(stream, files, folder):
    seen = set()
    with tarfile.open(fileobj=stream, mode="r|") as tar:
        for member in tar:
            relative = member.name.removeprefix("./")
            if not relative.startswith(ARCHIVE_PREFIX):
                continue
            name = relative[len(ARCHIVE_PREFIX):]
            if name not in files:
                continue
            item = files[name]
            if name in seen or not member.isfile() or member.size != item["bytes"]:
                raise ValueError("Unexpected Swift archive member: " + member.name)
            target = folder / name
            with target.open("xb") as output:
                shutil.copyfileobj(tar.extractfile(member), output)
            if not matches(target, item["bytes"], item["sha256"]):
                raise ValueError("Swift payload does not match its pinned LFS OID: " + name)
            seen.add(name)
    if seen != files.keys():
        raise ValueError("Official archive lacks the complete pinned Swift inventory")


def prepare(snapshot, archive, objects, output):
    snapshot = Path(snapshot).resolve(strict=True)
    manifest_path = snapshot / "manifest.json"
    if manifest_path.is_symlink() or not manifest_path.is_file():
        raise ValueError("Snapshot needs a regular manifest")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    files = inventory(manifest)
    check_snapshot(snapshot, manifest, files)
    archive = Path(archive).absolute()
    if not matches(archive, ARCHIVE_SIZE, ARCHIVE_SHA256):
        raise ValueError("Official recovery archive checksum/size mismatch")
    license_file = Path(__file__).with_name("swift-LICENSE.txt")
    if license_file.is_symlink() or not license_file.is_file():
        raise ValueError("The reviewed Swift license must accompany the preparation")
    objects = Path(objects).resolve()
    objects.mkdir(parents=True, exist_ok=True)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    # The archive is read as data only. Never install it or execute its hooks.
    with tempfile.TemporaryDirectory(prefix=".swift-recovery-", dir=objects) as temporary:
        staging = Path(temporary)
        process = subprocess.Popen(["dpkg-deb", "--fsys-tarfile", str(archive)],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            recover_members(process.stdout, files, staging)
            process.stdout.close()
            if process.wait(timeout=30) != 0:
                raise ValueError("dpkg-deb could not read the official recovery archive")
        finally:
            process.stdout.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for name, item in files.items():
            final = objects / item["sha256"]
            candidate = staging / name
            try:
                os.link(candidate, final)
            except FileExistsError:
                if not matches(final, item["bytes"], item["sha256"]):
                    raise ValueError("An existing object cache entry is corrupt; refusing replacement")
    record = {
        "schema": 1, "kind": "darling-swift-inputs", "status": "verified",
        "snapshot": {"manifest_sha256": digest(manifest_path), "archive": manifest["archive"],
                     "root_revision": ROOT_REVISION, "swift_revision": SWIFT_REVISION},
        "archive": {"url": ARCHIVE_URL, "sha256": ARCHIVE_SHA256, "bytes": ARCHIVE_SIZE},
        "license": {"source": "https://github.com/swiftlang/swift/blob/swift-5.2.2-RELEASE/LICENSE.txt",
                    "file": "swift-LICENSE.txt", "sha256": digest(license_file)},
        "files": [dict(item, name=name) for name, item in sorted(files.items())],
        "native_linux_binaries": False, "compiled_from_source": False,
        "preparer_sha256": digest(Path(__file__)),
    }
    shutil.copyfile(license_file, output / "swift-LICENSE.txt")
    (output / "provenance.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print("Verified pinned Swift runtime inputs: " + str(len(files)))
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", help="Complete source-snapshot inventory directory")
    parser.add_argument("--archive", required=True, help="Exact locally downloaded official DEB; never installed")
    parser.add_argument("--objects", required=True, help="SHA256-named binary input cache")
    parser.add_argument("--output", required=True, help="New provenance output directory")
    args = parser.parse_args()
    try:
        prepare(args.snapshot, args.archive, args.objects, args.output)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, tarfile.TarError) as error:
        print("prepare-swift: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
