// SPDX-License-Identifier: GPL-3.0-or-later
#include "notificationcard.h"

#include "timeformat.h"

#include <karyo/ui/appstyle.h>
#include <karyo/ui/desktopentry.h>
#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QAbstractButton>
#include <QMouseEvent>
#include <QPainter>

namespace Ui = Karyo::Ui;

namespace {

constexpr int Padding = 18;
constexpr int HeaderHeight = 24;
constexpr int TitleHeight = 20;
constexpr int BodyLine = 18;
constexpr int MaxBodyLines = 3;
constexpr int ButtonHeight = 32;

// Notification action: the first is primary (accent), the others outlined.
class ActionPill : public QAbstractButton
{
public:
    ActionPill(const QString &text, bool primary, QWidget *parent)
        : QAbstractButton(parent)
        , m_primary(primary)
    {
        setText(text);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(QFontMetrics(labelFont()).horizontalAdvance(text) + 34, ButtonHeight);
    }

    QFont labelFont() const { return m_primary ? Ui::headingFont(13.5) : Ui::bodyFont(13, QFont::DemiBold); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor ink = t.text;
        if (m_primary) {
            Ui::fillPill(painter, r, isDown() ? Ui::accent(700) : underMouse() ? Ui::accent(600) : t.accent);
            ink = Ui::neutral(100);
        } else {
            Ui::fillRounded(painter, r, r.height() / 2,
                            isDown() ? t.press : underMouse() ? t.hover : Ui::alpha(Ui::neutral(100), 70),
                            Ui::alpha(t.text, 14));
        }
        painter.setFont(labelFont());
        painter.setPen(ink);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    bool m_primary;
};

QFont bodyFont() { return Ui::bodyFont(12.5); }

} // namespace

NotificationCard::NotificationCard(const Notification &notification, NotificationServer *server,
                                   QWidget *parent)
    : QWidget(parent)
    , m_notification(notification)
    , m_server(server)
    , m_appName(notification.appName)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);

    if (!m_notification.desktopEntry.isEmpty()) {
        if (const auto entry = DesktopEntry::find(m_notification.desktopEntry + QStringLiteral(".desktop")))
            m_appName = entry->name;
    }
    if (m_appName.isEmpty())
        m_appName = tr("Notification");

    // Actions come as key, label pairs; "default" is the card itself.
    const QStringList &actions = m_notification.actions;
    for (qsizetype i = 0; i + 1 < actions.size(); i += 2) {
        if (actions.at(i) == QLatin1String("default"))
            continue;
        auto *button = new ActionPill(actions.at(i + 1), m_buttons.isEmpty(), this);
        const QString key = actions.at(i);
        connect(button, &QAbstractButton::clicked, this, [this, key] {
            m_server->invoke(m_notification.id, key);
            Q_EMIT used();
        });
        m_buttons.append(button);
    }
}

int NotificationCard::bodyHeight(int width) const
{
    if (m_notification.body.isEmpty())
        return 0;
    const QRect bounds = QFontMetrics(bodyFont())
                             .boundingRect(QRect(0, 0, width - 2 * Padding, 10000), Qt::TextWordWrap,
                                           m_notification.body);
    return qMin(bounds.height(), MaxBodyLines * BodyLine) + 2;
}

int NotificationCard::heightForWidth(int width) const
{
    int h = Padding + HeaderHeight + 8 + TitleHeight + bodyHeight(width);
    if (!m_buttons.isEmpty())
        h += 12 + ButtonHeight;
    return h + Padding;
}

QSize NotificationCard::sizeHint() const { return {380, heightForWidth(380)}; }

QRect NotificationCard::closeRect() const { return QRect(width() - Padding - 22, Padding, 24, 24); }

void NotificationCard::resizeEvent(QResizeEvent *)
{
    setFixedHeight(heightForWidth(width()));
    placeButtons();
}

void NotificationCard::placeButtons()
{
    int x = Padding;
    const int y = height() - Padding - ButtonHeight;
    for (QAbstractButton *button : std::as_const(m_buttons)) {
        button->move(x, y);
        x += button->width() + 8;
    }
}

void NotificationCard::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Ui::fillRounded(painter, rect(), 8, m_hover ? Ui::mix(t.text, t.card, 4) : t.card);

    // Header: app tile, app name, time (the close button on hover).
    const QRectF tile(Padding, Padding + 1, 22, 22);
    std::optional<DesktopEntry> entry;
    if (!m_notification.desktopEntry.isEmpty())
        entry = DesktopEntry::find(m_notification.desktopEntry + QStringLiteral(".desktop"));
    if (entry) {
        AppStyle::forEntry(*entry).paint(painter, tile, devicePixelRatioF());
    } else if (!m_notification.image.isNull()) {
        m_notification.image.paint(&painter, tile.toRect());
    } else {
        Ui::fillRounded(painter, tile, 7, Ui::neutral(800));
        painter.drawPixmap(QPointF(tile.x() + 3, tile.y() + 3),
                           Ui::icon(QStringLiteral("bell-off"), 16, Ui::neutral(100), devicePixelRatioF()));
    }
    const int x = Padding + 22 + 10;
    if (m_hover) {
        Ui::fillPill(painter, closeRect(), m_closeHover ? t.hover : Qt::transparent);
        painter.drawPixmap(closeRect().x() + 4, closeRect().y() + 4,
                           Ui::icon(QStringLiteral("x"), 16, t.muted, devicePixelRatioF()));
    } else {
        painter.setFont(Ui::bodyFont(12));
        painter.setPen(t.muted);
        painter.drawText(QRect(x, Padding, width() - x - Padding, HeaderHeight), Qt::AlignVCenter | Qt::AlignRight,
                         ShellTime::relativeTime(m_notification.time));
    }
    painter.setFont(Ui::bodyFont(13));
    painter.setPen(t.text);
    painter.drawText(QRect(x, Padding, width() - x - Padding - 80, HeaderHeight), Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetrics(painter.font()).elidedText(m_appName, Qt::ElideRight, width() - x - Padding - 80));

    int y = Padding + HeaderHeight + 8;
    const QFont title = Ui::bodyFont(13.5, QFont::DemiBold);
    painter.setFont(title);
    painter.setPen(t.text);
    painter.drawText(QRect(Padding, y, width() - 2 * Padding, TitleHeight), Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetrics(title).elidedText(m_notification.summary, Qt::ElideRight, width() - 2 * Padding));
    y += TitleHeight;
    if (!m_notification.body.isEmpty()) {
        painter.setFont(bodyFont());
        painter.setPen(t.muted);
        painter.drawText(QRect(Padding, y + 2, width() - 2 * Padding, bodyHeight(width()) - 2),
                         Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, m_notification.body);
    }
}

void NotificationCard::mouseMoveEvent(QMouseEvent *event)
{
    const bool closeHover = closeRect().contains(event->position().toPoint());
    if (closeHover != m_closeHover) {
        m_closeHover = closeHover;
        update();
    }
}

void NotificationCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (closeRect().contains(event->position().toPoint())) {
        Q_EMIT dismissed();
        return;
    }
    if (m_notification.actions.contains(QStringLiteral("default"))) {
        m_server->invoke(m_notification.id, QStringLiteral("default"));
        Q_EMIT used();
    }
}

void NotificationCard::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void NotificationCard::leaveEvent(QEvent *)
{
    m_hover = false;
    m_closeHover = false;
    update();
}
