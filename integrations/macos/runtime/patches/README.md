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


## UTF-8 X11 window titles

Patch0036 publishes _NET_WM_NAME as UTF8_STRING from NSString UTF8String and
maintains WM_NAME through Xutf8TextListToTextProperty, releasing its allocation.
The existing NSWindow creation path already forwards its title. An actual Mac
Foundation diagnostic on Debian34 and Ubuntu32 returns the correct Cyrillic
title through UTF8String but an empty legacy cString. The real Debian Cocoa
window is visible with a blank caption, independently captured and reviewed.

This changes the common X11 backend, with no title replacement or application
rule. Source application/reversal, complete compilation and actual installed
window properties on both bases remain required. It does not claim the separate
TextEditor startup or the wider graphical compatibility matrix is complete.


## Declared legacy document extensions

Patch0037 retains preferred UTI matches in NSDocumentController and falls back
to the existing typeFromFileExtension: lookup of CFBundleTypeExtensions. A failed
UTI lookup error is propagated only when no declared legacy type is found. This
supports applications that declare the older valid document-type keys without
requiring a LaunchServices database or a per-application type rule.

An instrumented upstream editor prototype on Debian34 and Ubuntu35 observes the
installed method return nil for its ordinary .txt fixture and the existing
extension lookup return txt file. With that fallback the Debian frame displays
the actual fixture contents and filename. This prototype is not the unchanged
application and does not accept the installed shared correction. Complete source
and native builds plus unchanged application document runs remain required.
The reviewed pinned footprint gains only AppKit/NSDocumentController.m.


## View backing-store size conversion

After the35 engine resolves the previous NSWindow selector, the actual Ubuntu Qt
application reaches a missing NSView convertSizeToBacking: selector. Patch0038
adds that common method and its inverse using existing view/window transforms.
The current X11 backing store uses one pixel per window coordinate unit, so this
preserves bounds scaling rather than returning every size unchanged. It does not
claim independent high-DPI backing stores or unimplemented point/rect APIs.

Apple's API contract describes local-view to backing-store size conversion:
https://developer.apple.com/documentation/appkit/nsview/converttobacking(_:)-4ra9y?language=objc
The backing coordinates belong to the object, not an absolute screen position:
https://developer.apple.com/library/archive/documentation/GraphicsAnimation/Conceptual/HighResolutionOSX/APIs/APIs.html
Only linear size conversions are added, so parent translation is not included.

The proposed methods run as a separate real Mac NSView subclass on Debian34 and
Ubuntu35. Ordinary and scaled-bounds fractional sizes produce the expected1x/2x
sizes and inverse round trips on both kernels. This accepts that prototype only;
the installed common API and real Qt application still require complete38 builds
and actual native checks. Both changed files are already in the pinned footprint.


## Flipped-view backing-size orientation

Patch0039 corrects the first backing-size implementation: forwarding a signed
window-space size exposes a negative height for a flipped view. The actual Qt
prototype reports widget DPR -1 and a null0x0 image. Use lengths of the local
horizontal/vertical basis vectors for the common backing scale and its inverse,
retaining bounds scaling without importing the window's axis orientation.

Actual Mac subclass prototypes on Debian34 and Ubuntu35 pass ordinary/flipped
views at1x/2x bounds scales with fractional sizes and round trips. The attempted
frame-rotation setter is unimplemented in this backend, so those duplicate cases
do not establish rotation support. A Qt diagnostic with this correction plus
screen metadata visibly draws its Cyrillic label and button on Ubuntu, then
reaches an ImageIO symbol failure during PNG output; full compatibility is open.

Only the existing pinned NSView file changes. Waiting38 controllers were replaced
before starting a complete38 engine; the current Debian35 build is untouched.
Complete source preparation and installed engine checks remain required.


## Common screen identity and backing metadata

Actual unmodified Cocoa screen diagnostics on Debian34 and Ubuntu35 show real
1280x800 frames and backend ID1, but empty deviceDescription, depth0 and no
backingScaleFactor selector. Patch0040 exposes the existing direct display ID
through NSScreenNumber, declares the backing-scale getter and describes the
current8-bit RGB, one-pixel-per-window-unit backing store. The no-RandR screen
also receives ID1, matching the existing CoreGraphics index convention.

The NSNumber screen-ID contract is documented by Apple:
https://developer.apple.com/documentation/appkit/nsscreen/devicedescription
The Qt Cocoa backend relies on this metadata to initialize its screen format;
there is no Qt rule or forced Qt platform setting. Depth and scale describe the
current X11 renderer, not independent high-DPI or HDR capability. Resolution DPI
is not invented. The pinned footprint gains only NSScreen.m and NSScreen.h;
X11Display.m is already included. Source and installed native checks remain open.


## Online screens with real geometry

The actual Ubuntu35 Mac diagnostic enumerates ID1 at1280x800 and ID2 at0x0;
both are incorrectly reported online. Debian35 has only its1280x800 screen.
Patch0041 keeps inactive backend entries out of CGGetOnlineDisplayList and
CGDisplayIsOnline, preserving backend IDs and primary-first order. A bounded
list reports entries actually written; the ordinary zero-capacity count query
still reports the full online count. The backend's complete array is not compacted.

The explicit common-function Qt prototype on Ubuntu now reports the real1280x800
screen instead of0x0 alongside its positive backing scale. This is a normal
display-enumeration scenario, not an invalid-input or security test. The pinned
CoreGraphics implementation is already within the reviewed footprint. Native
installed acceptance and broader multi-output/hotplug behavior remain separate.


## ImageIO type capability lists

After fixing screen metadata and flipped backing sizes, the real Qt prototype
allocates an image and reaches a missing CGImageSourceCopyTypeIdentifiers symbol
during its original PNG save operation. Patch0042 exports that shared ImageIO
function and reports only decoder classes present in the running Onyx2D build.
The existing destination wrapper already forwards its query; implement the
Onyx2D list for TIFF/PNG and JPEG only when LIBJPEG_PRESENT is enabled. Do not
advertise the unimplemented BMP/GIF/JPEG2000 destination branches.

Both Copy functions return owned arrays. A separate real Qt diagnostic containing
these proposed API bodies plus the common screen/backing fixes produces a nonnull
460x194 image, saves PNG, emits passed:true and exits normally on both QA kernels.
This is a diagnostic prototype, not acceptance of the unchanged application with
the installed shared engine. Exact complete-source application, full compilation,
symbol export and normal installed application checks remain required.

The explicit root footprint adds only the pinned ImageIO source and public
header; the Onyx2D implementation is already permitted. No SDK file, Qt source,
application configuration or global library injection is changed.
