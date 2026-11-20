// SPDX-License-Identifier: GPL-3.0-or-later
#include "trayflyout.h"

#include "buttons.h"
#include "tray/trayhost.h"
#include "tray/trayitem.h"

#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>

namespace Ui = Karyo::Ui;

namespace {

constexpr int CardWidth = 204;
constexpr int Padding = 10;
constexpr int Gap = 4;
constexpr int TileHeight = 52;
constexpr int Columns = 3;

class TrayTile : public ShellButton
{
public:
    TrayTile(TrayItem *item, TrayFlyout *flyout)
        : ShellButton(flyout)
        , m_item(item)
        , m_flyout(flyout)
    {
        setFixedHeight(TileHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setContextMenuPolicy(Qt::PreventContextMenu);
        refreshHint();
        connect(item, &TrayItem::changed, this, [this] {
            refreshHint();
            update();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        paintBackground(painter, 16);
        if (m_item)
            m_item->icon().paint(&painter, QRect((width() - 20) / 2, (height() - 20) / 2, 20, 20));
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool inside = rect().contains(event->position().toPoint());
        if (event->button() == Qt::RightButton) {
            const bool pressed = m_contextPressed;
            m_contextPressed = false;
            setDown(false);
            event->accept();
            // Keep the Wayland parent popup alive while its child menu owns
            // the grab. Opening on release also keeps that release out of the
            // new menu, where it could immediately dismiss it.
            if (pressed && inside && m_item) {
                if (m_item->isNetworkApplet())
                    openNetwork();
                else
                    m_item->showContextMenu(event->globalPosition().toPoint(), window());
            }
            return;
        }
        const bool pressed = isDown();
        ShellButton::mouseReleaseEvent(event);
        if (!inside || !m_item)
            return;
        const QPoint global = event->globalPosition().toPoint();
        if (event->button() == Qt::LeftButton && pressed) {
            if (m_item->isNetworkApplet()) {
                openNetwork();
                return;
            }
            QWidget *flyout = window();
            if (m_item->activationOpensMenu()) {
                m_item->activate(global, flyout);
                return;
            }
            flyout->close();
            m_item->activate(global, flyout->parentWidget());
        } else if (event->button() == Qt::MiddleButton) {
            m_item->secondaryActivate(global);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton && m_item) {
            m_contextPressed = true;
            setDown(true);
            event->accept();
            return;
        }
        ShellButton::mousePressEvent(event);
    }

private:
    void openNetwork()
    {
        m_flyout->close();
        Q_EMIT m_flyout->networkRequested();
    }

    void refreshHint()
    {
        if (m_item)
            setHint(m_item->toolTipTitle(), m_item->toolTipText());
    }

    QPointer<TrayItem> m_item;
    TrayFlyout *m_flyout;
    bool m_contextPressed = false;
};

} // namespace

TrayFlyout::TrayFlyout(TrayHost *host, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    , m_host(host)
    , m_grid(new QGridLayout(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    const QMarginsF shadow = Ui::largeShadowMargins();
    m_grid->setContentsMargins(int(shadow.left()) + Padding, int(shadow.top()) + Padding,
                               int(shadow.right()) + Padding, int(shadow.bottom()) + Padding);
    m_grid->setSpacing(Gap);
    connect(host, &TrayHost::itemsChanged, this, &TrayFlyout::rebuild);
    rebuild();
}

void TrayFlyout::rebuild()
{
    qDeleteAll(m_tiles);
    m_tiles.clear();
    const QList<TrayItem *> items = m_host->visibleItems();
    if (items.isEmpty()) {
        auto *label = new QLabel(tr("No background apps"), this);
        label->setAlignment(Qt::AlignCenter);
        label->setFont(Ui::bodyFont(12.5));
        QPalette palette = label->palette();
        palette.setColor(QPalette::WindowText, Ui::Theme::current().muted);
        label->setPalette(palette);
        label->setFixedHeight(TileHeight);
        m_grid->addWidget(label, 0, 0, 1, Columns);
        m_tiles.append(label);
    }
    for (qsizetype i = 0; i < items.size(); ++i) {
        auto *tile = new TrayTile(items.at(i), this);
        m_grid->addWidget(tile, int(i / Columns), int(i % Columns));
        m_tiles.append(tile);
    }
    const int rows = qMax(1, int((items.size() + Columns - 1) / Columns));
    const QMarginsF shadow = Ui::largeShadowMargins();
    setFixedSize(int(shadow.left() + shadow.right()) + CardWidth,
                 int(shadow.top() + shadow.bottom()) + 2 * Padding + rows * TileHeight
                     + (rows - 1) * Gap);
}

QRect TrayFlyout::cardRect() const
{
    const QMarginsF shadow = Ui::largeShadowMargins();
    return rect().marginsRemoved(shadow.toMargins());
}

void TrayFlyout::popupAbove(QWidget *anchor)
{
    const QMarginsF shadow = Ui::largeShadowMargins();
    const QPoint anchorBottom = anchor->mapToGlobal(QPoint(anchor->width() / 2, anchor->height()));
    const int cardHeight = cardRect().height();
    move(anchorBottom.x() - CardWidth / 2 - int(shadow.left()),
         anchorBottom.y() - 60 - cardHeight - int(shadow.top()));
    show();
}

void TrayFlyout::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QRectF card = cardRect();
    Ui::drawLargeShadow(painter, card, 24);
    Ui::fillRounded(painter, card, 8, t.glassStrong, t.edge);
}

void TrayFlyout::mousePressEvent(QMouseEvent *event)
{
    // The shadow margin belongs to the popup surface but reads as outside.
    if (!cardRect().contains(event->position().toPoint())) {
        close();
        return;
    }
    QWidget::mousePressEvent(event);
}
