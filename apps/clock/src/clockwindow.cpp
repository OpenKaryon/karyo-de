// SPDX-License-Identifier: GPL-3.0-or-later
#include "clockwindow.h"

#include "alarm.h"
#include "clockformat.h"
#include "stopwatchpage.h"
#include "timers.h"
#include "timerspage.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;
using State = TimerModel::State;

namespace {

// The app's 30px tile with the hourglass.
class AppTile : public QWidget
{
public:
    AppTile() { setFixedSize(30, 30); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(painter, rect(), 9, Ui::accent(600));
        painter.drawPixmap(6, 6, Ui::icon(QStringLiteral("hourglass"), 18, Ui::neutral(100), devicePixelRatioF()));
    }
};

} // namespace

ClockWindow::ClockWindow(QWidget *parent)
    : KaryoWindow(parent)
    , m_model(nullptr)
    , m_nav(new Ui::NavList)
{
    const Ui::Theme &t = Ui::Theme::current();
    setWindowTitle(tr("Clock"));
    setBodyColor(t.pane);
    auto *title = new QLabel(tr("Clock"));
    title->setFont(Ui::bodyFont(14, QFont::DemiBold));
    titleLayout()->setContentsMargins(20, 0, 8, 0);
    titleLayout()->setSpacing(10);
    titleLayout()->addWidget(new AppTile);
    titleLayout()->addWidget(title);
    titleLayout()->addStretch(1);

    const QString path = TimerModel::defaultPath();
    const bool firstStart = !QFile::exists(path);
    m_model = new TimerModel(path, {}, this);
    if (firstStart)
        m_model->addSamples();

    auto *body = new QWidget;
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(12, 0, 12, 12);
    bodyLayout->setSpacing(12);
    auto *navColumn = new QVBoxLayout;
    navColumn->setContentsMargins(0, 4, 0, 10);
    m_nav->setFixedWidth(200);
    const int timersIndex = m_nav->addItem(QStringLiteral("hourglass"), tr("Timers"));
    m_stopwatchIndex = m_nav->addItem(QStringLiteral("timer"), tr("Stopwatch"));
    navColumn->addWidget(m_nav);
    navColumn->addStretch();
    bodyLayout->addLayout(navColumn);

    auto *panel = new Ui::InsetPanel;
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    auto *stack = new QStackedWidget;
    m_timers = new TimersPage(m_model);
    m_stopwatch = new StopwatchPage;
    stack->addWidget(m_timers);
    stack->addWidget(m_stopwatch);
    panelLayout->addWidget(stack);
    bodyLayout->addWidget(panel, 1);
    connect(m_nav, &Ui::NavList::currentChanged, stack, [=](int index) {
        stack->setCurrentWidget(index == m_stopwatchIndex ? static_cast<QWidget *>(m_stopwatch) : m_timers);
    });
    m_nav->setCurrentIndex(timersIndex);
    setCentralWidget(body);
    resizeToFit({1060, 700});

    connect(m_model, &TimerModel::finished, this, &ClockWindow::onFinished);
    connect(m_model, &TimerModel::changed, this, &ClockWindow::updateTray);
    connect(m_timers, &TimersPage::stopRequested, this, [this](const QString &id) { stopAlarm(id, true); });
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        m_model->tick();
        if (isVisible())
            m_timers->refresh();
        if (m_tray && m_tray->isVisible())
            updateTray();
    });
    m_tick.start();
}

bool ClockWindow::busy() const { return m_model->anyRunning() || m_stopwatch->isRunning() || !m_alarms.isEmpty(); }

void ClockWindow::onFinished(const QString &id)
{
    const int index = m_model->indexOf(id);
    if (index < 0 || m_alarms.contains(id))
        return;
    const TimerModel::Timer &timer = m_model->timers().at(index);
    // A timer that ran out long ago while Clock was closed only says so.
    const bool ring = QDateTime::currentMSecsSinceEpoch() - timer.finishedAt < 2 * 60 * 1000;
    auto *alarm = new Alarm(timer.name, timer.finishedAt, ring, this);
    m_alarms.insert(id, alarm);
    connect(alarm, &Alarm::restartRequested, this, [this, id] {
        stopAlarm(id, false);
        if (m_model->indexOf(id) >= 0)
            m_model->start(m_model->indexOf(id));
    });
    connect(alarm, &Alarm::dismissed, this, [this, id] { stopAlarm(id, true); });
    connect(alarm, &Alarm::showRequested, this, [this, id] {
        stopAlarm(id, false);
        bringBack();
    });
    updateTray();
}

void ClockWindow::stopAlarm(const QString &id, bool resetTimer)
{
    if (Alarm *alarm = m_alarms.take(id)) {
        alarm->stop();
        alarm->deleteLater();
    }
    const int index = m_model->indexOf(id);
    if (resetTimer && index >= 0 && m_model->timers().at(index).state == State::Done)
        m_model->reset(index);
    updateTray();
}

void ClockWindow::bringBack()
{
    show();
    raise();
    activateWindow();
    updateTray();
}

void ClockWindow::showStopwatch()
{
    m_nav->setCurrentIndex(m_stopwatchIndex);
}

void ClockWindow::updateTray()
{
    if (isVisible()) {
        if (m_tray)
            m_tray->hide();
        return;
    }
    // Hidden with nothing left to do: Clock ends.
    if (!busy()) {
        QApplication::quit();
        return;
    }
    if (!m_tray) {
        m_tray = new QSystemTrayIcon(this);
        m_tray->setIcon(QIcon(Ui::icon(QStringLiteral("hourglass"), 32, Ui::Theme::current().text, 1.0)));
        auto *menu = new QMenu(this);
        Ui::styleMenu(menu);
        menu->addAction(tr("Show Clock"), this, &ClockWindow::bringBack);
        menu->addAction(tr("Quit"), qApp, &QApplication::quit);
        m_tray->setContextMenu(menu);
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason != QSystemTrayIcon::Context)
                bringBack();
        });
    }
    QStringList lines;
    for (int i = 0; i < m_model->timers().size(); ++i) {
        const TimerModel::Timer &t = m_model->timers().at(i);
        if (t.state == State::Running)
            lines.append(t.name + QStringLiteral(" · ") + ClockFormat::timer(m_model->remaining(i)));
        else if (t.state == State::Done && m_alarms.contains(t.id))
            lines.append(t.name + QStringLiteral(" · ") + tr("done"));
    }
    if (m_stopwatch->isRunning())
        lines.append(tr("Stopwatch running"));
    m_tray->setToolTip(tr("Clock") + QLatin1Char('\n') + lines.join(QLatin1Char('\n')));
    m_tray->show();
}

void ClockWindow::closeEvent(QCloseEvent *event)
{
    if (!busy()) {
        event->accept();
        QApplication::quit();
        return;
    }
    // Timers go on in the background.
    event->ignore();
    hide();
    updateTray();
    if (!m_toldBackground) {
        m_toldBackground = true;
        QDBusInterface notifications(QStringLiteral("org.freedesktop.Notifications"),
                                     QStringLiteral("/org/freedesktop/Notifications"),
                                     QStringLiteral("org.freedesktop.Notifications"), QDBusConnection::sessionBus());
        notifications.call(QDBus::NoBlock, QStringLiteral("Notify"), tr("Clock"), 0u,
                           QStringLiteral("karyo-clock"), tr("Your timers keep running"),
                           tr("Clock tells you when they finish. Open it again from Start or the tray."),
                           QStringList(),
                           QVariantMap{{QStringLiteral("desktop-entry"), QStringLiteral("karyo-clock")}}, -1);
    }
}

ClockService::ClockService(ClockWindow *window)
    : QObject(window)
    , m_window(window)
{
}

void ClockService::Show(const QString &mode)
{
    if (mode == QLatin1String("stopwatch"))
        m_window->showStopwatch();
    m_window->bringBack();
}
