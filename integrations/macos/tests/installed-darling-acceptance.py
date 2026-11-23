#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise actual Mac binaries through the installed launcher on a native kernel."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import signal
import stat
import subprocess
import threading
import time


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def command(argv, timeout, environment=None):
    started = time.monotonic()
    process = subprocess.Popen(argv, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               start_new_session=True, text=True, errors="replace")
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            output, _ = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output, _ = process.communicate(timeout=5)
        return {"argv": argv, "returncode": process.returncode, "timed_out": True,
                "elapsed": time.monotonic() - started, "output": output}
    return {"argv": argv, "returncode": process.returncode, "timed_out": False,
            "elapsed": time.monotonic() - started, "output": output}


def json_line(text, probe):
    for line in text.splitlines():
        try:
            value = json.loads(line)
        except ValueError:
            continue
        if isinstance(value, dict) and value.get("probe") == probe:
            return value
    raise ValueError("Missing actual application report: " + probe)



def observe_gui(title, output, kind, stop, observation):
    """Record a normal visible X11 application and its displayed Wayland frame."""
    try:
        environment=dict(os.environ)
        runtime=Path(environment['XDG_RUNTIME_DIR'])
        display=environment.get('WAYLAND_DISPLAY','')
        candidate=runtime/display
        # A fresh prefix can deliberately use another scope name while the
        # real graphical session retains its original Wayland socket.
        if not display or not candidate.exists() or not stat.S_ISSOCK(candidate.lstat().st_mode):
            sockets=[path for path in runtime.glob('wayland-*') if stat.S_ISSOCK(path.lstat().st_mode)]
            if len(sockets)!=1:raise ValueError('No unique graphical display for the actual frame')
            environment['WAYLAND_DISPLAY']=sockets[0].name
        pattern=re.compile(r'^\s*(0x[0-9a-fA-F]+) "'+re.escape(title)+'"',re.MULTILINE)
        while not stop.wait(.1):
            tree=subprocess.check_output(['xwininfo','-root','-tree'],text=True,timeout=2)
            windows=pattern.findall(tree)
            if not windows:
                expected_class={"cocoa":"KaryoCocoaProbe","qt-widgets":"KaryoQtProbe","texteditor":"TextEditor"}.get(kind)
                candidates=[]
                if expected_class:
                    for candidate in re.findall(r'^\s*(0x[0-9a-fA-F]+) \(has no name\)',tree,re.MULTILINE):
                        properties=subprocess.check_output(['xprop','-id',candidate,'WM_CLASS'],text=True,timeout=2)
                        if re.findall(r'"([^"]*)"',properties)!=[expected_class,expected_class]:continue
                        details=subprocess.check_output(['xwininfo','-id',candidate],text=True,timeout=2)
                        if 'Map State: IsViewable' in details:candidates.append(candidate)
                if len(candidates)==1:
                    windows=candidates
                    observation.update(title_missing=True,matched_wm_class=expected_class)
            if len(windows)!=1:continue
            window=windows[0]
            info=subprocess.check_output(['xwininfo','-id',window],text=True,timeout=2)
            if 'Map State: IsViewable' not in info:continue
            def number(label):
                match=re.search(r'^\s*'+re.escape(label)+r':\s*(-?\d+)\s*$',info,re.MULTILINE)
                if match is None:raise ValueError('Missing actual window geometry: '+label)
                return int(match.group(1))
            x,y,width,height=(number(label) for label in ('Absolute upper-left X','Absolute upper-left Y','Width','Height'))
            if x<0 or y<0 or width<=0 or height<=0:raise ValueError('Application frame is outside the native output')
            if stop.wait(.6):return
            png=output/(kind+'-visible.png')
            subprocess.run(['grim',str(png)],
                           env=environment,capture_output=True,check=True,timeout=3)
            header=png.read_bytes()[:24]
            frame_width,frame_height=int.from_bytes(header[16:20],'big'),int.from_bytes(header[20:24],'big')
            if header[:8]!=b'\x89PNG\r\n\x1a\n' or frame_width<=0 or frame_height<=0:
                raise ValueError('Missing actual whole-desktop PNG dimensions')
            observation.update(captured=True,title=title,expected_title=title,observed_title=None if observation.get('title_missing') else title,title_verified=not observation.get('title_missing',False),window=window,geometry=[x,y,width,height],
                               png=str(png),sha256=digest(png),capture_scope='whole-desktop',frame_dimensions=[frame_width,frame_height],visual_review_pending=True)
            return
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        observation.update(captured=False,error=str(error))


def check(args, output, result):
    uid = os.getuid()
    if uid == 0 or os.getresuid() != (uid, uid, uid) or os.getresgid() != (os.getgid(),) * 3:
        raise ValueError("Use an ordinary desktop account without sudo")
    if Path("/.dockerenv").exists() or Path("/proc/1/comm").read_text().strip() != "systemd":
        raise ValueError("A normal Linux kernel guest is required; container fixtures do not qualify")
    account = pwd.getpwuid(uid)
    home = Path(account.pw_dir).resolve(strict=True)
    fields = dict(line.split(":", 1) for line in Path("/proc/self/status").read_text().splitlines() if ":" in line)
    if fields["NoNewPrivs"].strip() != "0" or any(int(fields[key], 16) for key in ("CapInh", "CapPrm", "CapEff", "CapAmb")):
        raise ValueError("The test parent needs ordinary zero-capability IDs and NoNewPrivs0")
    if not os.environ.get("DISPLAY"):
        raise ValueError("Run in the ordinary graphical session with its XWayland DISPLAY")
    samples = Path(args.samples).resolve(strict=True)
    manifest = json.loads(Path(args.manifest).read_text())
    if (manifest.get("kind") != "real-macos-probes" or manifest.get("target") != "macos-x86_64"
            or manifest.get("compiled") is not True or manifest.get("source_dirty") is not False):
        raise ValueError("Clean-source real Intel Mach-O probe build receipt required")
    for member in manifest["members"]:
        path = samples / member["path"]
        if not path.resolve(strict=True).is_relative_to(samples):
            raise ValueError("Escaping installed sample path")
        if "sha256" in member:
            info = path.lstat()
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != uid
                    or info.st_size != member["bytes"] or digest(path) != member["sha256"]):
                raise ValueError("Changed or foreign sample file: " + member["path"])
        elif "symlink" in member and (not path.is_symlink() or os.readlink(path) != member["symlink"]):
            raise ValueError("Changed sample bundle link")
    helper = Path("/usr/bin/darling")
    info = helper.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or stat.S_IMODE(info.st_mode) != 0o4755:
        raise ValueError("Expected the audited root-owned native Darling SUID bootstrap")
    with helper.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            raise ValueError("Darling bootstrap must be native ELF")
    package = subprocess.check_output(["dpkg-query", "-W", "-f=${Status}\t${Version}", "karyo-darling-runtime"], text=True)
    if not package.startswith("install ok installed\t"):
        raise ValueError("The built native runtime package is not installed")
    result.update(uid=uid, kernel=os.uname().release, runtime_package=package.partition("\t")[2],
                  runtime_helper_sha256=digest(helper), probe_manifest_sha256=digest(Path(args.manifest)),
                  probe_source_revision=manifest["source_revision"], toolchain_image=manifest["toolchain_image"])
    launcher = "/usr/bin/karyo-macos"
    loader = samples / "karyo-macos-loader-probe"
    runs = result["runs"] = []
    def run(label, argv, code=0, environment=None, timeout=None):
        record = command(argv, timeout or args.timeout, environment)
        record["label"] = label
        runs.append(record)
        (output / (label + ".log")).write_text(record["output"], encoding="utf-8")
        if record["timed_out"] or record["returncode"] != code:
            raise ValueError("Actual native run failed: " + label)
        return record
    status = json.loads(run("status", [launcher, "--status", "--json"])["output"])
    if not status.get("installed") or status.get("version") != result["runtime_package"]:
        raise ValueError("Launcher did not identify the installed runtime")
    preview = json.loads(run("inspect", [launcher, "--inspect", str(loader)])["output"])
    prefix = Path(preview["prefix"])
    if not prefix.is_absolute() or not prefix.is_relative_to(home / ".karyo-macos") or preview["ownerUid"] != uid:
        raise ValueError("The runtime prefix is not scoped to the current account")
    arguments = ["Привет, literal spaces", "quotes '\" and backslash \\", "one\ntwo", "ordinary startup argument"]
    if args.ordinary_arguments_only:
        arguments = ["Привет", "ordinary document.txt", "project", "startup"]
    result["argument_profile"] = "ordinary" if args.ordinary_arguments_only else "literal-transport"
    result["runtime_attempted"] = True
    loader_run = run("loader", [launcher, str(loader), "--", *arguments])
    report = json_line(loader_run["output"], "macho-loader")
    if (report.get("passed") is not True or report.get("target") != "macos"
            or report.get("architecture") != "x86_64" or report.get("argv") != arguments):
        raise ValueError("Actual Mach-O startup, file I/O or literal argv failed")
    result["runtime_tested"] = True
    if not prefix.is_dir() or prefix.lstat().st_uid != uid:
        raise ValueError("Real Darling did not create an account-owned prefix")
    marker = prefix / ("karyo-qa-preserved-" + output.name)
    with marker.open("x") as stream:
        stream.write("Пример preserved runtime data\n")
    saved, inode = marker.read_bytes(), marker.stat().st_ino
    exit_run = run("exit17", [launcher, str(loader), "--", "--exit17"], 17)
    if json_line(exit_run["output"], "macho-loader").get("passed") is not True:
        raise ValueError("The nonzero-exit test failed before reaching Mac main")
    if marker.read_bytes() != saved or marker.stat().st_ino != inode:
        raise ValueError("Repeat launch changed existing prefix data")
    gui_errors = []
    result["gui_reports"] = []
    for kind, bundle in (("cocoa", "KaryoCocoaProbe.app"), ("qt-widgets", "KaryoQtProbe.app")):
        try:
            path = output / (kind + ".json")
            observation={"kind":kind,"captured":False}
            result.setdefault("gui_observations",[]).append(observation)
            stop=threading.Event()
            title="Karyo Cocoa — Привет" if kind=="cocoa" else "Karyo Qt — Привет"
            watcher=threading.Thread(target=observe_gui,args=(title,output,kind,stop,observation))
            watcher.start()
            try:
                run(kind, [launcher, str(samples / bundle), "--", "/Volumes/SystemRoot" + str(path)])
            finally:
                stop.set();watcher.join(5)
            if watcher.is_alive():raise ValueError("Displayed-frame observer did not finish")
            report = json.loads(path.read_text())
            if (report.get("probe") != kind or report.get("target") != "macos"
                    or report.get("passed") is not True or report.get("windowVisible") is not True
                    or report.get("buttonSignal") is not True):
                raise ValueError("Actual GUI self-report failed: " + kind)
            if kind == "qt-widgets":
                image = Path(str(path) + ".png")
                if image.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n" or report.get("widgetRasterRender") is not True:
                    raise ValueError("Actual Qt widget raster output is missing")
            result["gui_reports"].append(report)
        except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
            gui_errors.append({"kind": kind, "error": str(error)})
    result["gui_errors"] = gui_errors
    result["gui_frames_captured"] = all(item.get("captured") is True for item in result["gui_observations"])
    if gui_errors:
        raise ValueError("Actual GUI checks failed: " + "; ".join(item["kind"] + ": " + item["error"] for item in gui_errors))
    result.update(passed=True, runtime_tested=True, prefix=str(prefix), prefix_data_preserved=True,
                  gui_frames_captured=all(item.get("captured") is True for item in result["gui_observations"]),
                  application_reports=["macho-loader", "cocoa", "qt-widgets"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--timeout", type=int, default=180, choices=range(10, 301))
    parser.add_argument("--ordinary-arguments-only", action="store_true",
                        help="Use ordinary words, UTF-8 and a spaced filename for the startup check")
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    result = {"schema": 1, "kind": "actual-darling-applications", "passed": False,
              "runtime_tested": False, "complete": False, "visible_window_evidence": False,
              "limitation": "Separate visible-window and multi-account/upgrade evidence is still required"}
    try:
        check(args, output, result)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
    (output / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": result["passed"], "complete": False, "result": str(output / "result.json")}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
