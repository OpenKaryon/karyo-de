# Ordinary IOSurface buffer client

This positive functional client requests one small BGRA buffer, computes its
layout through the public API, creates and locks it, writes ordinary pixels,
unlocks it and releases it. A zero or undersized layout is a failed allocation
contract, not a successful graphics test. It does not replace framework methods,
change an application, or test security policy.

Build with the Builder Mac probe compiler using `--suite iosurface`. Compilation
does not imply execution; retain the separate runtime report and exit status.
A successful buffer round trip would still not prove layer compositing, IPC,
GPU interoperability or a visible UGENE window.

`karyo-macos-iosurface-matching` compares two valid public queries for the same
registered IOSurfaceRoot: iteration and a direct single-service query. Each call
receives its own matching dictionary, which IOKit consumes. Both returned objects
must expose the expected class, and all acquired ports are released. This localizes
a missing service lookup separately from the later buffer allocation contract.


The suite also builds `karyo-macos-iosurface-lifecycle`. Launch `--owner` with
absolute ready/stop file paths, then run the same executable and desktop scope
with `--reader ID` in a different process. The reader checks shared pixels while
its process count is zero and the owner's count is two, then tests its own
increment/decrement and another local handle. Signal the owner with the stop
file, wait for its ordinary exit, and use `--after-release ID` to check the final
lifetime. These are cooperating valid clients; there is no policy-denial or
security test. Keep all role reports, exit statuses and actual prefix identities.
The owner's report includes unlock/seed, ready-file completion, pixel access and
its usage counts before/after the reader and final decrements. A failing lifetime
therefore retains the individual public API observations rather than one boolean.

`karyo-macos-iosurface-layer REPORT` opens a normal Cocoa hosting view for six seconds.
It assigns a developer-created layer before enabling wantsLayer, checks the public
float opacity default/round trip and does not draw through NSView. Layer contents
are managed through Core Animation; a view-generated backing image must not replace
the supplied surface. The original backing-view variant was not a valid hosting
check and its black frames remain retained failures, not GPU acceptance.
It presents one odd-width surface, updates its pixels/seed after three seconds
without reassigning the contents object, then detaches and checks release.
Capture both phases from the real compositor, verify their distinct expected
colors, export/view the frames and retain the program result. A visible title,
a successful compile or the program's own boolean alone is insufficient.
