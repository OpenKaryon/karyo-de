# Reviewed common Darling changes

These are the prototype's general runtime fixes against Darling60ba801 and
Cocotronc8d38d1. series.json records the reviewed ordered SHA256 identities. Two original
patches needed context-only refresh for GNU patch2.8 at fuzz0; their code
results remain byte-identical to the original Git applications. They do not select an application name or profile.

The stack covers socket path bounds, multiarch native headers, opt-in X11 IPC,
CoreText/fontconfig and CoreGraphics drawing, AppKit color/image ownership,
bitmap representation, window/scroller behavior, input context and layer display.
The original upstream component copyright/license notices remain in the pinned
source snapshot. Native packaging must retain the full source and notice set.

verify_patches.py reads only the selected regular files from the checked snapshot,
applies the complete ordered stack in a private temporary shadow, reverses them and proves
byte-for-byte recovery, including absence of the patch-created private header.
The original source, cache and recipe remain unchanged. Patch applicability is
not compilation, GUI compatibility or permission/bootstrap acceptance.

Patch0022 supplies exact version metadata for a complete source archive lacking
.git. It is an additional common build fix; the original21 patch hashes remain
unchanged. The current22-patch shadow gate restores all53 selected files.

The verifier now also runs the actual quilt push/pop consumer with --fuzz=0,
compares its complete patched bytes with Git's result and checks full pristine
restoration. context-refresh.json records old/new identities and unchanged
before/after source hashes for patches0001 and0006. The other patch bytes are
unchanged. Do not work around malformed context by allowing fuzz.


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
