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


## Installed CoreText font families

The actual Qt startup calls CTFontManagerCopyAvailableFontFamilyNames, which is
still a stub although the existing font-collection and descriptor backend is
implemented. Patch0043 returns unique, nonempty actual family names from that
backend in an owned CFArray, without a new AppKit or native-library dependency.

The explicit Qt diagnostic prototype reports200 available families on Debian35
and198 on Ubuntu35, resolves Noto Sans and retains passing raster/PNG output on
both. It does not force an application font. Its Ubuntu widget PNG is byte-for-
byte identical to the previous prototype, so enumeration does not fix the
observed glyph/spacing defects. Visual font quality remains a separate open
issue; neither the family count nor a passing raster report proves that quality.
Source and installed API checks are still required. Only the already permitted
CoreText directory gains the touched CTFontManager file in the pinned stack.


## Windowed run-loop deadline follow-up

Ordinary native diagnostics reproduce a50ms Cocoa event wait taking29-30seconds
on both bases. Instrumentation places the delay inside NSRunLoop's CF wait,
not X11 event processing. A plain nonwindowed CF timeout works; a direct CF
timeout after window creation reproduces the delay. An executable-only deadline
timer prototype makes event waits return in40-51ms on Debian35 and Ubuntu43.
That prototype is diagnostic evidence, not an installed shared-library fix.

Patch0044 bounds Darling's Mach receive by the run loop's existing finite
deadline rather than relying solely on a dispatch-source wakeup. It retains
the existing polling path, reserves the infinity sentinel, rounds finite waits
up to milliseconds and caps long waits without overflow. Existing callbacks,
source delivery and dispatch timeout cleanup remain unchanged. Non-Darling
behavior remains the upstream infinite-receive behavior. No application rule,
global library injection or per-application configuration is added.

Only the pinned CoreFoundation CFRunLoop.c file is added to the footprint;
the verifier checks the exact module revision a3640a77410cf1825f7855172b1551ae7917c461.
Source application/reversal, complete native builds and installed event-deadline
and unchanged-application acceptance remain required. Direct windowed CF timing
is still an open installed-runtime gate until those checks pass.


## CoreText current text origin

The installed43 Qt raster has visibly clipped/jittered glyphs although Cocoa
text and document editing render normally. The Qt Cocoa font engine sets a
current CGContext text position before asking CoreText to draw a glyph at a
relative zero position. The existing CTFontDrawGlyphs implementation replaced
the entire text matrix with the font matrix, discarding that origin.

Patch0045 carries the current text origin into the installed font matrix before
the existing positioned-glyph renderer runs. It remains one common CoreText
implementation, with no Qt/font/application setting or new source footprint.
Font/font-size state and the existing matrix-state behavior are retained.

An executable-only proposed function on Ubuntu43 gives a clean, reviewed Qt
label and button, and a normal exit with real PNG output. The PNG changes from
a043c436661c41667bf846c902634e4fe52841306c83a161ca8f87ad207b4325 to
d90cf2c330b6cb14b737a913f15a4e4430fe7607b7c79394ba77041b4d3347d6.
This is a diagnostic prototype, not installed shared-library acceptance.
Debian43 prototype, complete source and installed45 checks remain separate
required gates, including transformed/repeated drawing behavior before broader
graphics compatibility is claimed.

Primary client source consulted (no Qt code is copied into the patch):
https://raw.githubusercontent.com/qt/qtbase/5.15/src/platformsupport/fontdatabases/mac/qfontengine_coretext.mm
The public CoreText reference documents font/size/matrix state changes:
https://developer.apple.com/documentation/coretext/ctfontdrawglyphs(_:_:_:_:_:)
Full modern CoreText positioning semantics are not inferred from one client.


## Shared system-color lifetime during dynamic Qt initialization

An unchanged UGENE53.1 Intel Mac bundle fails during initialization of its
bundled Qt5.15.10 platform plugin. With its own crash handler temporarily
disabled for diagnosis, an NSException identifies an O2Color receiver where
an NSColor is required. This diagnostic option is not a product setting.

A separate minimal client of the identical original Qt frameworks reproduces
the failure. A local method tracer shows valid system catalog colors during
initial palette construction, then a stale controlTextColor backing object
whose storage has been reused by a CFString. Its stack leads through
qt_mac_toQColor and QApplicationPrivate::initializeWidgetPalettesFromTheme.
Ordinary palette access and even before-main initialization pass separately;
those narrower checks do not cover plugin autorelease-pool cleanup.

Patch0046 retains shared System catalog proxies in a synchronized class cache.
The custom-catalog path and explicit fallback-color factory remain unchanged.
The existing proxy owns its concrete backing color; no raw CGColor is treated
as an NSColor. There is no Qt fork, UGENE name check or per-application policy.
General dynamic appearance/color-management support is not inferred from this
lifetime fix; the current X11 backend still supplies its existing system colors.

An executable-only proposed factory cache lets that real dynamic Qt5.15.10
client construct QApplication and QLabel and exit0 on both installed45 bases.
This is prototype evidence, not installed46 acceptance or UGENE GUI acceptance.
Both source checks, complete native46 builds and the original unchanged external
application's displayed UI remain required.
Shared policy/security suites are not executed as part of this work.

Primary client implementation consulted; no Qt code is copied into this patch:
https://raw.githubusercontent.com/qt/qtbase/5.15/src/plugins/platforms/cocoa/qcocoatheme.mm
https://raw.githubusercontent.com/qt/qtbase/5.15/src/gui/painting/qcoregraphics.mm


## Optional per-window preferences

After the proposed system-color fix, the original dynamic Qt5.15.10 client asks
NSWindow to disallow automatic tabbing and retain the ordinary opaque titlebar.
The backend already has the class-wide automatic-tabbing flag, defaulting off,
but lacks the instance selectors. Patch0047 adds the public tabbing-mode enum
and independent per-window tabbingMode/titlebarAppearsTransparent accessors.
Zero-initialized windows default to Automatic and an opaque titlebar.

These fields record requested preferences. They do not implement native tab
groups or transparent compositor-owned frames. The observed opaque request
matches current backend presentation; nondefault visual effects remain open.
A temporary associated-object prototype passes defaults, two-window independence
and reset on both native45 systems. This is not installed shared-library proof.
The unchanged UGENE proceeds past these selectors and next asks NSMenuItem to
clear its optional custom view. No application name or policy exception is used.
The exact previous stack plus this two-file patch applies and reverses locally;
complete native source-stack and installed checks remain required.

API contract consulted:
https://developer.apple.com/documentation/appkit/nswindow/tabbingmode-swift.property
https://raw.githubusercontent.com/qt/qtbase/5.15/src/plugins/platforms/cocoa/qcocoawindow.mm


## Optional menu-item view ownership

Qt's ordinary menu initialization clears NSMenuItem.view with nil; the original
UGENE currently fails because the selector is absent. Patch0048 adds the public
property, default nil storage, retain-before-release assignment, copy ownership
and deallocation. This fixes clearing and lifetime bookkeeping, not custom view
layout/event delivery or keyed archiving. Those broader presentation/serialization
capabilities remain explicit follow-up work; nil requests keep standard menus.

A temporary subclass implementing the same ownership rules passes default nil,
caller-release retention, self-assignment, copied ownership, clearing and exact
final release on both native45 bases. The unchanged UGENE's common diagnostic
prototype records only nil view assignments and reaches startup plugin loading,
then still exits1 without a captured window. Neither observation is full original
application or installed48 acceptance. No product application-name check exists.

Only the pinned menu-item implementation and public header extend the exact
source footprint. Both files apply and reverse against their pristine module
objects locally; full48 Git/zero-fuzz quilt and native installed checks remain.
The shared policy/security unit suite is not executed by these checks.


## Optional color-space representation query

The original Qt Cocoa plugin bundled with the official UGENE53.1 application
reaches CGColorSpaceCopyPropertyList when tagging an IOSurface graphics buffer.
The missing export currently aborts in dyld before the client can handle an
unavailable representation. Patch0049 adds the public nullable query and returns
no representation because Onyx2D currently retains no encoded ICC/property-list
profile. Device component counts are not substituted for calibrated metadata.

This supplies an honest optional-API boundary, not color-space serialization,
ColorSync fidelity or acceptance of UGENE rendering. The temporary helper did
not resolve the two-level CoreGraphics bind; the actual export must be provided
by the framework. An ordinary-file diagnostic and a temporary SIGABRT trace
identify the original binding failure. No helper or application-specific
environment setting is installed into the product launcher.

Only CoreGraphics/CGColorSpace.m and its public header extend the exact pinned
footprint. Git and zero-fuzz quilt application/reversal of the complete49 series
pass on both pinned SDKs, restoring88 pristine files. Installed-runtime checks
remain separate. Shared/security policy suites are not executed.

API contract: https://developer.apple.com/documentation/coregraphics/cgcolorspace/copypropertylist()
Original caller: https://github.com/qt/qtbase/blob/5.15/src/plugins/platforms/cocoa/qiosurfacegraphicsbuffer.mm


## Selecting each ordinary IOKit request

Patch0050 corrects the IOSurfaceRoot external-method dispatcher. Its upstream
function initialized a static method from `methods[index]` once, so a normal
sequence such as create, lock, lookup and release could keep the first selector.
The method record is now refreshed on every request, with storage local to the
current dispatch thread. The returned object is set for the current call.

The pinned IOKit daemon revision is
`5549ac4cd2db923c256f016a1381b3bfb716730a`. Only its
`src/IOSurfaceRoot.mm` extends the source footprint. The verifier also checks
that exact module identity; existing patch identities remain unchanged.

`tests/check_iokit_dispatch.py` compiles the actual dispatcher body and actual
selector enumeration with stand-in ordinary request handlers, then invokes five
different requests across two service objects. Both native compilers pass; the same request sequence fails on the original
dispatcher. The changed component also passes syntax checking with Clang19 and
Clang21 using the exact generated Debian engine header closure and compiler
arguments, in separate temporary copies. Each compiler uses its native resource
headers; the running49 build source is unchanged. This is not full IOKit IPC or
IOSurface allocation acceptance. The installed49 engine does not contain0050.

Allocation storage, mapping, layout, lock/unlock and layer presentation still
need their own implementations and actual public-API evidence. A correctly
selected method alone does not make the existing incomplete handlers work.


## Coherent CPU IOSurface buffers (runtime acceptance pending)

Patch0051 replaces the packed-buffer allocation stubs with real IOKit-owned
CPU storage. The service creates and maps a temporary backing file; each
framework handle maps the same file at its own process address. The private
versioned response carries layout, storage identity and allocation size, not a
foreign pointer to dereference. Caller references and normal process exit keep
the storage alive until the last handle is released. No display or EGL context
is required to allocate these CPU pixels.

The common layout functions supply cache-line row alignment and page-aligned
allocation sizes. Create, ID lookup, map/unmap, lock/unlock seeds and public
sizes/IDs now have concrete implementations. Per-process usage counts remain
distinct from the total shared in-use state. Every caller increment/decrement is
balanced, including normal release and caller exit. Lock/unlock provide CPU
visibility and modification seeds; they do not claim an exclusive mutex or GPU
synchronization. The protocol requires a matching service and framework; old
prefix daemon upgrade behavior is not yet accepted.

Five actual compilation units pass Clang19 and Clang21 syntax checks against
the exact generated engine header closure. That proof is compilation only.
The unchanged public pixel-buffer client has not run against an installed51
engine, so pixel allocation, cross-process lookup and lifecycle are not yet
accepted. No successful UGENE window or document workflow is claimed.

This implements packed CPU storage, not planar buffers, GPU import, Mach-port
transfer or cross-process attachments. Attachments currently belong to each
framework handle; unsupported purgeability returns an error. Device-limit
queries and layer presentation still need implementation and ordinary runtime
evidence. In particular, the existing renderer must recognize IOSurface pixels
rather than treating every layer content object as a CGImage.

Primary contracts: Apple documents [process usage counts](https://developer.apple.com/documentation/iosurface/iosurfacegetusecount(_:))
and [CPU access boundaries and modification seeds](https://developer.apple.com/documentation/iosurface/iosurfacelock(_:_:_:)).


## Presenting packed IOSurface layer contents (runtime acceptance pending)

Patch0052 gives the ordinary QuartzCore renderer distinct CGImage and IOSurface
upload paths. Packed BGRA/RGBA pixels use their actual row stride and a read-only
CPU lock. The renderer restores unpack row/alignment/skip state, reads the
modification seed, and caches an upload only after a successful transfer. It
reuses unchanged textures and refreshes changed contents or seeds. Actual GL
texture limits are queried from the current context rather than invented.

CALayer retains and releases contents, supplies the normal contentsScale default,
and invalidates its upload cache on content/context changes. Layer contexts own
the deferred texture deletion queue and detach the old layer tree before release.
The renderer balances its retained root layer; replacing or closing a view can
therefore release its CPU buffers instead of leaving them retained indefinitely.
QuartzCore links its IOSurface dependency explicitly. No application/profile or
original Qt plugin is selected or rewritten.

The three actual changed compilation units pass Clang19 and Clang21 against the
recorded engine header closure. These are compiler checks, not a visible frame
or GPU/layer acceptance. The six-file source footprint is explicit; common52
source application and actual installed buffer/layer tests are still pending.
Planar formats, other packed formats, GPU import, arbitrary layer transforms and
cross-process attachments are not established by this CPU upload path.


## Single-service IOKit matching (installed acceptance pending)

On installed Ubuntu52 an ordinary public client finds IOSurfaceRoot through
IOServiceGetMatchingServices/IOIteratorNext, but the direct query for the same
name returns no service. Its source RPC was still an unsupported stub. Buffer
layout now succeeds, but creation stops at this missing lookup. No layer or
UGENE GUI success follows from the registered service alone.

Patch0053 implements the single-result binary RPC using the same registry iterator
as enumeration, returns the actual first object's port and schedules release of
the temporary iterator. Deserialized dictionaries and errors are balanced. The
IOSurface initializer also stops releasing the input dictionary a second time:
the public IOKit call already consumes it. No application settings are selected.

All three changed units compile with Clang19 and21 against real generated MIG
headers and the recorded engine closure. The first exporter attempted to read
a header not yet generated by the ongoing build; a separate temporary directory
then generated it with the exact upstream build-mig command. No headers or sources
inside the active52 build were replaced. Full53/source and actual installed
single-match, CPU buffer, lifecycle and displayed pixels remain separate gates.
The retained ordinary matching client exercises two valid public queries, not
policy denial or a security suite.


## Existing CPU-surface temporary directory (installed54 acceptance pending)

Installed Ubuntu53 now passes both ordinary public IOSurfaceRoot matching queries.
Its buffer client then fails at allocation: fresh prefixes initialize /private/tmp,
while the hard-coded /private/var/tmp directory does not exist. An ordinary own
QA client that creates the missing directory from inside its prefix successfully
creates, maps, locks, fills and unlocks the same17x9 BGRA buffer. A separate valid
IOKit request also returns its192-byte description and maps the8192-byte backing
file. These temporary diagnostic clients do not modify the runtime or UGENE.
Creating a directory from the host side of an existing prefix did not make the
original client pass and is retained as an unsuccessful diagnostic.

Patch0054 changes the common backing-file template to /private/tmp, which is
already initialized in a fresh Darling prefix. The private response still names
the exact file used by both daemon and client; its protocol and lifecycle are
unchanged. The affected Objective-C++ unit compiles on Clang19 and21. Actual
installed54 buffer/lifecycle/pixel acceptance is pending, and no UGENE GUI or
Debian53 result is claimed by this source change. No app-specific option,
application/framework replacement, security test or shared unit suite was used.

The complete54 stack also passes source-only preparation on Debian and Ubuntu:
Git and quilt application/reversal are checked against the pinned source files.
Unit tests were not executed in that gate. Installed54 acceptance remains open.

## IOKit object-message caller (installed55 acceptance pending)

Installed Ubuntu54 now creates, maps, locks and updates ordinary CPU IOSurfaces.
In the cooperating owner/reader scenario the reader passes, but after it exits
the owner's service requests fail: its saved pixels still exist while its
per-process usage query returns zero. A diagnostic client records initial count2,
reader completion, failed owner lock and count0, with the shared in-use flag still
true. This is an ordinary positive lifecycle scenario, not a policy test.

The source routes master-port messages through iokitSaveAuditTrail but dispatches
object-port messages directly to iokit_server. Therefore an object request can
reuse the PID left by another client's master-port request. Patch0055 uses the
same existing caller-capturing handler for both receive paths on the main queue.
It does not change message formats, surface permissions or process cleanup.
IOObject.cpp and main.mm compile with Clang19 and21 against their exact678-file
dependency closure. The complete55 stack also applies and reverses with Git and
quilt on both native bases in source-only mode. Full installed55 lifecycle
acceptance is still pending.

The original layer-client black frames are retained as failures. Its diagnostic
revealed that ordinary view drawing replaced the assigned surface with O2Image
contents. A valid layer-hosting client and actual pixel review are still needed;
no UGENE GUI success follows from the CPU buffer result. No security or shared
unit suites were run.

## Developer-hosted layers and public opacity ABI (installed56 pending)

The valid ordinary hosting diagnostic on installed54 follows Apple's required
order: assign a developer CALayer before wantsLayer and use Core Animation for
its contents. Both Debian and Ubuntu initially hold the surface, but subsequent
NSView drawing replaces it; both expected color frames remain black. Separately,
the original public SDK declares opacity as float, while Cocotron's CGFloat
getter/setter use a different Intel64 return/argument ABI. The same diagnostic
observes zero through that public getter despite the internal default of one.

Patch0056 tracks developer-supplied layers separately from backing layers, keeps
view rasterization from overwriting them, reuses an existing root context and
retires it when a supplied root is replaced. Coalesced one-shot main-thread
frames present non-animated content updates, and invalidation releases both
pending display and animation timers. The public opacity methods use float;
internal CGFloat storage and unrelated class layout are retained.

NSView.m, CALayer.m and CALayerContext.m compile with Clang19 and21 using the
exact828-file dependency closure. The final56-patch stack also applies and
reverses with Git and quilt on both bases in source-only mode. Five internal AppKit/../AppKit include spellings
are normalized within that closure; the initial Debian QA extractor refused them
before compiling, and that failed log remains. This is compiler evidence only:
actual installed56 colors, redraw, root replacement, release and unchanged UGENE
GUI acceptance remain pending. No security or shared unit suites were run.

## Ordinary 2D layer tree order (installed57 pending)

The public three-phase client on installed Ubuntu56 retains and moves the same
surface through a valid tree. Its program succeeds, but a nested blue surface
is absent (green parent visible); flattening it makes blue appear. The renderer
assigns z=tree depth despite a projection limited to -1..1. Merely widening the
projection would also let an earlier deep subtree obscure later siblings.

Patch0057 uses painter order for the supported 2D layer path: all vertices share
one plane and synthetic depth testing is disabled. This does not claim support
for arbitrary 3D transforms or zPosition. The retained positive client checks
nested content, unchanged flattened content and a later sibling over the nested
tree. The renderer compiles with Clang19/21 against the exact840-file closure;
installed changed-runtime pixels remain pending. No security/shared suites ran.

## Backing-layer setter and display callbacks (installed58 pending)

An ordinary public backing-view client on installed Ubuntu56 records one
makeBackingLayer call but zero setLayer/displayLayer calls and three drawRect
calls. Its requested red/cyan surface never appears; direct displayIfNeeded
also does not invoke its delegate. Official Qt5.15.10's Cocoa view uses this
same public setter/delegate contract; the fix is independent of Qt or UGENE.

Patch0058 dispatches implicit backing creation through setLayer, preserves its
implicit ownership and avoids adding absent child layers. Delegate-backed view
display requests layer content instead of replacing it with a rasterized view.
CALayer consumes display demand before callbacks, allows a later request, and
the renderer dispatches pending display before reading contents. These units
compile with Clang19/21 against the exact840-file closure. Installed callback,
pixel, redraw and unchanged UGENE GUI results remain pending; source compilation
does not accept those scenarios. No security or shared unit suites ran.

## Inherited backing-layer return ABI (installed59 pending)

Installed Ubuntu58 now dispatches the public setter and displayLayer callbacks,
including coalesced display demand, but the backing-view frame remains blank.
The same positive client observes methodReturnLength0 for inherited bounds,
position and opacity, where CGRect, CGPoint and float are required. The backing
subclass returned a void stub signature even for methods implemented by CALayer;
the renderer's ordinary NSInvocation interpolation therefore lacks their ABI.

Patch0059 uses the superclass signature for implemented methods and retains the
existing fallback only when the superclass has none. The positive backing-view
client now records these inherited return lengths separately from callbacks and
pixels. The small translation unit compiles against the exact saved native
header closure on Debian/Clang19 and Ubuntu/Clang21. This is a general layer
contract fix, without app-specific selection. Its installed59 pixel and original
UGENE results remain pending; compiler success does not accept them. No shared
or security unit suite ran.

## Early backing-layer native child (installed60 pending)

Installed Ubuntu59 preserves the corrected inherited return lengths32/16/4,
public backing setter/display callbacks and ordinary hosted-root/nested pixels.
Its implicit backing-view frame is still gray. A read-only runtime observer
finds a shared live view/layer context and texture but no native subwindow;
public getters and KVC geometry/opacity agree, with no active animations.

The same ordinary backing view configured after initial window realization
has its native child and actual red/cyan pixels. That diagnostic's original
zero-draw counter also includes one legitimate pre-layer draw, so its whole
program result is not relabeled as a pass. It is an order-of-creation diagnosis,
not original UGENE acceptance. The original before-order-front client remains
unchanged as the installed-runtime gate.

Patch0060 uses the existing private lazy backend accessor when NSWindow creates
a native subwindow. It does not require applications to reorder initialization
or add application-specific settings. The exact changed method expression
compiles with native Clang19/21 and the captured header closure. The whole
NSWindow translation unit was not accepted by that reduced closure: it lacks
NSColorSpace.h. Full-stack/source and installed60 checks remain separate gates.
No shared/security unit suite ran. Ubuntu59 failure stops the next Debian full
engine and further original-app retries rather than claiming GUI compatibility.

## Child termination status (installed61 pending)

Original UGENE53.1 on Ubuntu60 exits quickly with launcher status0 and no visible
window. The same-prefix ordinary echo succeeds and a subsequent unchanged app
launch still has no window; there is no other live original UGENE process. Its
initializer trace reaches application initializers. None of these observations
proves that the application exited normally or identifies its remaining failure.

A later diagnostic runs the exact recorded launch plan under an ordinary
parent Bash in that same prefix, with every original app member still verified.
The parent observes a natural Abort trap6 and reports status134. This confirms
an application termination failure hidden by the earlier status0; it does not
identify the cause of the abort. No crash was intentionally generated.

Source inspection finds that shellspawn unconditionally uses WEXITSTATUS after
waitpid, including signaled children, and can use an uninitialized status after
a failed wait. Patch0061 retries EINTR, preserves ordinary exit codes, returns
128 plus the Darwin termination signal for signaled children, and prints that
signal. A wait failure retains status1. This is a common launcher correction,
without application-specific policy or changes to UGENE.

The complete changed shellspawn translation unit compiles using the configured
Debian60 command and native Clang19. Its exact159-file source/header closure and
arguments also compile with native Ubuntu Clang21. This is candidate-source
validation only: installed61 termination semantics and UGENE GUI remain pending.
No intentional crash, security or shared unit suite ran. The immutable full
Debian60 engine remains separate and unchanged by this candidate.

## Terminated prefix server (installed62 pending)

An ordinary Debian60 Qt startup times out. Its subsequent same-prefix retry
cannot open the old server's mount namespace: the stored PID is still present
with comm darlingserver and the expected owner, but its state is zombie and
its mount namespace is gone. No prefix data or PID file was manually repaired.

Patch0062 checks the stored server's process state before accepting it as live.
A terminated or unreadable process state follows the existing absent-server
path; normal startup can replace only its stale PID record. Existing comm and
ownership checks remain in place, and prefix data is retained. This is a common
server lifecycle correction, without application-specific selection.

An explicit ordinary native lifecycle check runs the exact getInitProcess body
against a real same-user child: first launch, live server, normal child exit,
unreaped terminated state and a saved data marker. The original body reproduces
the stale-server result; the changed body passes on Debian and Ubuntu. No crash
or security/shared suite is involved. This extracted-function check is not a
complete startup-unit build or installed engine acceptance; those remain gates.
