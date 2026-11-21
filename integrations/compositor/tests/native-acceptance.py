#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run a packaged compositor and real protocol clients as an ordinary user."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time


def run(args, **kwargs):
    print("+", shlex.join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True, help="Published Builder source-package result")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--display", help="Use an already running disposable VM compositor")
    args = parser.parse_args()
    if os.getuid() == 0:
        raise ValueError("Protocol acceptance must run without root")
    args.output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[3]
    manifest = json.loads((args.package / "manifest.json").read_text())
    for item in manifest["artifacts"]:
        path = args.package / item["file"]
        if path.parent != args.package or path.is_symlink():
            raise ValueError("Unsafe artifact path")
        with path.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != item["sha256"]:
                raise ValueError("Changed native package artifact")
    with tempfile.TemporaryDirectory(prefix="karyo-compositor-qa-") as tmp:
        work = Path(tmp)
        run(["dpkg-deb", "-x", args.package / manifest["binaries"]["labwc"], work / "installed"])
        protocols = {
            "layout": source / "shell/protocols/openkaryon-keyboard-layout-v1.xml",
            "snapshot": source / "apps/snip/protocols/openkaryon-window-snapshot-v1.xml",
            "xdg-shell": Path("/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"),
            "security": Path("/usr/share/wayland-protocols/staging/security-context/security-context-v1.xml"),
            "lock": Path("/usr/share/wayland-protocols/staging/ext-session-lock/ext-session-lock-v1.xml"),
        }
        for name, path in protocols.items():
            run(["wayland-scanner", "client-header", path, work / (name + ".h")])
            run(["wayland-scanner", "private-code", path, work / (name + ".c")])
        flags = subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "wayland-client", "xkbcommon"], text=True)
        probe = work / "probe"
        run(["cc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror",
             "-I", work, Path(__file__).with_name("protocol-probe.c"),
             *[work / (name + ".c") for name in protocols], *shlex.split(flags), "-o", probe])
        shutil.copyfile(probe, args.output / "protocol-probe")
        (args.output / "protocol-probe").chmod(0o755)
        environment = dict(os.environ)
        server = None
        with (args.output / "compositor.log").open("wb") as server_log:
            try:
                if args.display:
                    environment["WAYLAND_DISPLAY"] = args.display
                else:
                    runtime, config = work / "runtime", work / "config"
                    runtime.mkdir(mode=0o700)
                    config.mkdir()
                    (config / "rc.xml").write_text(
                        "<labwc_config><core><decoration>server</decoration></core></labwc_config>")
                    environment.update(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY="wayland-0",
                        WLR_BACKENDS="headless", WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="pixman",
                        XKB_DEFAULT_LAYOUT="us,ru", HOME=str(work), XDG_CONFIG_HOME=str(work / "xdg"))
                    environment.pop("DISPLAY", None)
                    environment.pop("WAYLAND_SOCKET", None)
                    server = subprocess.Popen(
                        [str(work / "installed/usr/bin/labwc"), "--config-dir", str(config)],
                        env=environment, cwd=work, stdout=server_log, stderr=subprocess.STDOUT)
                    for _ in range(100):
                        if (runtime / "wayland-0").exists():
                            break
                        if server.poll() is not None:
                            raise RuntimeError("Packaged compositor exited before opening its socket")
                        time.sleep(0.05)
                    else:
                        raise RuntimeError("Packaged compositor did not open its socket")
                with (args.output / "probe.log").open("wb") as log:
                    run([probe, *(["--keyboard"] if args.display else [])], env=environment,
                        cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=35)
            finally:
                if server and server.poll() is None:
                    server.terminate()
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait()
        (args.output / "acceptance.json").write_text(json.dumps({
            "uid": os.getuid(), "version": manifest["version"],
            "image_id": manifest["environment"]["image_id"],
            "mode": "physical keyboard VM" if args.display else "headless no physical keyboard",
            "checks": "registry/windows/stacking/minimize/security-context/session-lock passed"
        }, indent=2) + "\n")
    print("PASS compositor native acceptance:", args.output)


if __name__ == "__main__":
    main()
