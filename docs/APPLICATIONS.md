# Independent desktop applications

Applications use public Karyo UI targets and independently installable runtime
components. KARYO_BUILD_APPS controls application builds; it defaults to the
selected desktop-integration setting. Applications can build without the shell
and lock using KARYO_BUILD_SESSION=OFF. Product packaging does not register a
new display manager or change another desktop's application defaults.

## Terminal

karyo-terminal (short command: terminal) uses the distribution's QTermWidget 6,
not an embedded fork. It retains tabs, shell titles, SSH aliases read from the
current user's configuration, search, zoom, selection copy and explicit paste.
Ctrl+click opens HTTP(S) links. The PTY supports -w/--workdir and -e/-x/--execute
with exact argument forwarding. Single-string execute retains shell syntax.

The public desktop ID is karyo-terminal.desktop. Start/taskbar defaults include
it while retaining the legacy launcher fallback. The Karyo session selects it
only when TERMINAL was not explicitly supplied and the binary is installed.
No update-alternatives registration or global terminal replacement is performed.

The terminal-runtime CMake install component owns the executable, wrapper,
launcher and license/provenance. Builder examples/terminal.json declares runtime
data/plugins; native ELF dependencies are discovered from the selected base.

Regression coverage executes an actual shell through the PTY in a temporary
working directory and checks an argument containing spaces. The URL test drives
Qt modifier events and the real signal/URL handler: ordinary clicks and file URLs
must not open a browser, while Ctrl+HTTP(S) does. Debian 13/Qt 6.8 and Ubuntu
26.04/Qt 6.10 both pass all thirteen current CTest targets.

Physical keyboard/browser integration remains part of VM acceptance; the
synthetic modifier test is not a claim about host hypervisor key interception.

## Camera

karyo-camera (short command: camera) keeps the Qt Multimedia capture session,
JPEG photos and negotiated video/audio encoding, default camera/microphone hotplug
and standard Pictures/Videos paths. Closing while recording waits for finalization;
errors remain visible and do not report an empty output as a successful recording.
The standalone camera-runtime component has no hardware/provisioning scripts.

The no-device fixture verifies the unavailable-camera message, disabled capture
controls and normal close on both native bases. Physical photo/video/audio capture
is still a hardware acceptance step; no real device is claimed by this fixture.

## Photos

karyo-photos (short command: viewer) retains the gallery, bounded asynchronous
thumbnail cache, filmstrip, zoom/pan, animations, rotation, clipboard and desktop
background selection. Image formats follow the installed Qt image plugins. The
photos-runtime component owns its launchers without replacing global MIME choices.

Showing a file uses freedesktop FileManager1.ShowItems, allowing an installed
file manager to select the original. If none handles the request, the default
folder application opens its parent; no prototype executable is required.
Rotation publishes through QSaveFile instead of overwriting the original while
encoding. Tests verify a PNG's actual rotated dimensions/pixels and byte-identical
original contents after a failed unsupported-format write. All fifteen current
CTest targets pass on both native bases.

## Calculator

karyo-calculator (short command: calculator) uses the retained Qt Core expression
and unit engine with standard/scientific views, a shared history and the existing
molarity/dilution/nucleic-acid conversion pages. The --mode routes and desktop
actions remain available. Explicit legacy user settings retain the selected mode
and angle choice; no old executable name or external scientific runtime is needed.

The engine regression checks expressions, errors and unit/formula behavior. A
separate widget test types expressions through the display, evaluates percentages,
rejects division by zero, shares history between modes and clears it, using an
isolated configuration directory. All seventeen current tests pass on both bases.

## Clock

karyo-clock (short command: clock) retains named timers, pause/reset/extra time,
stopwatch/laps, persistence of wall-clock deadlines and freedesktop notifications.
The legacy clock-timers.json location remains readable, so elapsed timers are
reconciled when the application is opened again. Alarm playback uses available
audio tools with an application-beep fallback and introduces no system daemon.

The retained deterministic test advances a supplied clock, persists timers,
reopens them after elapsed time and verifies completion occurs once. Both native
bases pass all eighteen current tests. Actual audio playback remains a device/
service acceptance scenario, not a property established by the fake-clock test.

## Text Editor

karyo-editor (short command: notepad) retains tabs, search/replace, syntax modes,
indentation, external-change banners, shared commands and per-buffer grouped undo.
The existing recovery directory and Editor session-bus endpoint remain compatible:
old and public launches hand files to one window instead of competing for the
same unsaved buffers. Only the executable/desktop identity and public C++ targets
change; no personal text or session files are packaged.

Unrepresentable characters are rejected before a legacy Latin-1 write. The GUI
explicitly offers UTF-8; Cancel leaves the original and unsaved state unchanged,
while confirmation preserves both old and new characters. Recovery validates
metadata/version and buffer IDs, backs up unreadable session metadata and retains
unreferenced caches. Explicit tab close still removes that tab's own cache.

The editor window test restores a prototype-format unsaved tab, keeps its undo,
retains orphan recovery through corrupt metadata and a second reopening, drives
both encoding-dialog choices, and verifies a real second process reuses the
session-bus window. All twenty-one current tests pass on both native bases.

## PDF Reader

karyo-reader (short command: pdf) retains tabbed documents, thumbnails, contents,
text search, selectable page text, password prompts and remembered page/zoom.
Existing reader preferences and the org.openkaryon.Reader wire endpoint remain
compatible so a second process forwards files to one window. Show in Files uses
freedesktop FileManager1.ShowItems, falling back to the default folder opener.

A real two-page PDF is generated in the regression. The test renders it, extracts
text, replaces searches while results are pending, restores page/zoom/sidebar and
rejects missing/non-PDF inputs. A real second process reuses the window without
duplicating tabs. All twenty-two tests pass on native Debian 13 and Ubuntu 26.04.
Encrypted-document interaction and graphical package checks remain separate
acceptance items; the automated fixture does not establish those results.

## Media Player

karyo-player (short command: player) retains Music/Videos library scans on a
worker thread, queue/shuffle/repeat, metadata/artwork, seek/volume, fullscreen
and CPU-painted video frames. The existing preferences and Player bus endpoint
remain compatible; Show in Files uses the common FileManager1 handoff. Native
Qt Multimedia/FFmpeg dependencies remain external distribution packages.

Tests generate a PCM WAV and receive real decoded audio buffers through Qt
6.8+ QAudioBufferOutput without requiring an audio device. They also exercise
seek dragging and a real second-process handoff. A regression exposed lexical
track ordering under the C locale; natural sorting now works under C, English
and Russian locales. All 24 tests pass on both bases. Physical audio, video
decoding/GUI and installed package behavior still need acceptance.

## System Monitor

karyo-monitor (short command: monitor) retains grouped applications/workloads,
process details, CPU/memory/disk/network graphs, users, services and startup
entries. Sampling uses monotonic elapsed time and reset-safe counters. Service
queries are asynchronous with a timeout; control errors are visible and system
service authorization continues through the active polkit agent. No service is
changed at startup.

Process termination validates the sampled start time and uses a Linux pidfd so
a recycled PID cannot receive its signal. Efficiency mode retains the ordinary
setpriority permission checks; Linux provides no pidfd form of that operation,
so start time is checked immediately beforehand. Actions report kernel errors.
Startup changes atomically write only the user's override, preserve Desktop
Actions and correctly re-enable an entry with the GNOME autostart flag disabled.

Deterministic procfs/sysfs tests cover resets, reused/disappeared PIDs and model
invariants. UI tests retain a responsive event loop while a fixture systemctl
waits, preserve user service scope and system startup bytes, and control only
a disposable child process. All 26 tests pass on both native bases; real VM
service authorization and installed-monitor checks remain acceptance work.
