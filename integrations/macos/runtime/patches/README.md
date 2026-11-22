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
