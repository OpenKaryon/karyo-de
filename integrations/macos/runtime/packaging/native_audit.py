#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit a native staged stock runtime before normal debhelper packaging."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys

PACKAGE = "karyo-darling-runtime"


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def audit_native_bridges(stage, source, build):
    """Mach-O wrap_elf targets dlopen Linux sonames, which shlibdeps cannot see."""
    ld_cache = run(["ldconfig", "-p"], capture_output=True).stdout
    libraries = {}
    for line in ld_cache.splitlines():
        match = re.fullmatch(r"\s*(\S+)\s+\([^)]*\)\s+=>\s+(\S+)", line)
        if match:
            libraries.setdefault(match[1], []).append(Path(match[2]))
    bridges = []
    for relative in ("src/native", "src/CoreAudio"):
        cmake = (source / relative / "CMakeLists.txt").read_text(encoding="utf-8")
        targets = re.findall(r'^\s*wrap_elf\((\w+)\s+([^\s)]+)(?:\s+"([^"]+)")?\)\s*$', cmake, re.MULTILINE)
        for name, _, destination in targets:
            wrapper = stage / "usr/libexec/darling" / (destination or "usr/lib/native") / f"lib{name}.dylib"
            if not wrapper.is_file():
                raise SystemExit(f"Stock native bridge was not installed: {name}")
            generated = build / relative / (name + ".c")
            text = generated.read_text(encoding="utf-8")
            matches = re.findall(r'___elfname: \.asciz \\"([^"\\]+)\\"', text)
            if len(matches) != 1 or not re.fullmatch(r"[A-Za-z0-9_.+-]+", matches[0]):
                raise SystemExit(f"Cannot audit generated bridge soname: {generated}")
            soname = matches[0]
            candidates = [path.resolve() for path in libraries.get(soname, []) if path.is_file()]
            if len(set(candidates)) != 1:
                raise SystemExit(f"Cannot resolve an unambiguous Linux bridge library: {soname}")
            library = candidates[0]
            owners = run(["dpkg-query", "-S", str(library)], capture_output=True).stdout.splitlines()
            packages = []
            for line in owners:
                package, separator, filename = line.partition(": ")
                if separator and filename == str(library) and re.fullmatch(r"[a-z0-9][a-z0-9+.-]*(?::amd64)?", package):
                    packages.append(package)
            if len(set(packages)) != 1:
                raise SystemExit(f"Cannot resolve the Debian owner of {library}")
            metadata = run(["dpkg-query", "-W", "-f=${binary:Package}\t${Version}\t${Architecture}\n", packages[0]], capture_output=True).stdout.strip().split("\t")
            if len(metadata) != 3:
                raise SystemExit(f"Invalid Debian package metadata for {library}")
            package, version, architecture = metadata
            package = package.partition(":")[0]
            if architecture != "amd64" or package.endswith("-dev") or not re.fullmatch(r"[0-9][A-Za-z0-9.+:~_-]*", version):
                raise SystemExit(f"Unexpected native bridge owner: {metadata}")
            bridges.append({"wrapper": str(wrapper.relative_to(stage)), "soname": soname, "linuxLibrary": str(library), "package": package, "minimumVersion": version})
    if len(bridges) != 21:
        raise SystemExit("Pinned stock runtime must contain exactly 21 native GUI/audio bridges")
    return bridges


def audit_metal_dynamic_libraries(stage, source, build):
    """Indium/Iridium dlopen Vulkan/LLVM directly rather than using wrap_elf."""
    indium = source / "src/external/metal/deps/indium"
    vk_source = (indium / "src/indium/dynamic-vk.cpp").read_text(encoding="utf-8")
    vk_names = re.findall(r'_elfcalls->dlopen\("([A-Za-z0-9_.+-]+)"\)', vk_source)
    llvm_source = (indium / "src/iridium/dynamic-llvm.cpp").read_text(encoding="utf-8")
    if vk_names != ["libvulkan.so.1"] or "_elfcalls->dlopen(HOST_LLVM_LIBNAME)" not in llvm_source:
        raise SystemExit("Pinned Metal direct-loader source does not match the audit")
    definitions = (build / "build.ninja").read_text(encoding="utf-8").replace('\\"', '"')
    llvm_names = set(re.findall(r'HOST_LLVM_LIBNAME="([A-Za-z0-9_.+-]+)"', definitions))
    if len(llvm_names) != 1:
        raise SystemExit("Cannot determine the actual compiled Metal LLVM library name")
    llvm_name = next(iter(llvm_names))

    def owner(path):
        lines = run(["dpkg-query", "-S", str(path)], capture_output=True).stdout.splitlines()
        packages = []
        for line in lines:
            package, separator, filename = line.partition(": ")
            if separator and filename == str(path) and re.fullmatch(r"[a-z0-9][a-z0-9+.-]*(?::amd64)?", package):
                packages.append(package)
        if len(set(packages)) != 1:
            raise SystemExit(f"Cannot determine a unique runtime owner of {path}")
        metadata = run(["dpkg-query", "-W", "-f=${binary:Package}\t${Version}\t${Architecture}\n", packages[0]], capture_output=True).stdout.strip().split("\t")
        if len(metadata) != 3:
            raise SystemExit(f"Invalid direct-library Debian metadata for {path}")
        package, version, architecture = metadata
        package = package.partition(":")[0]
        if architecture != "amd64" or package.endswith("-dev") or not re.fullmatch(r"[0-9][A-Za-z0-9.+:~_-]*", version):
            raise SystemExit(f"Direct Metal library is not supplied by an amd64 runtime package: {metadata}")
        return package, version

    result = []
    for wrapper_name, requested_name in (("libindium.dylib", vk_names[0]), ("libiridium.dylib", llvm_name)):
        wrapper = stage / "usr/libexec/darling/usr/lib/darling" / wrapper_name
        if not wrapper.is_file():
            raise SystemExit(f"Enabled Metal runtime wrapper was not installed: {wrapper_name}")
        requested_paths = [Path(folder) / requested_name for folder in
                           ("/usr/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu", "/usr/lib", "/lib")]
        # Resolve the directory, preserving the requested symlink's filename.
        requested_paths = sorted({path.parent.resolve() / path.name for path in requested_paths if path.is_file()})
        libraries = {path.resolve() for path in requested_paths}
        if len(libraries) != 1:
            raise SystemExit(f"Cannot resolve an unambiguous direct Metal library: {requested_name}")
        library = next(iter(libraries))
        with library.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                raise SystemExit(f"Direct Metal library is not Linux ELF: {library}")
        package, version = owner(library)
        for alias in requested_paths:
            if owner(alias) != (package, version):
                raise SystemExit(f"Direct Metal library alias is not supplied by the runtime owner: {alias}")
        dynamic = run(["readelf", "-d", str(library)], capture_output=True).stdout
        sonames = re.findall(r"\(SONAME\).*?\[([A-Za-z0-9_.+-]+)\]", dynamic)
        if len(sonames) != 1:
            raise SystemExit(f"Cannot determine direct Metal ELF SONAME: {library}")
        result.append({"component": "Metal", "wrapper": str(wrapper.relative_to(stage)),
                       "dlopenName": requested_name, "soname": sonames[0],
                       "requestedLibrary": str(requested_paths[0]), "linuxLibrary": str(library),
                       "package": package, "minimumVersion": version})
    return result


def audit(stage, source, build):
    stage, source, build = (Path(p).resolve(strict=True) for p in (stage, source, build))
    if not stage.is_relative_to(source / "debian") or not build.is_relative_to(source):
        raise ValueError("Expected native source-local staging/build directories")
    helper = stage / "usr/bin/darling"
    if helper.is_symlink() or not helper.is_file() or stat.S_IMODE(helper.stat().st_mode) != 0o4755:
        raise ValueError("Darling's one reviewed SUID bootstrap must have mode4755")
    required = ["usr/bin/darlingserver", "usr/libexec/darling/usr/lib/native/libX11.dylib",
                "usr/libexec/darling/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit"]
    required += [f"usr/libexec/darling/System/Library/Frameworks/{name}.framework/Versions/C/Resources/Backends/X11.backend/Contents/MacOS/X11"
                 for name in ("AppKit", "CoreGraphics")]
    for name in required:
        if not (stage / name).is_file():
            raise ValueError("Full stock runtime lacks " + name)
    elf_files = []
    for folder, directories, names in os.walk(stage, followlinks=False):
        for name in names:
            path = Path(folder) / name
            if path.is_symlink():
                continue
            if not path.is_file():
                raise ValueError("Unexpected nonregular payload file: " + str(path))
            mode = stat.S_IMODE(path.stat().st_mode)
            if mode & 0o6000 and path != helper:
                raise ValueError("Unexpected privileged payload: " + str(path.relative_to(stage)))
            if mode & 0o022:
                raise ValueError("Group/world-writable payload: " + str(path.relative_to(stage)))
            with path.open("rb") as stream:
                signature = stream.read(128)
            if signature.startswith(b"version https://git-lfs.github.com/spec/v1"):
                raise ValueError("Unresolved LFS pointer in installed payload")
            if signature.startswith(b"\x7fELF"):
                elf_files.append(path)
    if not elf_files or helper not in elf_files:
        raise ValueError("The privileged bootstrap must be native Linux ELF")
    helper_segments = run(["readelf", "-l", str(helper)], capture_output=True).stdout
    if "GNU_RELRO" not in helper_segments:
        raise ValueError("Native bootstrap lost the distribution's ELF RELRO protection")
    if run(["getcap", "-r", str(stage)], capture_output=True).stdout.strip():
        raise ValueError("Unexpected file capabilities in runtime payload")
    elf_files.sort()
    library_dirs = sorted({str(path.parent) for path in elf_files})
    env = dict(os.environ, LC_ALL="C", LD_LIBRARY_PATH=":".join(library_dirs))
    audit_lines = []
    for path in elf_files:
        result = subprocess.run(["ldd", str(path)], env=env, capture_output=True, text=True)
        text = result.stdout + result.stderr
        if "not found" in text or (result.returncode and "not a dynamic executable" not in text and "statically linked" not in text):
            raise ValueError("Unresolved native ELF dependencies: " + str(path.relative_to(stage)))
        audit_lines.append(str(path.relative_to(stage)) + "\n" + text)
    command = ["dpkg-shlibdeps", "-O", "--ignore-missing-info", "-S" + str(stage)]
    command += ["-l" + name for name in library_dirs] + ["-e" + str(path) for path in elf_files]
    dependencies = run(command, cwd=source, capture_output=True)
    line = next((line for line in dependencies.stdout.splitlines() if line.startswith("shlibs:Depends=")), None)
    if not line or not line.partition("=")[2]:
        raise ValueError("Native shlibdeps returned no dependencies")
    bridges = audit_native_bridges(stage, source, build)
    dynamic = audit_metal_dynamic_libraries(stage, source, build)
    dlopen = sorted({entry["package"] + " (>= " + entry["minimumVersion"] + ")" for entry in bridges + dynamic})
    substvars = source / "debian" / (PACKAGE + ".substvars")
    previous = substvars.read_text() if substvars.exists() else ""
    previous = "\n".join(row for row in previous.splitlines() if not row.startswith(("shlibs:Depends=", "karyo:DlopenDepends=")))
    substvars.write_text((previous + "\n" if previous else "") + line + "\nkaryo:DlopenDepends=" + ", ".join(dlopen) + "\n")
    docs = stage / "usr/share/doc" / PACKAGE
    licenses = docs / "upstream-licenses"
    licenses.mkdir(parents=True, exist_ok=True)
    pattern = re.compile(r"(?:LICENSE|COPYING|COPYRIGHT|NOTICE)(?:[._-].*)?$|APPLE_LICENSE$", re.I)
    notices = []
    for folder, directories, names in os.walk(source, followlinks=False):
        if Path(folder) == source:
            directories[:] = [d for d in directories if d not in ("debian", build.name, ".git", ".pc")]
        else:
            directories[:] = [d for d in directories if d != ".git"]
        for name in names:
            path = Path(folder) / name
            if not pattern.fullmatch(name) or path.is_symlink() or not path.is_file():
                continue
            relative = path.relative_to(source)
            target = licenses / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            notices.append({"file": str(relative), "sha256": digest(path)})
    if not any(entry["file"] == "LICENSE" for entry in notices):
        raise ValueError("Missing upstream root license")
    swift_notice = source / "debian/karyo/swift-LICENSE.txt"
    swift_target = licenses / "src/external/swift/LICENSE.txt"
    swift_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(swift_notice, swift_target)
    notices.append({"file": "src/external/swift/LICENSE.txt", "sha256": digest(swift_notice)})
    (docs / "license-index.json").write_text(json.dumps(sorted(notices, key=lambda entry: entry["file"]), indent=2) + "\n")
    inputs = json.loads((source / "debian/karyo/source-provenance.json").read_text())
    record = dict(inputs, native_distribution=dict(line.split("=", 1) for line in Path("/etc/os-release").read_text().splitlines() if "=" in line),
                  native_elf_dependencies=line.partition("=")[2], native_bridges=bridges, native_dynamic_libraries=dynamic,
                  native_elf=[{"file":str(path.relative_to(stage)),"sha256":digest(path)} for path in elf_files],
                  native_auditor_sha256=digest(Path(__file__)), suid_bootstrap="usr/bin/darling", runtime_tested=False)
    (docs / "build-provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    (docs / "native-elf-audit.log").write_text("\n".join(audit_lines))
    (docs / "native-shlibdeps.log").write_text(dependencies.stdout + dependencies.stderr)
    shutil.copyfile(build / "CMakeCache.txt", docs / "CMakeCache.txt")
    patch_dir = docs / "patches"
    patch_dir.mkdir(exist_ok=True)
    for path in sorted((source / "debian/patches").glob("*.patch")):
        shutil.copyfile(path, patch_dir / path.name)
    print("Native runtime audited: " + str(len(elf_files)) + " ELF files, " + str(len(bridges)) + " bridges")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--build", required=True)
    args = parser.parse_args()
    audit(args.stage, args.source, args.build)


if __name__ == "__main__":
    main()
