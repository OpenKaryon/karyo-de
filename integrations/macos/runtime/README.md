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

## Reviewed patch stack

patches/series.json captures all21 general fixes against Darling60ba801 and
Cocotronc8d38d1. verify_patches.py checks archive/recipe hashes and a bounded
source-only footprint, copies only52 regular source files into a private shadow,
then applies and reverses the complete stack. Both native bases recover every
original byte and remove the one patch-created private header. Reports match
exactly between bases, and the original source archive remains unchanged.

Six additional policy regressions cover the actual ordered patch hashes,
changed/unlisted patches, source pins, unsafe paths/modes/renames, counted hunks
whose source text resembles headers, and real forward/reverse Git operations
with an unrelated inherited GIT_DIR. The source-input suite now has13 cases;
standalone core CTest passes both entries on both bases (35 backend cases plus
13 source-input cases). Windows passes12 with the native DEB case skipped.

```sh
python3 verify_patches.py /out/darling-inventory/karyo-source-snapshot-JOB \
  --output /out/patch-stack.json
```

The first real source inventory includes161 pinned repositories and292917
tracked files. Its SHA256 is
9991624da4e72530f844e14aafae91e196505a3e98b65f047e9bafdd48379d09
(bytes780871273). Both native preparers recover the same44 exact Swift inputs
(total16128448 bytes), with identical provenance and preserved license hash
770af8291f708538d8ff885a0bbc4e045cd700531741c4f99528d435c14d7f55.
This input verification reads the official archive as data; it executes neither
its Linux runtime nor its Mac libraries. Builder records current local artifacts
and native environment evidence separately for review.

## Source archive version metadata

The original21 runtime patches are preserved unchanged. Patch0022 adds the
explicit DARLING_SOURCE_REVISION configure input for source archives without
.git. It accepts only a complete lowercase40-digit commit, records the exact
revision and source-archive origin, and retains normal Git discovery when the
input is absent. Packaging passes the reviewed root revision. This changes no
application selection or profile configuration.

Both native bases apply/reverse all22 patches and restore53 pristine files.
The source-version CMake test executes the actual added block and rejects
empty, floating, truncated, uppercase and list-like revisions. The joined
source-version gate passes14 input cases on both bases; later source-package
checks add three ordinary DSC/overlay tests (17 total). Runtime compilation and
application execution are separate gates.

## Native quilt consumer

The first real Debian source build found that Git-only applicability was an
incomplete gate: GNU patch2.8 rejected two hand-written hunks at fuzz0 because
they had truncated trailing context. Patches0001/0006 now have complete context;
all resulting source bytes exactly match the original Git applications. The
remaining patch bytes are unchanged. context-refresh.json preserves the old/new
hashes and source-effect proof. The verifier now checks both Git and the actual
quilt push/pop sequence with zero fuzz, compares final patched bytes and restores
all53 pristine files. Native quilt versions can differ while results agree;
per-base receipts retain tool versions separately from shared source effects.
