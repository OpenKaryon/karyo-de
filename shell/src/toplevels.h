// SPDX-License-Identifier: GPL-3.0-or-later
// Window list from wlr-foreign-toplevel-management (labwc, other wlroots compositors).
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-wlr-foreign-toplevel-management-unstable-v1.h"

class Toplevel : public QObject, public QtWayland::zwlr_foreign_toplevel_handle_v1
{
    Q_OBJECT
public:
    explicit Toplevel(::zwlr_foreign_toplevel_handle_v1 *handle, QObject *parent = nullptr);
    ~Toplevel() override;

    QString title() const { return m_title; }
    QString appId() const { return m_appId; }
    bool isActive() const { return m_active; }
    bool isMinimized() const { return m_minimized; }
    // Order in which windows were last activated; higher is more recent.
    quint64 activationSerial() const { return m_activationSerial; }

    void activateWindow();
    void minimizeWindow();
    void closeWindow();

Q_SIGNALS:
    void changed();
    void closed();

protected:
    void zwlr_foreign_toplevel_handle_v1_title(const QString &title) override;
    void zwlr_foreign_toplevel_handle_v1_app_id(const QString &appId) override;
    void zwlr_foreign_toplevel_handle_v1_state(wl_array *state) override;
    void zwlr_foreign_toplevel_handle_v1_done() override;
    void zwlr_foreign_toplevel_handle_v1_closed() override;

private:
    QString m_title;
    QString m_appId;
    bool m_active = false;
    bool m_minimized = false;
    quint64 m_activationSerial = 0;

    QString m_pendingTitle;
    QString m_pendingAppId;
    bool m_pendingActive = false;
    bool m_pendingMinimized = false;
    bool m_initialized = false;
};

class ToplevelManager : public QWaylandClientExtensionTemplate<ToplevelManager>,
                        public QtWayland::zwlr_foreign_toplevel_manager_v1
{
    Q_OBJECT
public:
    explicit ToplevelManager(QObject *parent = nullptr);
    ~ToplevelManager() override;

    QList<Toplevel *> toplevels() const { return m_toplevels; }

Q_SIGNALS:
    void toplevelAdded(Toplevel *toplevel);
    void toplevelRemoved(Toplevel *toplevel);
    void toplevelChanged(Toplevel *toplevel);

protected:
    void zwlr_foreign_toplevel_manager_v1_toplevel(
        ::zwlr_foreign_toplevel_handle_v1 *toplevel) override;
    void zwlr_foreign_toplevel_manager_v1_finished() override;

private:
    QList<Toplevel *> m_toplevels;
};
