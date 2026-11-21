# Optional greetd front end

`karyo-greeter` is a greetd client for a Wayland kiosk compositor. It does not
replace a display manager, install PAM policy, create an account, enable a
service or change autologin. Installing its package is inert. Existing Karyo
sessions continue to work under LightDM or another display manager.

For explicit administrator integration, install greetd, cage and dbus-run-session
(`dbus-daemon` on the tested bases). Read the platform's greetd and cage manuals,
verify its greeter account and select an available virtual terminal. The supplied
`greetd-karyo.toml.example` is documentation, not an installed configuration.
Its `greeter` account is an example: use a dedicated unprivileged service account
with its own writable home/config directory, never root or a regular user's home.
Do not run two display managers on the same seat/VT. Keep SSH or a rescue TTY and
back up the current display-manager configuration before activation.

The client requires a greetd-provided `GREETD_SOCK` and Wayland display. greetd
performs authentication and starts the selected session only after successful
PAM authentication. The client supports repeated secret, visible, information
and error prompts; cancellation is acknowledged before another attempt. Passwords
are not saved in preference files. PAM configuration remains the administrator's
responsibility and is not bypassed by this UI.

Main login accounts and executable sessions in `/usr/share/wayland-sessions`
appear in the selectors; child desktop accounts are omitted. Karyo is preferred,
but other installed Wayland sessions remain selectable. The last user/session
are stored atomically in the greeter account's `karyo/greeter.json` below its XDG
configuration directory, only after session-start acknowledgement. A save failure
is reported to stderr without preventing an already authorized session.

The initial appearance can adopt the last selected user's public theme/accent
from `/var/lib/karyo/appearance/USER.conf`, with legacy fallback only when the
new path is absent. The file must be a small regular file owned by that user;
symlinks are rejected. No user's private home preferences are read. The shell publishes these two values at startup and when desktop preferences
change, deriving its account from the effective UID rather than USER. It writes
atomically relative to a no-follow directory descriptor and refuses another
owner's existing file, symlinks, special files and unsafe shared directories.
No password, token or arbitrary preferences are published.

The greeter component provides /usr/lib/tmpfiles.d/karyo-greeter.conf. Normal
tmpfiles boot processing creates the optional root-owned 1733 publication
directory; for immediate explicit integration run
sudo systemd-tmpfiles --create /usr/lib/tmpfiles.d/karyo-greeter.conf.
This creates only a data directory and does not activate greetd. A missing
publication directory is harmless: the base session still runs without a greeter.
The directory permits creating known per-user files but not listing it or
replacing another account's files; the greeter also checks the file owner.

`--help` works without a graphical display. Other arguments are rejected. The
runtime uses Qt Wayland, the Karyo style, IBM Plex when installed and embedded
Russian translations selected by the greeter account's locale. Display-manager
language and keyboard configuration must therefore be set explicitly there.

Native tests use private fake daemons and temporary preferences on Debian and
Ubuntu. They do not establish real PAM/login/logout acceptance. That separate
check must use disposable machines and restore the previous display manager.

References: [greetd IPC](https://manpages.debian.org/trixie/greetd/greetd-ipc.7.en.html),
[greetd configuration](https://manpages.debian.org/trixie/greetd/greetd.5.en.html),
[cage](https://manpages.debian.org/trixie/cage/cage.1.en.html).
