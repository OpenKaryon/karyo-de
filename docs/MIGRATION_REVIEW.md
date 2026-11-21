# Remaining DE migration review

User-approved overnight work: migrate the remaining prototype capabilities into
the independent DE, test Debian and Ubuntu, make local reviewable commits and
**do not push**. Forge stays under its separate maintainer. Baselines before this
work are DE 8ef17c0 and Builder a343953. The donor is read-only during extraction.
The long-running task remains active until its remaining scope is implemented
and verified; this checklist must not be read as a completion claim.

## Boundaries and current state

| Capability | State / next acceptance |
| --- | --- |
| Shared UI, shell, lock, selectable session | Already migrated. Prior clean Debian VM lifecycle accepted; ongoing cross-base regression. |
| Debian/Ubuntu native package stage | Added Ubuntu 26.04 build environment and exact base identity validation. Both native fixture packages built successfully; host tests 11/11. |
| Qt compatibility | Explicit private Wayland import for Qt 6.10; Debian Qt 6.8 path retained. |
| Terminal | Public UI targets and independent runtime component migrated. PTY execution and Ctrl+link regression pass on both bases; package/GUI acceptance follows. |
| Camera | Migrated with independent runtime and public UI. No-device controls/close pass on both bases (14/14 total tests); physical photo/video/audio acceptance pending. |
| Photos | Migrated with atomic rotation publication and FileManager1. PNG pixels and failed-write preservation pass on both bases (15/15 total). GUI/package checks follow. |
| PDF Reader | Migrated tabs, search, remembered positions and compatible IPC; native real-PDF/render/second-process tests pass on both bases (22/22 total). Graphical/encrypted-file checks follow. |
| Player | Migrated library/queue/video surface and compatible IPC. Actual PCM decoding, natural sorting and second-process tests pass on both bases (24/24 total). GUI/video-device checks follow. |
| Calculator | Core engine and complete UI migrated with shared history and existing scientific/lab modes. Native regressions pass on both bases (17/17 total); GUI package checks follow. |
| Clock | Migrated timers/stopwatch, persisted deadlines and notifications. Fake-clock/restart regression passes on both bases (18/18 total); audio/GUI checks follow. |
| Editor | Complete independent window, compatible recovery/IPC and lossless encoding choice migrated. Undo, malformed metadata, both dialog choices and real second-process reuse pass on both bases (21/21 total). GUI package checks follow. |
| Monitor | Complete sampler/UI migrated. Reset/PID/model, responsive services, user scope, atomic startup and disposable-child controls pass on both bases (26/26 total). Real authorization/package checks follow. |
| Files | Pending. Preserve FileManager1, existing-window/tab routing, selection, rename, removable disks, folder-size cache and archive handoff. |
| Archive | Pending. Preserve protected archives, password retry/cancel, atomic publication and actual compression/encryption behavior. |
| Snipping | Complete canvas/window/overlay migrated with public shortcut routes and compatible preferences. Native tests pass on both bases (28/28 total). Real overlay/clipboard and optional compositor window-snapshot integration follow. |
| Settings | Pending. Review each page, user persistence, privileged changes and rollback. Public short routes must keep existing functionality. |
| App Center / catalog | Pending. Keep personal/corporate package sources and data external; review privileged installation boundaries. |
| Optional greeter | Pending as optional component. Keep existing display manager authoritative. |
| Compositor layout extension / optional integrations | Separate reviewed packages and explicit capability gates; no mandatory VPN/VMware/Wine services in the base DE. |
| OS defaults, browser/boot/installer, ISO generation | Owned by Karyo Linux/Builder stages, outside the DE migration scope. |

## Validation policy and retained evidence

Every migrated component must compile and run applicable regressions in native
Debian 13 and Ubuntu 26.04 environments; do not reuse binaries across native
bases. Use real VM sessions for desktop/PAM/portal behavior. Hardware-dependent
results must state whether they used virtual devices, fixtures or actual hardware.

Local ignored Builder output out/migration contains native build/test logs and
separate package records. out/debian-vm-qa retains the previous clean Debian VM.
A separately provisioned Ubuntu VM is being prepared under out/ubuntu-vm-qa.
No credential, personal profile, private endpoint or machine image belongs in Git.

Before morning review, list exact commits, verified source revisions, actual
commands/results, package hashes and remaining limitations. Local commit trailers
must contain Co-Authored-By: Codex <noreply@openai.com> exactly once. Do not push
these commits or include another agent's modifications.
