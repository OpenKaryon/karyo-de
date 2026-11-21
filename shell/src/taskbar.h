// SPDX-License-Identifier: GPL-3.0-or-later
// The 48px bottom taskbar: Start, the desktops and the pinned/running apps centered,
// tray, input language, quick settings and clock on the right.
#pragma once

#include <karyo/ui/desktopentry.h>

#include <QList>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <optional>

class AppIndex;
class ClockButton;
class Flyout;
class DockButton;
class GlyphButton;
class LanguageButton;
class QMouseEvent;
class NetworkStatus;
class NightLight;
class NotificationBanner;
class NotificationServer;
class QuickButton;
class QScreen;
class StartButton;
class StartMenu;
class Toplevel;
class ToplevelManager;
class TrayFlyout;
class TrayHost;

class Taskbar : public QWidget
{
    Q_OBJECT
public:
    Taskbar(QScreen *screen, ToplevelManager *toplevels, TrayHost *tray, NetworkStatus *network,
            AppIndex *apps, NotificationServer *notifications, NightLight *nightLight);

    ~Taskbar() override;
    // Close transient output-bound views before retiring this panel.
    void hideForOutputLoss();

    // Opens Start above the taskbar, or closes it when it is open.
    void toggleStart();
    void toggleQuickSettings(bool network = false);
    void toggleNotificationCenter();
    // Start's "Pin to taskbar".
    bool isAppPinned(const QString &id) const;
    void setAppPinned(const QString &id, bool pinned);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Slot
    {
        std::optional<DesktopEntry> entry;
        QString appId;  // set for running apps without a desktop entry
        bool pinned = false;
        QList<QPointer<Toplevel>> windows;
        DockButton *button = nullptr;
    };

    // Alignment, input language and transparency from Settings.
    void readOptions();
    void loadPinned();
    void savePinned() const;
    void syncWindows();
    void layoutButtons();
    void activateSlot(Slot &slot);
    void showSlotMenu(Slot &slot, const QPoint &globalPos);
    // The person's Karyo desktops (openkaryon-desktops): switch, or add one.
    void showDesktopsMenu();
    // Opens the flyout made by create, or closes the open one. The first
    // button shows as highlighted while it is open; all of them toggle it.
    void toggleFlyout(QPointer<Flyout> &flyout, const QList<QWidget *> &buttons,
                      const std::function<Flyout *()> &create);
    Slot *slotForButton(QObject *button);
    DockButton *createButton(Slot &slot);
    // Dragging an app along the taskbar reorders the apps, as on Windows;
    // the pinned order is saved. True when the event belonged to a drag.
    bool dragApp(QObject *watched, QMouseEvent *event);

    ToplevelManager *m_toplevels;
    NetworkStatus *m_network;
    AppIndex *m_apps;
    NotificationServer *m_notifications;
    NightLight *m_nightLight;
    QPointer<Flyout> m_startMenu;
    QPointer<Flyout> m_quickSettings;
    QPointer<Flyout> m_notificationCenter;
    NotificationBanner *m_banner = nullptr;
    bool m_alignLeft = false;
    bool m_opaque = false;

    StartButton *m_start;
    DockButton *m_desktops;
    GlyphButton *m_chevron;
    LanguageButton *m_language;
    QuickButton *m_quick;
    ClockButton *m_clock;
    TrayFlyout *m_trayFlyout;
    QList<Slot> m_slots;
    QPointer<DockButton> m_pressed;
    QPoint m_pressPos;
    int m_grabX = 0;
    bool m_dragging = false;
};
