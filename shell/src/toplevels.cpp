// SPDX-License-Identifier: GPL-3.0-or-later
#include "toplevels.h"

#include <QGuiApplication>

namespace {

quint64 nextActivationSerial()
{
    static quint64 serial = 0;
    return ++serial;
}

wl_seat *waylandSeat()
{
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    return app ? app->seat() : nullptr;
}

} // namespace

Toplevel::Toplevel(::zwlr_foreign_toplevel_handle_v1 *handle, QObject *parent)
    : QObject(parent)
    , QtWayland::zwlr_foreign_toplevel_handle_v1(handle)
{
}

Toplevel::~Toplevel()
{
    if (object())
        destroy();
}

void Toplevel::activateWindow()
{
    if (wl_seat *seat = waylandSeat())
        activate(seat);
}

void Toplevel::minimizeWindow() { set_minimized(); }

void Toplevel::closeWindow() { close(); }

void Toplevel::zwlr_foreign_toplevel_handle_v1_title(const QString &title)
{
    m_pendingTitle = title;
}

void Toplevel::zwlr_foreign_toplevel_handle_v1_app_id(const QString &appId)
{
    m_pendingAppId = appId;
}

void Toplevel::zwlr_foreign_toplevel_handle_v1_state(wl_array *state)
{
    m_pendingActive = false;
    m_pendingMinimized = false;
    const auto *values = static_cast<const uint32_t *>(state->data);
    const size_t count = state->size / sizeof(uint32_t);
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == state_activated)
            m_pendingActive = true;
        else if (values[i] == state_minimized)
            m_pendingMinimized = true;
    }
}

void Toplevel::zwlr_foreign_toplevel_handle_v1_done()
{
    if (!m_initialized) {
        // Title and app_id are only sent when set; keep pending values in step.
        m_initialized = true;
    }
    if (m_pendingActive && !m_active)
        m_activationSerial = nextActivationSerial();
    m_title = m_pendingTitle;
    m_appId = m_pendingAppId;
    m_active = m_pendingActive;
    m_minimized = m_pendingMinimized;
    Q_EMIT changed();
}

void Toplevel::zwlr_foreign_toplevel_handle_v1_closed() { Q_EMIT closed(); }

ToplevelManager::ToplevelManager(QObject *parent)
    : QWaylandClientExtensionTemplate<ToplevelManager>(3)
{
    setParent(parent);
    initialize();
}

ToplevelManager::~ToplevelManager()
{
    qDeleteAll(m_toplevels);
    if (object()) {
        if (isActive())
            stop();
        // The compositor may still send events until it handles stop().
        zwlr_foreign_toplevel_manager_v1_destroy(object());
    }
}

void ToplevelManager::zwlr_foreign_toplevel_manager_v1_toplevel(
    ::zwlr_foreign_toplevel_handle_v1 *handle)
{
    auto *toplevel = new Toplevel(handle, this);
    // Announce the window only once its first state batch is complete.
    connect(toplevel, &Toplevel::changed, this, [this, toplevel] {
        if (!m_toplevels.contains(toplevel)) {
            m_toplevels.append(toplevel);
            Q_EMIT toplevelAdded(toplevel);
        } else {
            Q_EMIT toplevelChanged(toplevel);
        }
    });
    connect(toplevel, &Toplevel::closed, this, [this, toplevel] {
        if (m_toplevels.removeOne(toplevel))
            Q_EMIT toplevelRemoved(toplevel);
        toplevel->deleteLater();
    });
}

void ToplevelManager::zwlr_foreign_toplevel_manager_v1_finished()
{
    const QList<Toplevel *> toplevels = m_toplevels;
    m_toplevels.clear();
    for (Toplevel *toplevel : toplevels) {
        Q_EMIT toplevelRemoved(toplevel);
        toplevel->deleteLater();
    }
}
