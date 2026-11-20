// SPDX-License-Identifier: GPL-3.0-or-later
#include "notificationcenter.h"

#include "notificationcard.h"
#include "notificationserver.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QAbstractButton>
#include <QDate>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

constexpr int ColumnWidth = 420;
constexpr int EdgeGap = 12;
constexpr int TaskbarHeight = 48;
constexpr int CardRadius = 8;
constexpr int FocusMinutes = 30;

// Accent text button: "Clear all".
class TextButton : public QAbstractButton
{
public:
    TextButton(const QString &text, QWidget *parent)
        : QAbstractButton(parent)
    {
        setText(text);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(QFontMetrics(Ui::bodyFont(13, QFont::DemiBold)).horizontalAdvance(text) + 16, 30);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (underMouse())
            Ui::fillPill(painter, rect(), t.hover);
        painter.setFont(Ui::bodyFont(13, QFont::DemiBold));
        painter.setPen(isEnabled() ? t.ink1 : t.muted);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
};

// Month grid, weeks starting on the locale's first day; today in an accent circle.
class CalendarView : public QWidget
{
public:
    explicit CalendarView(QWidget *parent)
        : QWidget(parent)
        , m_month(QDate::currentDate().year(), QDate::currentDate().month(), 1)
    {
        setFixedHeight(HeaderHeight + WeekdayHeight + 6 * RowHeight);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        const QLocale locale;
        const QDate today = QDate::currentDate();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QString title;
        if (m_month.year() == today.year() && m_month.month() == today.month()) {
            title = locale.toString(today, locale.language() == QLocale::English ? QStringLiteral("dddd, MMMM d")
                                                                                  : QStringLiteral("dddd, d MMMM"));
        } else {
            title = locale.standaloneMonthName(m_month.month()) + QLatin1Char(' ') + QString::number(m_month.year());
        }
        if (!title.isEmpty())
            title[0] = title.at(0).toUpper();
        painter.setFont(Ui::bodyFont(14, QFont::DemiBold));
        painter.setPen(t.text);
        painter.drawText(QRect(4, 0, width() - 90, HeaderHeight), Qt::AlignVCenter | Qt::AlignLeft, title);
        const qreal dpr = devicePixelRatioF();
        for (int i = 0; i < 2; ++i) {
            const QRect r = arrowRect(i);
            if (m_hoverArrow == i)
                Ui::fillPill(painter, r, t.hover);
            painter.drawPixmap(r.x() + 7, r.y() + 7,
                               Ui::icon(i == 0 ? QStringLiteral("chevron-up") : QStringLiteral("chevron-down"), 16,
                                        t.text, dpr));
        }

        const int columnWidth = width() / 7;
        const Qt::DayOfWeek first = locale.firstDayOfWeek();
        painter.setFont(Ui::bodyFont(12, QFont::DemiBold));
        painter.setPen(t.muted);
        for (int c = 0; c < 7; ++c) {
            const int day = (int(first) - 1 + c) % 7 + 1;
            QString name = locale.dayName(day, QLocale::ShortFormat).left(2);
            if (!name.isEmpty())
                name[0] = name.at(0).toUpper();
            painter.drawText(QRect(c * columnWidth, HeaderHeight, columnWidth, WeekdayHeight), Qt::AlignCenter, name);
        }

        const int offset = (m_month.dayOfWeek() - int(first) + 7) % 7;
        const QDate start = m_month.addDays(-offset);
        for (int i = 0; i < 42; ++i) {
            const QDate date = start.addDays(i);
            const QRect cell((i % 7) * columnWidth, HeaderHeight + WeekdayHeight + (i / 7) * RowHeight, columnWidth,
                             RowHeight);
            if (date == today) {
                Ui::fillPill(painter, QRectF(cell.center().x() - 17.5, cell.center().y() - 17.5, 36, 36), t.accent);
                painter.setFont(Ui::bodyFont(13, QFont::Bold));
                painter.setPen(Ui::neutral(100));
            } else {
                painter.setFont(Ui::bodyFont(13));
                painter.setPen(date.month() == m_month.month() ? t.text : t.muted);
            }
            painter.drawText(cell, Qt::AlignCenter, QString::number(date.day()));
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        int hover = -1;
        for (int i = 0; i < 2; ++i) {
            if (arrowRect(i).contains(event->position().toPoint()))
                hover = i;
        }
        if (hover != m_hoverArrow) {
            m_hoverArrow = hover;
            update();
        }
    }

    void leaveEvent(QEvent *) override
    {
        m_hoverArrow = -1;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        for (int i = 0; i < 2; ++i) {
            if (arrowRect(i).contains(event->position().toPoint())) {
                m_month = m_month.addMonths(i == 0 ? -1 : 1);
                update();
            }
        }
    }

private:
    static constexpr int HeaderHeight = 44;
    static constexpr int WeekdayHeight = 30;
    static constexpr int RowHeight = 40;

    QRect arrowRect(int index) const { return QRect(width() - 76 + index * 38, (HeaderHeight - 30) / 2, 30, 30); }

    QDate m_month;
    int m_hoverArrow = -1;
};

// "Focus · 30 min" and Start: Do not disturb for a while.
class FocusRow : public QWidget
{
public:
    FocusRow(NotificationServer *server, QWidget *parent)
        : QWidget(parent)
        , m_server(server)
    {
        setFixedHeight(52);
        setMouseTracking(true);
        connect(server, &NotificationServer::doNotDisturbChanged, this, qOverload<>(&QWidget::update));
        auto *tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, qOverload<>(&QWidget::update));
        tick->start(30 * 1000);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(painter, rect(), 8, t.card);
        painter.drawPixmap(16, (height() - 18) / 2, Ui::icon(QStringLiteral("bell-off"), 18, t.text, devicePixelRatioF()));

        QString label;
        const QDateTime until = m_server->focusUntil();
        if (m_server->doNotDisturb() && until.isValid()) {
            const int left = qMax(1, int((QDateTime::currentDateTime().secsTo(until) + 59) / 60));
            label = NotificationCenter::tr("Focus · %n min left", nullptr, left);
        } else if (m_server->doNotDisturb()) {
            label = NotificationCenter::tr("Do not disturb is on");
        } else {
            label = NotificationCenter::tr("Focus · %1 min").arg(FocusMinutes);
        }
        painter.setFont(Ui::bodyFont(13, QFont::DemiBold));
        painter.setPen(t.text);
        painter.drawText(QRect(46, 0, width() - 46 - 100, height()), Qt::AlignVCenter | Qt::AlignLeft, label);

        const QRect button = buttonRect();
        const bool active = m_server->doNotDisturb();
        Ui::fillRounded(painter, QRectF(button).adjusted(0.5, 0.5, -0.5, -0.5), button.height() / 2.0,
                        m_buttonHover ? t.hover : Ui::alpha(Ui::neutral(100), 70), Ui::alpha(t.text, 14));
        painter.drawPixmap(button.x() + 12, button.center().y() - 7,
                           Ui::icon(active ? QStringLiteral("square") : QStringLiteral("play"), 14, t.text,
                                    devicePixelRatioF()));
        painter.setFont(Ui::headingFont(13));
        painter.drawText(button.adjusted(32, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         active ? NotificationCenter::tr("Stop") : NotificationCenter::tr("Start"));
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const bool hover = buttonRect().contains(event->position().toPoint());
        if (hover != m_buttonHover) {
            m_buttonHover = hover;
            update();
        }
    }

    void leaveEvent(QEvent *) override
    {
        m_buttonHover = false;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!buttonRect().contains(event->position().toPoint()))
            return;
        if (m_server->doNotDisturb())
            m_server->setDoNotDisturb(false);
        else
            m_server->startFocus(FocusMinutes);
    }

private:
    QRect buttonRect() const { return QRect(width() - 10 - 86, (height() - 32) / 2, 86, 32); }

    NotificationServer *m_server;
    bool m_buttonHover = false;
};

} // namespace

NotificationCenter::NotificationCenter(NotificationServer *server, QScreen *screen)
    : Flyout(screen)
    , m_server(server)
    , m_listCard(new QWidget(this))
    , m_calendarCard(new QWidget(this))
    , m_list(new QVBoxLayout)
    , m_empty(new QLabel(tr("No new notifications"), this))
{
    setWindowTitle(QStringLiteral("karyo-notifications"));
    const QMarginsF shadow = Ui::largeShadowMargins();
    setFixedWidth(ColumnWidth + EdgeGap + int(shadow.left()));
    resize(width(), screen->geometry().height() - TaskbarHeight);
    place(LayerSurface::AnchorTop | LayerSurface::AnchorBottom | LayerSurface::AnchorRight,
          QMargins(0, 0, 0, TaskbarHeight));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(int(shadow.left()), EdgeGap, EdgeGap, EdgeGap);
    outer->setSpacing(14);
    outer->addWidget(m_listCard, 1);
    outer->addWidget(m_calendarCard);

    auto *listLayout = new QVBoxLayout(m_listCard);
    listLayout->setContentsMargins(20, 20, 20, 20);
    listLayout->setSpacing(14);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(4, 0, 0, 0);
    auto *title = new QLabel(tr("Notifications"), m_listCard);
    title->setFont(Ui::headingFont(21));
    QPalette palette = title->palette();
    palette.setColor(QPalette::WindowText, Ui::Theme::current().text);
    title->setPalette(palette);
    header->addWidget(title);
    header->addStretch(1);
    auto *clear = new TextButton(tr("Clear all"), m_listCard);
    connect(clear, &QAbstractButton::clicked, m_server, &NotificationServer::dismissAll);
    header->addWidget(clear);
    listLayout->addLayout(header);

    auto *content = new QWidget;
    content->setAutoFillBackground(false);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    m_list->setSpacing(10);
    contentLayout->addLayout(m_list);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setFont(Ui::bodyFont(13));
    QPalette muted = m_empty->palette();
    muted.setColor(QPalette::WindowText, Ui::Theme::current().muted);
    m_empty->setPalette(muted);
    m_empty->setMinimumHeight(120);
    contentLayout->addWidget(m_empty);
    contentLayout->addStretch(1);
    auto *scroll = new QScrollArea(m_listCard);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setWidget(content);
    content->setAutoFillBackground(false);
    listLayout->addWidget(scroll, 1);

    auto *calendarLayout = new QVBoxLayout(m_calendarCard);
    calendarLayout->setContentsMargins(20, 14, 20, 20);
    calendarLayout->setSpacing(10);
    calendarLayout->addWidget(new CalendarView(m_calendarCard));
    calendarLayout->addWidget(new FocusRow(m_server, m_calendarCard));

    // Queued: a card's own button can change the list, and the card must not
    // be deleted while it handles the click.
    connect(m_server, &NotificationServer::changed, this, &NotificationCenter::rebuild, Qt::QueuedConnection);
    rebuild();
}

void NotificationCenter::rebuild()
{
    while (QLayoutItem *item = m_list->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<Notification> &all = m_server->notifications();
    m_empty->setVisible(all.isEmpty());
    for (const Notification &notification : all) {
        auto *card = new NotificationCard(notification, m_server, this);
        const uint id = notification.id;
        connect(card, &NotificationCard::dismissed, this, [this, id] { m_server->dismiss(id); });
        connect(card, &NotificationCard::used, this, &QWidget::close);
        m_list->addWidget(card);
    }
}

void NotificationCenter::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    paintCard(painter, m_listCard->geometry(), CardRadius);
    paintCard(painter, m_calendarCard->geometry(), CardRadius);
}

void NotificationCenter::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (!m_listCard->geometry().contains(pos) && !m_calendarCard->geometry().contains(pos)) {
        close();
        return;
    }
    Flyout::mousePressEvent(event);
}
