# Darling runtime source preparation

This directory owns the reviewed runtime inputs for the optional Karyo macOS
integration. It is build-time preparation, not an installed desktop component.
It does not install Darling, activate binfmt, replace Wine, change global MIME
handlers or demonstrate that a Mac application already runs.

## Recursive source

Use Builder's source-snapshot command with source.json and separate dependency
cache/output directories. Project source continues to build from HEAD; the
external runtime dependency is deliberately pinned to the reviewed commit
60ba801decee7a00782f74f6be4c8ffb013f79ff. Recursive submodules follow exact gitlinks,
not branch names. The recipe explicitly permits the reviewed root link
Developer/usr/bin/git -> /usr/bin/git and libxslt/libxslt/INSTALL pointing to
its original build host Automake documentation. The latter is a preserved
upstream documentation link, not a dependency installed on the developer host. The archive must be materialized only
inside an isolated native build environment, with that policy reviewed first.

```sh
karyo-builder source-snapshot source.json --cache /cache/upstream-git \
  --output /out/darling-inventory --fetch
```

An unresolved LFS inventory is not build-ready source. No source working copy or
Git LFS global configuration is required, and the private prototype stays intact.

## Exact Swift binary inputs

The pinned darling-swift module is
471514f4b4985a604b192e1facac9bbc53dedad3 and contains44 Swift dylib LFS pointers.
prepare_swift.py checks the complete snapshot's archive hash and the actual
pointer blob identities, then recovers only those expected payloads from this
separately downloaded official archive:

- URL: https://github.com/darlinghq/darling/releases/download/v0.1.20230310_update_sources_11_5/darling_0.1.20230310.jammy_amd64.deb
- SHA256: a4405582a17d3e6b50b6751dc0d5d741f8e23848b014d1ad20731b2efbe7a55f
- Bytes:123429806

```sh
python3 prepare_swift.py /out/darling-inventory/karyo-source-snapshot-JOB \
  --archive /cache/official-archive.deb --objects /cache/lfs-objects \
  --output /out/swift-provenance-JOB
karyo-builder source-snapshot source.json --cache /cache/upstream-git \
  --lfs-objects /cache/lfs-objects --output /out/darling-complete
```

The preparer is offline and needs native dpkg-deb. It reads the DEB's filesystem
tar stream as data, selects only expected regular files and verifies every
payload's size/SHA256 against its source pointer. Missing, duplicate, linked,
changed or incomplete members fail. No archive file is installed and no package
hook is executed. Existing SHA256 cache objects are reused unchanged; corrupt
ones fail without replacement. Output includes original Swift license bytes
and provenance joining the source snapshot, exact official archive, pointer
inventory, license source/hash and preparer hash.

These are upstream Mac runtime binary inputs. They are not native Linux ELF
outputs and were not recompiled by this preparer. The remaining native engine,
frameworks, patches, package dependency/privilege audits and actual CLI/Cocoa/Qt
application gates are separate work. A build_ready source snapshot establishes
source completeness only, not successful compilation or runtime compatibility.

The preparation was adapted after reviewing the prototype's own recover-swift
workflow, removing its fixed private checkout paths, source mutation, global LFS
configuration and implicit download behavior. The accompanying swift-LICENSE.txt
is preserved byte-for-byte from its reviewed Swift5.2.2 upstream notice; source:
https://github.com/swiftlang/swift/blob/swift-5.2.2-RELEASE/LICENSE.txt.
Third-party component notices remain separate from the GPL-3.0-or-later license
of this preparation code. Runtime packaging must collect the complete upstream
source and component license set, not replace it with this one notice.

## Checks

Seven focused regression cases pass on ordinary Debian13 and Ubuntu26.04
accounts with read-only inputs, no network, no capabilities and
no-new-privileges. The native case builds a tiny synthetic DEB containing a
failing postinst, then proves data-only reading/cache reuse and corrupt-cache
refusal. Windows passes six cases with the native archive case skipped.
The standalone macOS core CMake/CTest entry passes both checks on both bases,
including35 backend policy cases and these seven preparation cases. The
missing-frontend unit fixture explicitly selects a nonexistent picker so its
result does not depend on whether the optional GUI is installed on the test host.
Unit fixtures substitute tiny inventory/archive constants internally; the public
CLI has no bypass for the44-object inventory or fixed official archive identity.
