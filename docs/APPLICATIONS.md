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
