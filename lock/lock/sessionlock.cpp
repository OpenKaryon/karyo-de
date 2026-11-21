// SPDX-License-Identifier: GPL-3.0-or-later
#include "sessionlock.h"

#include "qwayland-ext-session-lock-v1.h"

#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include <QPointer>
#include <QScreen>
#include <QWindow>

namespace SessionLock {

using QtWaylandClient::QWaylandWindow;

// ext_session_lock_v1: forwards locked and finished to the Lock.
class LockObject : public QtWayland::ext_session_lock_v1
{
public:
    explicit LockObject(Lock *lock)
        : m_lock(lock)
    {
    }

protected:
    void ext_session_lock_v1_locked() override
    {
        m_lock->m_locked = true;
        Q_EMIT m_lock->locked();
    }
    void ext_session_lock_v1_finished() override { Q_EMIT m_lock->finished(); }

private:
    Lock *m_lock;
};

namespace {

class LockSurface : public QtWaylandClient::QWaylandShellSurface, public QtWayland::ext_session_lock_surface_v1
{
public:
    LockSurface(LockObject *lock, QWaylandWindow *window)
        : QtWaylandClient::QWaylandShellSurface(window)
    {
        const QPointer<QScreen> screen = window->window()->screen();
        // Qt 6.8 commits the bare surface right after creating its role,
        // which a lock surface must never do: the role comes after that commit.
        QMetaObject::invokeMethod(
            this,
            [this, lock, window, screen] {
                // Output removal can race this queued role creation. Do not
                // dereference an old wl_output or bind Qt's placeholder screen.
                if (!screen || window->window()->screen() != screen)
                    return;
                auto *native = dynamic_cast<QtWaylandClient::QWaylandScreen *>(screen->handle());
                if (native && native->output())
                    init(lock->get_lock_surface(window->waylandSurface()->object(), native->output()));
            },
            Qt::QueuedConnection);
    }
    ~LockSurface() override
    {
        if (object())
            destroy();
    }

    bool isExposed() const override { return m_configured; }

    void applyConfigure() override { window()->resizeFromApplyConfigure(m_pendingSize); }

private:
    void ext_session_lock_surface_v1_configure(uint32_t serial, uint32_t width, uint32_t height) override
    {
        ack_configure(serial);
        m_pendingSize = QSize(int(width), int(height));
        if (!m_configured) {
            m_configured = true;
            applyConfigure();
#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
            window()->sendRecursiveExposeEvent();
#else
            window()->updateExposure();
#endif
        } else {
            window()->applyConfigureWhenPossible();
        }
    }

    QSize m_pendingSize;
    bool m_configured = false;
};

class Integration : public QtWaylandClient::QWaylandShellIntegrationTemplate<Integration>,
                    public QtWayland::ext_session_lock_manager_v1
{
public:
    Integration()
        : QtWaylandClient::QWaylandShellIntegrationTemplate<Integration>(1)
    {
    }
    ~Integration() override
    {
        if (object())
            destroy();
    }

    QtWaylandClient::QWaylandShellSurface *createShellSurface(QWaylandWindow *window) override
    {
        return new LockSurface(lockObject, window);
    }

    LockObject *lockObject = nullptr;
};

Integration *integration = nullptr;

} // namespace

Lock::Lock(QObject *parent)
    : QObject(parent)
{
}

Lock *Lock::create(QWindow *window, QObject *parent)
{
    auto *waylandWindow = dynamic_cast<QWaylandWindow *>(window->handle());
    if (!waylandWindow || integration)
        return nullptr;
    integration = new Integration;
    if (!integration->initialize(waylandWindow->display())) {
        delete integration;
        integration = nullptr;
        return nullptr;
    }
    auto *lock = new Lock(parent);
    auto *object = new LockObject(lock);
    object->init(integration->lock());
    integration->lockObject = object;
    lock->m_object = object;
    lock->m_display = waylandWindow->display();
    return lock;
}

bool Lock::outputAvailable(QScreen *screen)
{
    auto *wayland = screen ? dynamic_cast<QtWaylandClient::QWaylandScreen *>(screen->handle()) : nullptr;
    return wayland && wayland->output();
}

bool Lock::attach(QWindow *window)
{
    if (!window || !outputAvailable(window->screen()))
        return false;
    if (auto *waylandWindow = dynamic_cast<QWaylandWindow *>(window->handle())) {
        waylandWindow->setShellIntegration(integration);
        return true;
    }
    return false;
}

void Lock::unlock()
{
    if (!m_locked || !m_object)
        return;
    m_object->unlock_and_destroy();
    m_locked = false;
    // The display outlives hotplugged lock windows and in-flight PAM checks.
    if (m_display)
        m_display->forceRoundTrip();
}

} // namespace SessionLock
