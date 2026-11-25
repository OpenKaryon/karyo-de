#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Download recipe-locked native sources through signed APT in a disposable image."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", choices=("debian", "ubuntu"), required=True)
    parser.add_argument("--image", required=True, help="Prepared native VMware Tools build image for that base")
    args = parser.parse_args()
    integration = Path(__file__).resolve().parent
    root = integration.parents[2]
    recipe_path = integration / "recipes" / (args.base + ".json")
    spec = json.loads(recipe_path.read_text())
    folder = root / "out/vmware" / args.base / "inputs"
    # Downloads never overwrite an in-use input: verify fresh bytes, then publish only absent files.
    folder.mkdir(parents=True, exist_ok=True)
    records = [spec["source"]["dsc"], *spec["source"]["archives"]]
    for record in records:
        target = (recipe_path.parent / record["path"]).resolve()
        if target.parent != folder.resolve() or target.is_symlink():
            raise ValueError("Recipe source parts must stay within the selected ignored input folder")
    image = json.loads(subprocess.check_output(["docker", "image", "inspect", args.image], text=True))[0]
    if image["Os"] != "linux" or image["Architecture"] != "amd64":
        raise ValueError("A local Linux amd64 image is required")
    with tempfile.TemporaryDirectory(prefix="karyo-apt-source-") as temporary:
        staging = Path(temporary)
        source = spec["source"]["name"] + "=" + spec["source"]["version"]
        # sh receives positional arguments, not interpolated recipe text.
        subprocess.run(["docker", "run", "--rm", "--security-opt=no-new-privileges", "--mount",
            "type=bind,source=" + str(staging) + ",target=/output",
            "--workdir", "/output", "--entrypoint", "sh", image["Id"], "-c",
            'set -eu; apt-get update -qq; apt-get source --download-only --only-source "$1"',
            "karyo-source-fetch", source], check=True)
        for record in records:
            name = Path(record["path"]).name
            candidate = staging / name
            if candidate.is_symlink() or not candidate.is_file() or digest(candidate) != record["sha256"]:
                raise ValueError("Downloaded source does not match the reviewed recipe: " + name)
        for record in records:
            name = Path(record["path"]).name
            target = folder / name
            if target.exists():
                if target.is_symlink() or digest(target) != record["sha256"]:
                    raise ValueError("Existing input differs; preserve and inspect it: " + name)
                continue
            with (staging / name).open("rb") as reader, target.open("xb") as writer:
                shutil.copyfileobj(reader, writer)
    print("Verified native source inputs:", folder)


if __name__ == "__main__":
    main()
