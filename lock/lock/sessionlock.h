// SPDX-License-Identifier: GPL-3.0-or-later
// ext-session-lock-v1 for Qt windows, modelled on layer-shell-qt: the
// compositor hides every other surface, and each lock window becomes the
// lock surface of its screen. Unlocking needs the locked event first.
#pragma once

#include <QObject>

class QWindow;

namespace SessionLock {

class LockObject;

class Lock : public QObject
{
    Q_OBJECT
public:
    // Locks the session of the display that window belongs to. The window
    // must be created (winId()) but not shown yet.
    static Lock *create(QWindow *window, QObject *parent = nullptr);

    // Before show(): the window becomes this lock's surface on its screen.
    void attach(QWindow *window);
    bool isLocked() const { return m_locked; }
    // Unlocks and waits for the compositor to hear it.
    void unlock();

Q_SIGNALS:
    void locked();
    // The compositor refused the lock (another locker runs) or ended it.
    void finished();

private:
    explicit Lock(QObject *parent);
    friend class LockObject;
    bool m_locked = false;
    LockObject *m_object = nullptr;
};

} // namespace SessionLock
