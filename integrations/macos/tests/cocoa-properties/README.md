# Ordinary installed Cocoa property checks

These GPL-3.0-or-later clients call the actual shared NSWindow and NSMenuItem
implementations. They contain no category replacements or factory overrides
and are not installed in a desktop runtime package. The tracked NSView subclass
only observes final destruction; the menu item itself is the real AppKit class.

The window client checks defaults, independent values and reset. Its windows
are deferred and never shown, so it does not accept native tab groups or
transparent compositor decorations. The menu client checks default nil,
caller-release retention, self-assignment, copied ownership, clearing and final
release. It does not accept custom-view rendering, events or keyed archiving.
Each executable writes one JSON result and returns0 only when its checks pass.

Use Builder's existing external cross-SDK driver with the property suite:

```sh
python tests/acceptance/build-macos-probes.py --suite cocoa-properties \
    --de-source /path/to/karyo-de --image local-macos-cross-sdk --output out
```

The SDK needs an Intel macOS Clang wrapper and Cocoa headers/libraries; Qt is
not required for this suite. The archive preserves executable modes. The build
receipt records source bytes, revision, SDK identity and executable hashes.
Compilation establishes no runtime compatibility. Run both executables through
the installed karyo-macos launcher as an ordinary desktop user on each target
kernel, retain stdout/exit status and identify the installed runtime provenance.
