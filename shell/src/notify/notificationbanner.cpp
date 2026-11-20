// SPDX-License-Identifier: GPL-3.0-or-later
#include "notificationbanner.h"

#include "flyout.h"
#include "layersurface.h"
#include "notificationcard.h"

#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QPainter>

namespace Ui = Karyo::Ui;

namespace {

constexpr int CardWidth = 380;
constexpr int EdgeGap = 12;
constexpr int TaskbarHeight = 48;
constexpr int ShowMs = 6000;

} // namespace

NotificationBanner::NotificationBanner(NotificationServer *server, QScreen *screen)
    : QWidget(nullptr, Qt::FramelessWindowHint)
    , m_server(server)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("karyo-notification"));
    const QMarginsF shadow = Ui::largeShadowMargins();
    setFixedWidth(CardWidth + EdgeGap + int(shadow.left()));
    LayerSurface::configure(this, screen, LayerSurface::LayerOverlay,
                            LayerSurface::AnchorBottom | LayerSurface::AnchorRight, -1);
    LayerSurface::setMargins(this, QMargins(0, 0, 0, TaskbarHeight));
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &QWidget::hide);
}

QRect NotificationBanner::cardRect() const
{
    const QMarginsF shadow = Ui::largeShadowMargins();
    return QRect(int(shadow.left()), int(shadow.top()), CardWidth, height() - int(shadow.top()) - EdgeGap);
}

void NotificationBanner::showNotification(const Notification &notification)
{
    delete m_card;
    m_card = new NotificationCard(notification, m_server, this);
    connect(m_card, &NotificationCard::dismissed, this, &QWidget::hide);
    connect(m_card, &NotificationCard::used, this, &QWidget::hide);
    const QMarginsF shadow = Ui::largeShadowMargins();
    const int cardHeight = m_card->heightForWidth(CardWidth);
    setFixedHeight(int(shadow.top()) + cardHeight + EdgeGap);
    m_card->setGeometry(cardRect());
    m_card->show();
    m_sticky = notification.critical || notification.timeout == 0;
    if (m_sticky)
        m_timer.stop();
    else
        m_timer.start(notification.timeout > 0 ? qMax(notification.timeout, 2000) : ShowMs);
    show();
    update();
}

void NotificationBanner::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    Flyout::paintCard(painter, cardRect(), 24);
}

void NotificationBanner::enterEvent(QEnterEvent *)
{
    m_timer.stop();
}

void NotificationBanner::leaveEvent(QEvent *)
{
    if (!m_sticky && isVisible())
        m_timer.start(ShowMs / 2);
}
