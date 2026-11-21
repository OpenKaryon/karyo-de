// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsnapshot.h"
#include "wayland-openkaryon-window-snapshot-v1-client-protocol.h"

#include <QCoreApplication>
#include <wayland-client.h>

namespace {
struct Snapshot {
    openkaryon_window_snapshot_v1 *control = nullptr;
    QList<CaptureWindow> windows;
    bool available = false;
    bool done = false;
};
void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t)
{
    auto *snapshot = static_cast<Snapshot *>(data);
    if (qstrcmp(interface, openkaryon_window_snapshot_v1_interface.name) == 0)
        snapshot->control = static_cast<openkaryon_window_snapshot_v1 *>(
            wl_registry_bind(registry, name, &openkaryon_window_snapshot_v1_interface, 1));
}
void removed(void *, wl_registry *, uint32_t) {}
void window(void *data, openkaryon_window_snapshot_v1 *, int32_t x, int32_t y, int32_t width, int32_t height, const char *title)
{
    if (width > 0 && height > 0)
        static_cast<Snapshot *>(data)->windows.append({QRect(x, y, width, height), QString::fromUtf8(title)});
}
void done(void *data, openkaryon_window_snapshot_v1 *, uint32_t available)
{
    auto *snapshot = static_cast<Snapshot *>(data);
    snapshot->available = available;
    snapshot->done = true;
}
const wl_registry_listener registryListener = {global, removed};
const openkaryon_window_snapshot_v1_listener listener = {window, done};
}

bool windowSnapshot(QList<CaptureWindow> *windows, QString *error)
{
    // A separate connection keeps the finite snapshot independent of Qt's
    // event queue and never changes focus, stacking, or compositor state.
    wl_display *display = wl_display_connect(nullptr);
    if (!display) {
        *error = QCoreApplication::translate("Snipper", "Cannot connect to the Wayland session.");
        return false;
    }
    Snapshot snapshot;
    wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registryListener, &snapshot);
    if (wl_display_roundtrip(display) >= 0 && snapshot.control) {
        openkaryon_window_snapshot_v1_add_listener(snapshot.control, &listener, &snapshot);
        openkaryon_window_snapshot_v1_snapshot(snapshot.control);
        wl_display_roundtrip(display);
    }
    const bool supported = snapshot.control;
    if (snapshot.control)
        openkaryon_window_snapshot_v1_destroy(snapshot.control);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    if (!supported)
        *error = QCoreApplication::translate("Snipper", "This compositor does not provide window selection. Choose region or screen capture.");
    else if (!snapshot.done || !snapshot.available)
        *error = QCoreApplication::translate("Snipper", "Window capture is unavailable while the session is locked or disconnected.");
    else {
        *windows = snapshot.windows;
        return true;
    }
    return false;
}
