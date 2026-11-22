#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the reviewed patch stack in a selected-file shadow of pinned source."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile
import tempfile

from patch_policy import DARLING_REVISION, COCOTRON_REVISION, MAX_FILE_BYTES, EXPECTED_PATCH_COUNT, parse_paths
from prepare_swift import digest, matches


def capture_patches(folder):
    record = json.loads((folder / "series.json").read_text(encoding="utf-8"))
    if (set(record) != {"schema", "root_revision", "cocotron_revision", "patches"}
            or record["schema"] != 1 or record["root_revision"] != DARLING_REVISION
            or record["cocotron_revision"] != COCOTRON_REVISION):
        raise ValueError("Unexpected patch source identity")
    expected = record["patches"]
    if not isinstance(expected, list) or len(expected) != EXPECTED_PATCH_COUNT:
        raise ValueError("The reviewed recipe must contain exactly " + str(EXPECTED_PATCH_COUNT) + " patches")
    patches, files, added = [], set(), set()
    for number, item in enumerate(expected, 1):
        if (not isinstance(item, dict) or set(item) != {"file", "sha256"}
                or not isinstance(item["file"], str)
                or not re.fullmatch(f"{number:04d}-[A-Za-z0-9_-]+[.]patch", item["file"])
                or not isinstance(item["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", item["sha256"])):
            raise ValueError("Malformed or unordered reviewed patch entry")
        path = folder / item["file"]
        if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_FILE_BYTES:
            raise ValueError("Expected a bounded regular patch")
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != item["sha256"]:
            raise ValueError("Reviewed patch checksum changed: " + item["file"])
        paths = parse_paths(data)
        patches.append(dict(item, data=data, paths=paths))
        files.update(row["path"] for row in paths)
        added.update(row["path"] for row in paths if row["added"])
    actual = {p.name for p in folder.glob("*.patch")}
    if actual != {p["file"] for p in patches}:
        raise ValueError("Unlisted/missing runtime patches")
    return patches, files, added


def command(shadow, patch, reverse=False, check=False):
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_SYSTEM=os.devnull,
               GIT_CONFIG_GLOBAL=os.devnull, GIT_TERMINAL_PROMPT="0")
    args = ["git", "-c", "core.hooksPath=" + os.devnull, "apply", "--whitespace=nowarn"]
    if reverse:
        args.append("--reverse")
    if check:
        args.append("--check")
    result = subprocess.run(args + ["-"], input=patch["data"], cwd=shadow, env=env,
                            capture_output=True, timeout=60)
    if result.returncode:
        raise ValueError("Patch " + ("reverse" if reverse else "forward") + " failed: "
                         + patch["file"] + ": " + result.stderr.decode("utf-8", "replace")[-3000:])


def verify(snapshot, recipe, output):
    snapshot, recipe = Path(snapshot).resolve(strict=True), Path(recipe).resolve(strict=True)
    output = Path(output).absolute()
    if output.exists() or output.is_symlink() or output.resolve().is_relative_to(snapshot) or output.resolve().is_relative_to(recipe):
        raise ValueError("Choose a fresh report outside source and recipe")
    manifest = json.loads((snapshot / "manifest.json").read_text(encoding="utf-8"))
    if (manifest.get("schema") != 1 or manifest.get("kind") != "source-snapshot"
            or manifest.get("source") != {"url": "https://github.com/darlinghq/darling.git", "revision": DARLING_REVISION}):
        raise ValueError("Pinned Darling source snapshot required")
    cocotron = [m for m in manifest.get("modules", []) if m.get("path", "").rstrip("/") == "src/external/cocotron"]
    if (len(cocotron) != 1 or cocotron[0].get("revision") != COCOTRON_REVISION
            or cocotron[0].get("url") != "https://github.com/darlinghq/darling-cocotron.git"):
        raise ValueError("Pinned Cocotron source snapshot required")
    artifact = manifest.get("archive", {})
    if (artifact.get("file") != "source.tar.gz" or type(artifact.get("bytes")) is not int
            or not isinstance(artifact.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"])):
        raise ValueError("Malformed snapshot archive record")
    archive = snapshot / "source.tar.gz"
    if not matches(archive, artifact["bytes"], artifact["sha256"]):
        raise ValueError("Snapshot archive checksum/size changed")
    patches, files, added = capture_patches(recipe)
    originals = {}
    report = {"schema": 1, "kind": "darling-patch-stack", "status": "pending",
              "source": manifest["source"], "cocotron_revision": COCOTRON_REVISION,
              "snapshot_manifest_sha256": digest(snapshot / "manifest.json"), "archive": artifact,
              "checker_sha256": digest(Path(__file__)), "policy_sha256": digest(Path(__file__).with_name("patch_policy.py")),
              "patches": [{k: v for k, v in p.items() if k != "data"} for p in patches],
              "forward": [], "reverse": [], "files": [], "source_mutated": False,
              "scope": "Patch applicability and reversal only; no compilation or application execution"}
    with tempfile.TemporaryDirectory(prefix="karyo-darling-patch-shadow-") as temporary:
        shadow = Path(temporary)
        with tarfile.open(archive, mode="r|gz") as tar:
            for member in tar:
                if member.name not in files:
                    continue
                if member.name in originals or not member.isfile() or member.size > MAX_FILE_BYTES:
                    raise ValueError("Expected a unique bounded regular patch source file")
                if member.name in added:
                    raise ValueError("Patch-created file already exists in pristine source")
                data = tar.extractfile(member).read()
                originals[member.name] = data
                path = shadow.joinpath(*PurePosixPath(member.name).parts)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        if originals.keys() != files - added:
            raise ValueError("Snapshot lacks a required patch source file")
        for patch in patches:
            command(shadow, patch, check=True)
            command(shadow, patch)
            report["forward"].append(patch["file"])
        final_git_bytes = {name: (shadow / name).read_bytes() if (shadow / name).exists() else None for name in files}
        for patch in reversed(patches):
            command(shadow, patch, reverse=True, check=True)
            command(shadow, patch, reverse=True)
            report["reverse"].append(patch["file"])
        # Check the actual Builder consumer, not merely Git's different diff
        # context rules. GNU patch2.8 rejects truncated trailing context at
        # fuzz=0 even when Git accepts the hunk at the expected location.
        patch_dir = shadow / "debian/patches"
        patch_dir.mkdir(parents=True)
        for patch in patches:
            (patch_dir / patch["file"]).write_bytes(patch["data"])
        (patch_dir / "series").write_text("\n".join(p["file"] for p in patches) + "\n")
        env = {key: value for key, value in os.environ.items() if not key.startswith("QUILT_")}
        env.update(QUILT_PATCHES="debian/patches", QUILT_PATCH_OPTS="--fuzz=0", QUILTRC=os.devnull)
        for action in ("push", "pop"):
            result = subprocess.run(["quilt", action, "-a"], cwd=shadow, env=env,
                                    capture_output=True, text=True, timeout=120)
            if result.returncode:
                raise ValueError("Native zero-fuzz quilt " + action + " failed: " + result.stdout[-4000:] + result.stderr[-2000:])
            if action == "push":
                actual = {name: (shadow / name).read_bytes() if (shadow / name).exists() else None for name in files}
                if actual != final_git_bytes:
                    raise ValueError("Quilt's patched bytes differ from Git's complete result")
        report["quilt_zero_fuzz"] = True
        report["quilt_forward"] = [p["file"] for p in patches]
        report["quilt_reverse"] = list(reversed(report["quilt_forward"]))
        report["gnu_patch_version"] = subprocess.check_output(["patch", "--version"], text=True).splitlines()[0]
        report["quilt_version"] = subprocess.check_output(["quilt", "--version"], text=True).strip()
        for name in sorted(files):
            path = shadow.joinpath(*PurePosixPath(name).parts)
            if path.is_symlink() or path.exists() != (name in originals):
                raise ValueError("Patch reversal changed a source file's identity: " + name)
            data = path.read_bytes() if path.exists() else None
            if data != originals.get(name):
                raise ValueError("Reversed patch bytes differ from pristine snapshot: " + name)
            report["files"].append({"path": name, "sha256": hashlib.sha256(data).hexdigest() if data is not None else None,
                                    "absent": data is None, "matches_pristine": True})
    if not matches(archive, artifact["bytes"], artifact["sha256"]):
        raise ValueError("Source snapshot changed during verification")
    report["status"] = "verified"
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print("Verified " + str(len(patches)) + " patches and pristine reversal of " + str(len(files)) + " selected source files")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot")
    parser.add_argument("--recipe", type=Path, default=Path(__file__).with_name("patches"))
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        verify(args.snapshot, args.recipe, args.output)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, tarfile.TarError) as error:
        print("verify-patches: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
