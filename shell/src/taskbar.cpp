// SPDX-License-Identifier: GPL-3.0-or-later
#include "integrations.h"
#include "taskbar.h"

#include <karyo/ui/appstyle.h>
#include "buttons.h"
#include "keyboardlayout.h"
#include "layersurface.h"
#include "networkstatus.h"
#include "notify/notificationbanner.h"
#include "notify/notificationcenter.h"
#include "notify/notificationserver.h"
#include "quick/quicksettings.h"
#include "start/startmenu.h"
#include "toplevels.h"
#include "trayflyout.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QProcess>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QWindow>
#include <QStandardPaths>

#include <algorithm>

namespace Ui = Karyo::Ui;

namespace {

constexpr int BarHeight = 48;
constexpr int Gap = 4;
constexpr int SidePadding = 12;

// Matches the dock of KaryonShell.dc.html; missing apps are skipped.
const QStringList DefaultPinned = {
    QStringLiteral("openkaryon-files.desktop"),   QStringLiteral("chromium.desktop"),
    QStringLiteral("karyo-terminal.desktop"), QStringLiteral("openkaryon-terminal.desktop"), QStringLiteral("karyo-editor.desktop"), QStringLiteral("openkaryon-editor.desktop"),
    QStringLiteral("rstudio.desktop"),            QStringLiteral("openkaryon-appcenter.desktop"),
    QStringLiteral("karyo-monitor.desktop"), QStringLiteral("openkaryon-monitor.desktop"), QStringLiteral("openkaryon-settings.desktop"),
};

QSettings shellSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("shell"));
}

// The desktops button wears an app tile, like the apps next to it.
AppStyle desktopsStyle()
{
    AppStyle style;
    style.glyph = QStringLiteral("layers");
    style.tile = Ui::sage(200);
    style.ink = Ui::sage(800);
    return style;
}

// The session's XKB layout at this group, shown as EN, RU, DE...
QString layoutCode(int group)
{
    const QStringList layouts = qEnvironmentVariable("XKB_DEFAULT_LAYOUT").split(QLatin1Char(','));
    QString layout = layouts.value(group, layouts.value(0)).trimmed();
    if (layout.isEmpty() || layout == QLatin1String("us") || layout == QLatin1String("gb"))
        return QStringLiteral("EN");
    return layout.left(2).toUpper();
}

} // namespace

Taskbar::Taskbar(QScreen *screen, ToplevelManager *toplevels, TrayHost *tray,
                 NetworkStatus *network, AppIndex *apps, NotificationServer *notifications,
                 NightLight *nightLight)
    : QWidget(nullptr, Qt::FramelessWindowHint)
    , m_toplevels(toplevels)
    , m_network(network)
    , m_apps(apps)
    , m_notifications(notifications)
    , m_nightLight(nightLight)
    , m_start(new StartButton(this))
    , m_desktops(new DockButton(desktopsStyle(), this))
    , m_chevron(new GlyphButton(QStringLiteral("chevron-up"), 16, QSize(32, 40), 6, this))
    , m_language(new LanguageButton(this))
    , m_quick(new QuickButton(this))
    , m_clock(new ClockButton(this))
    , m_trayFlyout(new TrayFlyout(tray, this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("karyo-taskbar"));
    LayerSurface::configure(this, screen, LayerSurface::LayerTop,
                            LayerSurface::AnchorBottom | LayerSurface::AnchorLeft
                                | LayerSurface::AnchorRight,
                            BarHeight);
    setFixedHeight(BarHeight);
    resize(screen->size().width(), BarHeight);

    m_start->setHint(tr("Start"));
    m_desktops->setHint(tr("Desktops (Ctrl+Alt+Left/Right)"));
    // Isolated user desktops are an optional system integration.
    m_desktops->setVisible(!QStandardPaths::findExecutable(QStringLiteral("openkaryon-desktops")).isEmpty());
    m_chevron->setHint(tr("Show hidden icons"));
    // The active layout: Alt+Shift and the like switch it, and labwc keeps one
    // per window where configured.
    auto *keyboardLayout = new KeyboardLayout(this);
    auto showLayout = [this](int group) {
        const QString code = layoutCode(group);
        m_language->setCode(code);
        const QLocale layoutLocale(code.toLower());
        m_language->setHint(tr("Input language: %1")
                                .arg(layoutLocale.language() == QLocale::English
                                         ? QLocale::languageToString(QLocale::English)
                                         : layoutLocale.nativeLanguageName()));
    };
    connect(keyboardLayout, &KeyboardLayout::groupChanged, this, showLayout);
    m_language->setEnabled(keyboardLayout->canCycle());
    connect(keyboardLayout, &KeyboardLayout::availabilityChanged, m_language, &QWidget::setEnabled);
    connect(m_language, &QAbstractButton::clicked, keyboardLayout, &KeyboardLayout::cycle);
    showLayout(keyboardLayout->group());
    m_quick->setHint(tr("Quick settings"));

    connect(m_start, &QAbstractButton::clicked, this, &Taskbar::toggleStart);
    connect(m_desktops, &QAbstractButton::clicked, this, &Taskbar::showDesktopsMenu);
    connect(m_quick, &QAbstractButton::clicked, this, [this] { toggleQuickSettings(); });
    connect(m_quick, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *menu = new QMenu(m_quick);
        Ui::styleMenu(menu);
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        menu->addAction(tr("Network settings"), this, [] {
            Integrations::openSettings(true);
        });
        menu->popup(m_quick->mapToGlobal(pos));
    });
    connect(m_clock, &QAbstractButton::clicked, this, &Taskbar::toggleNotificationCenter);

    // New notifications: the count on the clock, and a banner above the tray
    // unless the Notification center is open.
    auto updateCount = [this] { m_clock->setNotificationCount(int(m_notifications->notifications().size())); };
    connect(m_notifications, &NotificationServer::changed, this, updateCount);
    updateCount();
    m_banner = new NotificationBanner(m_notifications, screen);
    connect(m_notifications, &NotificationServer::bannerRequested, this, [this](const Notification &n) {
        if (!m_notificationCenter)
            m_banner->showNotification(n);
    });
    connect(m_chevron, &QAbstractButton::clicked, this, [this] {
        if (m_trayFlyout->isVisible())
            m_trayFlyout->close();
        else
            m_trayFlyout->popupAbove(m_chevron);
    });
    connect(m_trayFlyout, &TrayFlyout::networkRequested, this,
            [this] { toggleQuickSettings(true); }, Qt::QueuedConnection);
    m_trayFlyout->installEventFilter(this);
    // The clock widens with the time and the notification badge.
    m_clock->installEventFilter(this);

    auto updateNetwork = [this] {
        m_quick->setNetworkGlyph(m_network->glyph(), m_network->isConnected());
    };
    connect(m_network, &NetworkStatus::changed, this, updateNetwork);
    updateNetwork();

    loadPinned();
    connect(m_toplevels, &ToplevelManager::toplevelAdded, this, &Taskbar::syncWindows);
    connect(m_toplevels, &ToplevelManager::toplevelRemoved, this, &Taskbar::syncWindows);
    connect(m_toplevels, &ToplevelManager::toplevelChanged, this, &Taskbar::syncWindows);
    syncWindows();

    readOptions();
    connect(Ui::ThemeNotifier::instance(), &Ui::ThemeNotifier::configurationChanged, this,
            [this](const QString &name) {
                if (name == QLatin1String("shell") || name == QLatin1String("desktop"))
                    readOptions();
            });
    connect(Ui::ThemeNotifier::instance(), &Ui::ThemeNotifier::changed, this, [this] {
        const auto children = findChildren<QWidget *>();
        for (QWidget *child : children)
            child->update();
        update();
    });
}

Taskbar::~Taskbar()
{
    // These layer-shell toplevels have no QWidget parent. Shared service/model
    // objects belong to the application and deliberately survive this panel.
    delete m_startMenu.data();
    delete m_quickSettings.data();
    delete m_notificationCenter.data();
    delete m_banner;
}

void Taskbar::hideForOutputLoss()
{
    hide();
    for (const auto &flyout : { m_startMenu, m_quickSettings, m_notificationCenter }) {
        if (flyout)
            flyout->hide();
    }
    m_trayFlyout->hide();
    m_banner->hide();
    for (auto *dialog : findChildren<QDialog *>())
        dialog->reject();
}

void Taskbar::readOptions()
{
    const QSettings shell = shellSettings();
    m_alignLeft = shell.value(QStringLiteral("Taskbar/alignment")).toString() == QLatin1String("left");
    m_language->setVisible(shell.value(QStringLiteral("Taskbar/showLanguage"), true).toBool());
    const QSettings desktop(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                            QStringLiteral("desktop"));
    m_opaque = !desktop.value(QStringLiteral("Appearance/transparency"), true).toBool();
    layoutButtons();
    update();
}

void Taskbar::loadPinned()
{
    QSettings settings = shellSettings();
    const QStringList ids = settings.value(QStringLiteral("Taskbar/pinned"), DefaultPinned).toStringList();
    for (const QString &id : ids) {
        auto entry = DesktopEntry::find(id);
        if (!entry)
            continue;
        Slot slot;
        slot.entry = entry;
        slot.pinned = true;
        m_slots.append(slot);
        m_slots.last().button = createButton(m_slots.last());
    }
}

void Taskbar::savePinned() const
{
    QStringList ids;
    for (const Slot &slot : m_slots) {
        if (slot.pinned && slot.entry)
            ids.append(slot.entry->id);
    }
    QSettings settings = shellSettings();
    settings.setValue(QStringLiteral("Taskbar/pinned"), ids);
}

bool Taskbar::isAppPinned(const QString &id) const
{
    return std::any_of(m_slots.cbegin(), m_slots.cend(),
                       [&](const Slot &slot) { return slot.pinned && slot.entry && slot.entry->id == id; });
}

void Taskbar::setAppPinned(const QString &id, bool pinned)
{
    for (Slot &slot : m_slots) {
        if (slot.entry && slot.entry->id == id) {
            slot.pinned = pinned;
            savePinned();
            syncWindows();
            return;
        }
    }
    if (!pinned)
        return;
    // Not on the taskbar yet: pinned after the others.
    auto entry = DesktopEntry::find(id);
    if (!entry)
        return;
    Slot slot;
    slot.entry = entry;
    slot.pinned = true;
    m_slots.append(slot);
    m_slots.last().button = createButton(m_slots.last());
    savePinned();
    syncWindows();
}

DockButton *Taskbar::createButton(Slot &slot)
{
    const AppStyle style = slot.entry ? AppStyle::forEntry(*slot.entry)
                                      : AppStyle::fallback(slot.appId);
    auto *button = new DockButton(style, this);
    button->setHint(slot.entry ? slot.entry->name : slot.appId);
    button->setContextMenuPolicy(Qt::CustomContextMenu);
    button->installEventFilter(this);
    connect(button, &QAbstractButton::clicked, this, [this, button] {
        if (Slot *s = slotForButton(button))
            activateSlot(*s);
    });
    connect(button, &QWidget::customContextMenuRequested, this, [this, button](const QPoint &pos) {
        if (Slot *s = slotForButton(button))
            showSlotMenu(*s, button->mapToGlobal(pos));
    });
    button->show();
    return button;
}

Taskbar::Slot *Taskbar::slotForButton(QObject *button)
{
    for (Slot &slot : m_slots) {
        if (slot.button == button)
            return &slot;
    }
    return nullptr;
}

void Taskbar::syncWindows()
{
    for (Slot &slot : m_slots)
        slot.windows.clear();

    for (Toplevel *toplevel : m_toplevels->toplevels()) {
        Slot *owner = nullptr;
        for (Slot &slot : m_slots) {
            const bool matches = slot.entry ? slot.entry->matchesAppId(toplevel->appId())
                                            : slot.appId == toplevel->appId();
            if (matches) {
                owner = &slot;
                break;
            }
        }
        if (!owner) {
            const auto entry = DesktopEntry::forAppId(toplevel->appId());
            if (entry) {
                for (Slot &slot : m_slots) {
                    if (slot.entry && slot.entry->id == entry->id) {
                        owner = &slot;
                        break;
                    }
                }
            }
            if (!owner) {
                // Running but not pinned: appended after the pinned apps.
                Slot slot;
                slot.entry = entry;
                slot.appId = toplevel->appId();
                m_slots.append(slot);
                owner = &m_slots.last();
                owner->button = createButton(*owner);
            }
        }
        owner->windows.append(toplevel);
        // Start lists the apps used lately: each window counts once, when its app is known.
        if (owner->entry && !toplevel->property("karyoNoted").toBool()) {
            toplevel->setProperty("karyoNoted", true);
            m_apps->noteUsed(owner->entry->id);
        }
    }

    for (qsizetype i = m_slots.size() - 1; i >= 0; --i) {
        Slot &slot = m_slots[i];
        if (!slot.pinned && slot.windows.isEmpty()) {
            slot.button->deleteLater();
            m_slots.removeAt(i);
            continue;
        }
        DockButton::State state = DockButton::State::Idle;
        for (const auto &window : std::as_const(slot.windows)) {
            if (!window)
                continue;
            state = DockButton::State::Running;
            if (window->isActive() && !window->isMinimized()) {
                state = DockButton::State::Focused;
                break;
            }
        }
        slot.button->setState(state);
    }
    layoutButtons();
}

void Taskbar::activateSlot(Slot &slot)
{
    QList<Toplevel *> windows;
    for (const auto &window : std::as_const(slot.windows)) {
        if (window)
            windows.append(window);
    }
    if (windows.isEmpty()) {
        if (slot.entry)
            slot.entry->launch();
        return;
    }
    std::sort(windows.begin(), windows.end(), [](Toplevel *a, Toplevel *b) {
        return a->activationSerial() > b->activationSerial();
    });
    Toplevel *active = nullptr;
    for (Toplevel *window : std::as_const(windows)) {
        if (window->isActive() && !window->isMinimized())
            active = window;
    }
    if (active && windows.size() == 1) {
        // A second click on the focused app minimizes it.
        active->minimizeWindow();
    } else if (active) {
        // Several windows: cycle to the least recently used one.
        windows.last()->activateWindow();
    } else {
        windows.first()->activateWindow();
    }
}

void Taskbar::showSlotMenu(Slot &slot, const QPoint &globalPos)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    DockButton *button = slot.button;

    if (slot.entry) {
        const DesktopEntry entry = *slot.entry;
        menu->addAction(slot.windows.isEmpty() ? tr("Open") : tr("New window"),
                        [entry] { entry.launch(); });
        menu->addAction(slot.pinned ? tr("Unpin from taskbar") : tr("Pin to taskbar"), this,
                        [this, button] {
                            Slot *s = slotForButton(button);
                            if (!s)
                                return;
                            s->pinned = !s->pinned;
                            savePinned();
                            syncWindows();
                        });
    }
    if (!slot.windows.isEmpty()) {
        menu->addSeparator();
        menu->addAction(slot.windows.size() > 1 ? tr("Close all windows") : tr("Close window"), this,
                        [this, button] {
                            Slot *s = slotForButton(button);
                            if (!s)
                                return;
                            for (const auto &window : std::as_const(s->windows)) {
                                if (window)
                                    window->closeWindow();
                            }
                        });
    }
    menu->popup(globalPos);
}

void Taskbar::toggleFlyout(QPointer<Flyout> &flyout, const QList<QWidget *> &buttons,
                           const std::function<Flyout *()> &create)
{
    if (flyout) {
        flyout->close();
        return;
    }
    flyout = create();
    flyout->setToggleWidgets(buttons);
    auto *shellButton = static_cast<ShellButton *>(buttons.first());
    shellButton->setHighlighted(true);
    connect(flyout, &QObject::destroyed, shellButton, [shellButton] { shellButton->setHighlighted(false); });
    flyout->show();
}

void Taskbar::showDesktopsMenu()
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);

    QProcess list;
    list.start(QStringLiteral("openkaryon-desktops"), {QStringLiteral("list"), QStringLiteral("--json")});
    const QJsonArray desktops =
        list.waitForFinished(3000) ? QJsonDocument::fromJson(list.readAllStandardOutput()).array() : QJsonArray();
    for (const QJsonValue &value : desktops) {
        const QJsonObject desktop = value.toObject();
        const QString user = desktop.value(QStringLiteral("user")).toString();
        QString text = desktop.value(QStringLiteral("label")).toString();
        if (!desktop.value(QStringLiteral("running")).toBool())
            text += QStringLiteral(" · ") + tr("not started");
        QAction *action = menu->addAction(text, this, [user] {
            QProcess::startDetached(QStringLiteral("openkaryon-desktops"), {QStringLiteral("switch"), user});
        });
        action->setCheckable(true);
        action->setChecked(desktop.value(QStringLiteral("current")).toBool());
    }
    if (!desktops.isEmpty())
        menu->addSeparator();
    menu->addAction(tr("Add desktop…"), this, [this] {
        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Add desktop"));
        dialog->setMinimumWidth(420);
        auto *layout = new QVBoxLayout(dialog);
        auto *fields = new QWidget(dialog);
        auto *form = new QFormLayout(fields);
        form->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(fields);
        auto *name = new QLineEdit(dialog);
        name->setMaxLength(160);
        name->setPlaceholderText(tr("Experiments"));
        form->addRow(tr("Desktop name"), name);
        auto *protect = new QCheckBox(tr("Require a password each time"), dialog);
        form->addRow(protect);
        auto *mode = new QComboBox(dialog);
        mode->addItem(tr("Use the Home password"), QStringLiteral("home"));
        mode->addItem(tr("Use a separate password"), QStringLiteral("separate"));
        form->addRow(tr("Password source"), mode);
        auto passwordRow = [dialog, form](const QString &label) {
            auto *row = new QWidget(dialog);
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            auto *password = new QLineEdit(row);
            password->setEchoMode(QLineEdit::Password);
            password->setMaxLength(256);
            auto *eye = new QToolButton(row);
            eye->setIcon(QIcon(Ui::icon(QStringLiteral("eye"), 18, Ui::Theme::current().text,
                                        dialog->devicePixelRatioF())));
            eye->setAccessibleName(tr("Hold to show password"));
            eye->setToolTip(eye->accessibleName());
            connect(eye, &QToolButton::pressed, password, [password] {
                password->setEchoMode(QLineEdit::Normal);
            });
            connect(eye, &QToolButton::released, password, [password] {
                password->setEchoMode(QLineEdit::Password);
            });
            layout->addWidget(password, 1);
            layout->addWidget(eye);
            form->addRow(label, row);
            return password;
        };
        auto *password = passwordRow(tr("Password"));
        auto *confirm = passwordRow(tr("Repeat password"));
        auto *status = new QLabel(dialog);
        status->setWordWrap(true);
        status->hide();
        layout->addWidget(status);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
        layout->addWidget(buttons);
        const auto separate = [protect, mode] { return protect->isChecked() && mode->currentIndex() == 1; };
        const auto refresh = [=] {
            form->setRowVisible(mode, protect->isChecked());
            form->setRowVisible(password->parentWidget(), separate());
            form->setRowVisible(confirm->parentWidget(), separate());
            if (!separate()) {
                password->clear();
                confirm->clear();
            }
        };
        const auto setCreating = [fields, buttons](bool creating) {
            fields->setEnabled(!creating);
            buttons->setEnabled(!creating);
        };
        connect(protect, &QCheckBox::toggled, dialog, refresh);
        connect(mode, &QComboBox::currentIndexChanged, dialog, refresh);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
            const QString label = name->text().trimmed();
            QString error;
            if (label.isEmpty() || label.contains(QLatin1Char(':')) || label.contains(QLatin1Char(',')))
                error = tr("Enter a desktop name without colons or commas.");
            else if (separate() && (password->text().isEmpty() || password->text() != confirm->text()))
                error = tr("Enter the same non-empty password twice.");
            if (!error.isEmpty()) {
                status->setText(error);
                status->show();
                return;
            }
            QStringList args{QStringLiteral("add"), QStringLiteral("--label"), label};
            if (protect->isChecked())
                args << QStringLiteral("--protection") << mode->currentData().toString();
            if (separate())
                args << QStringLiteral("--password-stdin");
            auto *process = new QProcess(dialog);
            connect(process, &QProcess::started, dialog, [=] {
                if (separate())
                    process->write(password->text().toUtf8() + '\n');
                password->clear();
                confirm->clear();
                process->closeWriteChannel();
            });
            connect(process, &QProcess::errorOccurred, dialog, [=](QProcess::ProcessError failure) {
                if (failure == QProcess::FailedToStart) {
                    setCreating(false);
                    status->setText(tr("Could not start the desktop setup."));
                    status->show();
                    process->deleteLater();
                }
            });
            connect(process, &QProcess::finished, dialog, [=](int code, QProcess::ExitStatus exit) {
                if (exit == QProcess::NormalExit && code == 0) {
                    dialog->accept();
                    return;
                }
                process->deleteLater();
                setCreating(false);
                status->setText(tr("Could not create the desktop. Check the name and try again."));
                status->show();
            });
            setCreating(true);
            process->start(QStringLiteral("openkaryon-desktops"), args);
        });
        refresh();
        name->setFocus();
        dialog->open();
    });
    const QSize size = menu->sizeHint();
    menu->popup(m_desktops->mapToGlobal(QPoint(0, -size.height() - 6)));
}

void Taskbar::toggleStart()
{
    QScreen *screen = windowHandle() ? windowHandle()->screen() : this->screen();
    toggleFlyout(m_startMenu, {m_start}, [this, screen] {
        return new StartMenu(m_apps, screen,
                             {[this](const QString &id) { return isAppPinned(id); },
                              [this](const QString &id, bool pinned) { setAppPinned(id, pinned); }});
    });
}

void Taskbar::toggleQuickSettings(bool network)
{
    if (auto *quick = qobject_cast<QuickSettings *>(m_quickSettings.data());
        quick && quick->isVisible() && quick->showsNetwork() != network) {
        quick->showNetwork(network);
        return;
    }
    QScreen *screen = windowHandle() ? windowHandle()->screen() : this->screen();
    toggleFlyout(m_quickSettings, {m_quick}, [this, screen, network] {
        auto *quick = new QuickSettings(m_network, m_notifications, m_nightLight, screen);
        quick->showNetwork(network);
        return quick;
    });
}

void Taskbar::toggleNotificationCenter()
{
    QScreen *screen = windowHandle() ? windowHandle()->screen() : this->screen();
    m_banner->hide();
    toggleFlyout(m_notificationCenter, {m_clock},
                 [this, screen] { return new NotificationCenter(m_notifications, screen); });
}

void Taskbar::layoutButtons()
{
    // Center: Start, desktops, then the apps; all separated by 4px.
    QList<QWidget *> center{m_start, m_desktops};
    int centerWidth = m_start->width() + Gap + m_desktops->width();
    for (const Slot &slot : std::as_const(m_slots)) {
        center.append(slot.button);
        centerWidth += Gap + slot.button->width();
    }
    int x = m_alignLeft ? SidePadding : (width() - centerWidth) / 2;
    for (QWidget *widget : std::as_const(center)) {
        // The dragged app follows the pointer; its place stays free.
        if (!(m_dragging && widget == m_pressed))
            widget->move(x, (height() - widget->height()) / 2);
        x += widget->width() + Gap;
    }

    // Right: chevron, language, quick settings, clock.
    int right = width() - SidePadding;
    for (QWidget *widget : {static_cast<QWidget *>(m_clock), static_cast<QWidget *>(m_quick),
                            static_cast<QWidget *>(m_language), static_cast<QWidget *>(m_chevron)}) {
        if (widget->isHidden())
            continue;
        right -= widget->width();
        widget->move(right, (height() - widget->height()) / 2);
        right -= Gap;
    }
}

void Taskbar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutButtons();
}

bool Taskbar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_clock && event->type() == QEvent::Resize)
        layoutButtons();
    if (watched == m_trayFlyout && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
        m_chevron->setHighlighted(event->type() == QEvent::Show);
    if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove
         || event->type() == QEvent::MouseButtonRelease)
        && dragApp(watched, static_cast<QMouseEvent *>(event)))
        return true;
    if (event->type() == QEvent::MouseButtonRelease) {
        // Middle click on an app starts another instance.
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::MiddleButton) {
            if (Slot *slot = slotForButton(watched); slot && slot->entry) {
                slot->entry->launch();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool Taskbar::dragApp(QObject *watched, QMouseEvent *event)
{
    Slot *slot = slotForButton(watched);
    if (!slot)
        return false;
    DockButton *button = slot->button;
    const QPoint pos = mapFromGlobal(event->globalPosition().toPoint());
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        if (event->button() == Qt::LeftButton) {
            m_pressed = button;
            m_pressPos = pos;
            m_grabX = pos.x() - button->x();
            m_dragging = false;
        }
        return false;
    case QEvent::MouseMove: {
        if (m_pressed != button || !(event->buttons() & Qt::LeftButton))
            return false;
        if (!m_dragging) {
            if (qAbs(pos.x() - m_pressPos.x()) < QApplication::startDragDistance())
                return false;
            m_dragging = true;
            button->setDown(false);
            button->raise();
        }
        // Between the places of the first and the last app; app buttons are one size.
        const int first = m_desktops->x() + m_desktops->width() + Gap;
        const int last = first + int(m_slots.size() - 1) * (button->width() + Gap);
        const int x = qBound(first, pos.x() - m_grabX, last);
        button->move(x, button->y());
        // The app takes the place of the one whose middle it has passed.
        const qsizetype from = std::distance(m_slots.cbegin(),
                                             std::find_if(m_slots.cbegin(), m_slots.cend(),
                                                          [button](const Slot &s) { return s.button == button; }));
        const int middle = x + button->width() / 2;
        qsizetype to = 0;
        for (qsizetype i = 0; i < m_slots.size(); ++i) {
            if (i == from)
                continue;
            const DockButton *other = m_slots.at(i).button;
            if (middle > other->x() + other->width() / 2)
                to = i < from ? i + 1 : i;
        }
        if (to != from) {
            m_slots.move(from, to);
            layoutButtons();
        }
        return true;
    }
    case QEvent::MouseButtonRelease:
        if (event->button() != Qt::LeftButton || m_pressed != button)
            return false;
        m_pressed = nullptr;
        if (!m_dragging)
            return false;
        m_dragging = false;
        button->setDown(false);
        layoutButtons();
        savePinned();
        return true;
    default:
        return false;
    }
}

void Taskbar::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    // Replace, not blend: repaints of a translucent surface must not stack the glass.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    // Without transparency effects the glass is as it reads over the desktop.
    QColor fill = t.glass;
    if (m_opaque) {
        const qreal coverage = fill.alphaF() * 100;
        fill.setAlpha(255);
        fill = Ui::mix(fill, t.desk, coverage);
    }
    painter.fillRect(rect(), fill);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.fillRect(QRect(0, 0, width(), 1), t.edge);
}
