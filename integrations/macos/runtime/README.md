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

## Ordinary Debian source package

prepare_source.py takes the complete snapshot, verified Swift provenance and a
current native zero-fuzz patch receipt. It rechecks all44 actual materialized
payloads, requires matching checker/policy and ordered patch hashes, captures
packaging files and emits a normal unsigned Debian3.0(quilt) DSC, original
archive, deterministic Debian overlay and two ordinary Builder source recipes.
No Linux source tree is extracted on Windows. Both native extractors prove the
overlay replaces the upstream debian directory, including old maintainer hooks.

```sh
python3 prepare_source.py COMPLETE --swift-provenance SWIFT_PROVENANCE \
  --patch-verification NATIVE_PATCH_REPORT --output out/source-inputs
# Use the generated directory and its recipe with the existing Builder:
karyo-builder source-package out/source-inputs/JOB/trixie.json \
  --image karyo-darling-source-builder:debian --output out/native-debian --jobs 2
# After Debian completes, use resolute.json with the Ubuntu SDK.
```

The source package uses normal debhelper/CMake/Ninja, stock components, the
reviewed revision and native architecture. It retains the one upstream SUID
bootstrap at usr/bin/darling, rejects other privileged or writable payloads,
audits actual Linux ELF dependencies and21 native wrap_elf bridges plus Metal's
Vulkan/LLVM loaders, derives native runtime owners and preserves upstream
notices/build provenance. The compiler/header/dependency results are native to
each base; a Debian binary is never copied into the Ubuntu runtime package.

Twenty source/packaging cases pass as ordinary users on Debian13 and Ubuntu26.04.
They include actual DSC extraction, obsolete-hook removal, strong checksum
metadata, deterministic overlays, stale-proof rejection and privileged/writable
payload guards. The complete dependency/install audit can only pass after a
real engine build; these unit cases do not establish that result. The first
corrected Debian invocation has reached actual Clang/Ninja compilation.
Binary installation, namespace/SUID bootstrap and real CLI/Cocoa/Qt application
acceptance remain required subsequent gates.

## Host ELF and guest Mach-O linker boundary

Patch0023 routes base/configuration-specific CMake linker flags to Linux native
executables, shared libraries and modules, and marks ld64 targets explicitly.
This preserves native distro hardening without feeding ELF-only -z switches to
Apple's linker. Compilation flags and existing guest target options remain in
place. The source policy admits only the exact root CMake and use_ld64 files.

Both native bases pass21 source/packaging cases. The new case executes the
actual added CMake block, builds six small ELF fixtures and checks native
RELRO/BIND_NOW plus exclusion of host flags from marked guest targets. These
marked fixtures test flag routing; they are not Mac application execution.
Real23-patch Git/quilt proofs restore55 original source files on both bases.
The staged runtime auditor additionally requires ELF RELRO on the actual SUID
bootstrap. The next complete engine build must prove real Mach-O linking and
all native dependency/install invariants, followed by bootstrap/application QA.

## Objective-C runtime build modes

The next actual Release compilation exposed objc4's unconditional
OBJC_IS_DEBUG_BUILD=1, contradicting its own NDEBUG-derived DEBUG invariant.
Patch0024 selects the correct constant for Release, RelWithDebInfo and MinSizeRel,
while preserving Debug and unspecified configurations. It does not change the
runtime's client-facing DEBUG interpretation or disable the mismatch guard.
The verifier now checks the exact objc4 module pin as well as the root/Cocotron.

Both native suites pass22 cases, including compilation/execution across all five
standard/default build modes. Both actual zero-fuzz Git/quilt stacks restore
56 files for all24 patches. A fourth real Debian invocation uses this correction;
completed engine packaging and actual runtime/application gates remain pending.

The stock runtime also supplies its upstream declarative binfmt configuration.
It is preserved as stock runtime data; the package build does not load handlers
or run activation commands on the host. Actual installation/registration and
SUID/namespace behavior must be verified in a separate kernel guest, not inferred
from Docker's restricted build environment.

## Foundation assertion format safety

The following real Release build reached Foundation and rejected two assertion
calls that treated NSInternalInconsistencyException as a format string. Common
patch0025 supplies a literal @"%@" format and passes that exception name as data
in both hash/map implementations. Keep format-security enabled. The Foundation
module revision55a4341b470c2a56fea667c1d2167fb074226f04 is verified independently.

Both native source suites pass23 cases. The Objective-C compiler regression
rejects both original calls and accepts their actual patched arguments with
-Werror=format-security. Both complete25-patch Git/zero-fuzz quilt runs restore
all58 selected source files. This does not establish a completed engine build;
full native package auditing and application execution remain subsequent gates.

## Ruby counter headers and reproducible paths

The next full build passed Foundation and reached the bundled Ruby interpreter.
Its debug_counter.h used #include __FILE__ to expand an X-macro list twice;
Debian's normal -ffile-prefix-map remapped that string to a path no longer
resolvable by the preprocessor. Common patch0026 includes the same adjacent
header by its stable filename. Preserve reproducible path mapping and the
interpreter instead of removing either. The exact Ruby module revision
423f2439478d2e24d7c19b2e0ea4a67ee3e80c3d is verified independently.

Both native source suites pass24 cases, including compiler rejection of the
original mapped self-include and successful compilation/execution of the actual
replacement directive with an X-macro header. All26 Git/zero-fuzz quilt patches
apply/reverse on both bases and restore59 pristine selected files. Full engine
packaging and native application execution remain separate unfinished gates.

## Screen messages and complete compiler diagnostics

The sixth real native build passed the Ruby correction and reached more than
17800 Ninja steps before screen forwarded a message as an unchecked format.
Patch0027 passes that message through a literal "%s", retaining format-security
and preserving percent characters as data. The exact screen module revision
4bed52587563ede850bf9ff834567478ac1e616b is checked independently.

Both native source suites pass26 cases; actual27-patch Git/zero-fuzz quilt
proofs restore60 pristine files. The C compiler rejects the original forwarding
call and accepts/executes the corrected percent-containing message. The ordinary
debhelper build rule now asks Ninja to continue independent targets after an
error, so one attempt can report more than the first failure. A real two-target
native debhelper control proves independent linking occurs after the deliberate
compiler error while the packaging command still fails. No failed package or
incomplete stage is accepted; successful builds perform the same target work.
Full native engine packaging and real Mac execution remain required gates.


## Complete Screen diagnostics and static bootstrap unwind

The seventh complete Debian build, with independent failures collected through
Ninja -k0, exposed30 additional Screen format-security calls and one dyld link
failure after26425/26426 steps. Patch0028 passes allocation diagnostics, failed
command names and configured lock text through literal formats. Compiler checks
reject every original call and accept/execute every replacement with percent
characters preserved as data; the normal error argument is unchanged.

The dyld failure identifies a relative switch lookup table emitted by modern
Clang in static libunwind. The pinned ld64 cannot encode that table in its
chained-fixup bootstrap image. Patch0029 sets the documented -fno-jump-tables
option only on unwind_static, keeping optimized compilation, native hardening,
other targets and dyld chained fixups. Its exact libunwind module revision
 a91da1a0e262e04eb601152a84228ff733e48422 is checked independently. A native CMake
control proves target isolation and identical switch results; Mach-O LLVM IR
contains a relative lookup table before the option and no table after it on
both Clang19/Debian and Clang21/Ubuntu. See the upstream
[Clang option reference](https://clang.llvm.org/docs/ClangCommandLineReference.html)
and [LLVM's switch lookup attribute change](https://reviews.llvm.org/D35579).

Both native source suites pass28 cases. All29 patches apply/reverse through
Git and zero-fuzz quilt on both bases, restoring71 pristine source files.
These checks cover source effects and compiler controls, not a completed dyld
link or engine package; the next complete native build must establish those.
Actual Mac application execution remains a separate required gate.


## Preserve native DWARF5 without optional dwz compaction

The eighth complete Debian build successfully compiled the full target graph,
linked dyld and installed its staged tree. Packaging then failed in dwz because
it did not understand Clang's .debug_addr section. The full native dependency
audit had not yet run; compilation alone did not produce an accepted package.

The common packaging override bypasses only the optional DWZ debug-information
compaction. Standard dh_strip still separates debug symbols; native hardening,
optimized compilation, runtime stripping and the staged ELF/dependency audit
remain enabled. This follows the distinct responsibilities documented by
[dh_dwz](https://manpages.debian.org/trixie/debhelper/dh_dwz.1.en.html) and
[dh_strip](https://manpages.debian.org/trixie/debhelper/dh_strip.1.en.html).

A native regression compiles actual Clang DWARF5 and runs a complete small
Debian-format package through the real override and ordinary debhelper chain.
It requires a runnable stripped ELF with a debug link and a separate symbol
package whose ELF still contains .debug_addr. Both Debian .deb and Ubuntu .ddeb
automatic symbol-package formats are accepted. This tiny packaging control is
separate from the next complete Darling build and real Mac runtime acceptance.

Both native source/packaging suites now pass29 cases, including this complete
small-package control. The next full engine package still needs its own audit.


## Separate mixed-format compilation from distribution LTO defaults

The first complete Debian native build now passes compilation, dyld linking,
installation, the4-ELF/21-bridge dependency and hardening audit, and binary/debug
packaging. Its package is installed on the native Debian QA guest. The first
ordinary Mach-O launch still aborts during bootstrap before the probe's main;
neither the source controls nor successful packaging establishes Mac runtime
compatibility. That failure is being diagnosed separately.

The first Ubuntu26.04 full build exposes a different packaging issue: native
dpkg defaults add GCC/ELF LTO flags, including -ffat-lto-objects, to Mach-O
compilation. Clang21 rejects that flag for x86_64-apple-darwin20. The shared
rules now select `hardening=+all optimize=-lto` through DEB_BUILD_MAINT_OPTIONS.
This removes mixed-format LTO without disabling optimization, compiler checks,
native ELF hardening, stripping, symbol separation or the staged audit.

The regression runs the actual rules' make exports through native dpkg-buildflags
and compiles both a genuine Mach-O object and a runnable native ELF. Ubuntu's
unmodified defaults are the paired negative compiler control; the fixed flags
retain stack protection and format-security checks. All30 source/packaging cases
pass on both SDKs. A new complete Ubuntu build must still validate the corrected
package flags against the full target graph and native audit.


## Keep guard seeding outside its own stack-protected frame

Offline symbolication of the actual first Debian bootstrap core identifies
`libSystem_initializer -> __stack_chk_fail`. Its saved stack cookie is zero,
whereas the global guard has been initialized while the same frame is alive.
The initializer invokes libc setup, which seeds that guard; the compiler's
ordinary stack-protector instrumentation therefore compares different epochs.
This is before vchroot main and before the requested Mac application.

Patch0030 marks only libSystem_initializer no_stack_protector, preserving the
constructor and all other function, library and native ELF protection. The
Libsystem module is pinned independently to08df454b6eb0df9400aa4c39839a7efd6efd2c3c.
No program-specific profile, environment workaround or global protection switch
is introduced.

The compiler regression uses the original and patched attributes from the actual
patch. A bootstrap that seeds a global guard fails before main with the original
instrumentation and returns normally with the narrow annotation. In that same
corrected binary an ordinary protected frame still detects a guard change.
Actual Darwin LLVM IR independently confirms protection remains on that ordinary
function and is omitted only from the initializer. All31 native source/packaging
tests pass on Debian and Ubuntu; all30 patches apply and reverse through Git and
zero-fuzz quilt, restoring72 pristine files. This is diagnosis and source/compiler
acceptance. A new full engine package and real Mac runtime must still establish
that this fixes bootstrap on both supported native bases.


## Follow-up: the dyld entry's own initialization epoch

Both native 30-patch engines now build and package successfully. Reinstalling the
Debian runtime preserves existing packages and prefixes, but its actual ordinary
Mach-O launch still aborts before application main. A fresh captured core now
resolves to `dyldbootstrap::start -> __stack_chk_fail -> dyld::halt`; the previous
Libsystem initializer failure is absent. The entry stores zero at FP-0x30, calls
its own __guard_setup and later compares against the seeded dyld guard. The core
and mapped instructions confirm this distinct initialization-epoch mismatch.

Patch0031 marks only that bootstrap entry no_stack_protector. It does not change
dyld's ordinary functions or global compiler settings. The reviewed dyld module
is independently pinned to63f667cf06d7ed59553adebb0c8d70a117135ac9; the only added
source footprint is src/external/dyld/src/dyldInitialization.cpp.

`tests/check_dyld_bootstrap.py` compiles the actual patched entry body with simple
startup callback implementations. The native Debian and Ubuntu SDKs both verify
return to the caller, startup callback order and literal argv/envp/apple vectors.
This positive functional check does not represent a whole engine or real Mac
application pass. Real installed CLI/Cocoa/Qt launches remain required after
rebuilding the 31-patch runtime. Subsequent preparation uses Builder's explicit
source-only mode; the shared unit suite is not part of that run.


## Actual native CLI startup and the first graphical math failure

The full Debian31 engine now executes the real Intel Mach-O loader sample in an
ordinary desktop account. Literal arguments, UTF-8 file I/O, repeat launch and
exit17 propagation work. The first full functional actor stopped at a stale QA
marker reference; after correcting that actor, these CLI cases pass again. This
is actual application execution, not an installed-engine inference.

Cocoa reaches its graphical libraries but exits before producing its report;
the separate Qt attempt does not complete. A captured Cocoa core resolves to
repeated __sincos_stret frames at the bottom of its eight-megabyte virtual stack.
The exact installed libsystem_m instructions call that same helper recursively.
Its float counterpart does the same, and __exp10 jumps to itself while __exp10f
calls that entry. The plain source implements scalar sin/cos and pow calls.
Modern Darwin optimization has folded these library implementations into the
ABI helpers that they implement, rather than preserving the scalar operations.

Patch0032 adds -fno-builtin only to the libm subdirectory's C flags. Release
optimization and flags outside that library stay intact. This preserves the
actual scalar math implementations without an application-specific setting.
The source functions and their public float/double structures remain unchanged.

`tests/check_libm_calls.py` compiles the actual sincos.c and exp10.c with the
patched math-library option. Both native SDKs return the expected values from
all six helpers across30 ordinary calls; actual Darwin LLVM output retains sin,
cos, sinf, cosf and pow calls. The check executes no deliberately broken or
privilege-related case. Early harness attempts exposed quoted-CMake-token and
aggregate-IR parsing errors; corrected v4 logs retain the actual output and pass
on both bases. This is a functional compiler/numerical result. Complete32-patch
native engines and real Cocoa/Qt startup on both kernels are still required.


## Returning a finished Darwin thread to its live Linux stack

The installed Debian32 engine preserves the real Mac CLI behavior and reads
application data written with the previous engine from the same prefix. Cocoa
now passes the former recursive libm fault but still does not produce a valid
application report or captured window. Its new bounded core stops in native
glibc `__longjmp_chk` during a worker thread's ordinary termination. The mldr
return address resolves to its call at file offset0x55a1; the next address is
0x55a6. The source has one matching call in `__darling_thread_terminate`.

`darling_thread_entry` saves a still-live Linux frame with setjmp and explicitly
switches to a separately allocated Darwin stack. Termination must return to that
Linux frame before unmapping the Darwin allocation and finishing the native
pthread. glibc's fortified single-stack ordering assumption does not describe
this intentional transfer between live stack allocations.

Patch0033 binds only that return to the native libc `longjmp` ABI through a
distinct declaration. It changes no other call or compiler setting. The existing
thread cleanup, RPC checkout, TLS jump buffer and saved Linux frame remain intact.
The footprint is one root-pinned file, `src/startup/mldr/elfcalls/threads.c`;
there are no application-specific settings.

`tests/check_mldr_stack_return.py` compiles the actual patched termination body
and declaration, supplies ordinary RPC callback implementations and runs16 real
pthread returns from separate live stacks. Both Debian and Ubuntu compilers pass
the normal flow and unmap the old Darwin allocation only after returning to the
saved Linux stack. The first harness missed stdbool.h and did not compile; v2
preserves that log and passes after adding the standard include. This is a
positive native function-body check, not installed Mach-O or GUI acceptance.

The complete33-patch engine must still be rebuilt and run on both kernels.
Debian32's overall GUI result remains failed even though its launcher returned0:
the required Cocoa JSON and visible frame are absent. The ordinary upstream
uname and echo programs work; defaults writes but its next read fails. That
separate preference issue remains open. No security or denied-operation tests
are part of this follow-up.


## Persistent Foundation domains across processes

The actual packaged defaults utility writes with status0 but its next process
cannot read the value, including the supplied OPENSTEP syntax and output format.
The underlying NSUserDefaults setPersistentDomain implementation clears the suite
then calls registerDefaults, which creates transient registration values rather
than persisted application preferences. Patch0034 sets each supplied value on
that suite before the existing synchronize call. Other domain operations, RPC,
timers and compiler settings are unchanged; this is a general Foundation fix.

The exact proposed method body was compiled into a standalone Mac subclass for
a positive functional check without changing the installed framework. Separate
ordinary Mach-O processes write and read the same UTF-8 preference successfully
on both real kernels (Debian32, Ubuntu31 runtime). Both reports identify macos/
x86_64. This validates the proposed body and persistence path; it does not claim
the installed framework or whole34 runtime passes. The full updated defaults
utility and graphical applications still require installed-package acceptance.
No app-specific policy, security suite or deliberately denied case is included.


## Shared NSWindow restorable property

The actual Ubuntu32 Qt application independently stops with unrecognized selector
-[QNSWindow setRestorable:]. Patch0035 adds the public NSWindow restorable property
and its isRestorable/setRestorable: methods to the shared AppKit class. A zeroed
private state selects the SDK default (titled windows yes, untitled windows no),
while explicit assignments retain their chosen boolean value. The declaration
and default are verified in the existing immutable Mac SDK's
AppKit/NSWindowRestoration.h through the cross compiler, not inferred from Qt.

This exposes the window flag; it does not implement automatic window/session
restoration or claim that subsystem works. There is no Qt-specific override or
per-application configuration. Both files are within the existing pinned Cocotron
footprint. Source-only application/reversal and complete native build plus real
Qt/Cocoa/editor windows remain required before installed API acceptance.
