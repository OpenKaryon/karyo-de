// SPDX-License-Identifier: GPL-3.0-or-later
#include "overlay.h"

#include "capture.h"

#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QWindow>
#include <QLabel>

namespace Ui = Karyo::Ui;

namespace {

// The glass pill holding the modes.
class Toolbar : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), height() / 2.0, t.glassStrong, t.edge);
    }
};

} // namespace

SnipOverlay::SnipOverlay(Snipper *snipper, QScreen *screen, const QImage &frozen)
    // Frameless from the start: no Karyo client frame on a layer surface.
    : QWidget(nullptr, Qt::FramelessWindowHint)
    , m_snipper(snipper)
    , m_frozen(frozen)
    , m_toolbar(new Toolbar(this))
    , m_screen(screen)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    winId();
    QWindow *window = windowHandle();
    window->setScreen(screen);
    auto *layer = LayerShellQt::Window::get(window);
    layer->setScope(QStringLiteral("karyo-snip"));
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                    | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    layer->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
    resize(screen->size());

    auto *row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(6, 6, 6, 6);
    row->setSpacing(2);
    auto *region = new Ui::IconButton(QStringLiteral("scan"), m_toolbar, {40, 40}, 18);
    region->setHint(tr("Rectangle"));
    region->setActive(true);
    auto *full = new Ui::IconButton(QStringLiteral("monitor"), m_toolbar, {40, 40}, 18);
    full->setHint(tr("Full screen"));
    connect(full, &QAbstractButton::clicked, this, [this] { m_snipper->finish(m_frozen); });
    auto *close = new Ui::IconButton(QStringLiteral("x"), m_toolbar, {40, 40}, 18);
    close->setHint(tr("Cancel (Esc)"));
    connect(close, &QAbstractButton::clicked, m_snipper, &Snipper::cancel);
    row->addWidget(region);
    row->addWidget(full);
    if (snipper->windowMode()) {
        region->hide();
        full->hide();
        row->addWidget(new QLabel(tr("Click a window · Esc to cancel"), m_toolbar));
    }
    row->addSpacing(6);
    row->addWidget(close);
    m_toolbar->adjustSize();
    m_toolbar->move((width() - m_toolbar->width()) / 2, 24);
    for (QWidget *button : m_toolbar->findChildren<QAbstractButton *>())
        button->setCursor(Qt::PointingHandCursor);
}

QImage SnipOverlay::cut(const QRect &area) const
{
    // The frozen picture has the screen's pixels; the area is in points.
    const qreal sx = qreal(m_frozen.width()) / width();
    const qreal sy = qreal(m_frozen.height()) / height();
    const QRect pixels(qRound(area.x() * sx), qRound(area.y() * sy), qRound(area.width() * sx),
                       qRound(area.height() * sy));
    return m_frozen.copy(pixels.intersected(m_frozen.rect()));
}

void SnipOverlay::paintEvent(QPaintEvent *)
{
    m_selection = m_snipper->selection(m_screen);
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.drawImage(rect(), m_frozen);
    // Everything dims except the selection.
    QRegion dim(rect());
    if (!m_selection.isEmpty())
        dim -= QRegion(m_selection);
    p.setClipRegion(dim);
    p.fillRect(rect(), QColor(0, 0, 0, 115));
    p.setClipping(false);
    if (m_selection.isEmpty())
        return;
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(t.accent, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(m_selection).adjusted(-1, -1, 1, 1));
    // The size in the picture's pixels, under the rectangle.
    const QSize size = cut(m_selection).size();
    const QString label = QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
    p.setFont(Ui::bodyFont(12.5, QFont::DemiBold));
    const QRect text = QFontMetrics(p.font()).boundingRect(label).adjusted(-10, -5, 10, 5);
    QPoint at(m_selection.left(), m_selection.bottom() + 8);
    if (at.y() + text.height() > height())
        at.setY(m_selection.top() - text.height() - 8);
    const QRectF pill(QPointF(at), QSizeF(text.size()));
    Ui::fillPill(p, pill, t.glassStrong);
    p.setPen(t.text);
    p.drawText(pill, Qt::AlignCenter, label);
}

void SnipOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        m_snipper->cancel();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (m_snipper->windowMode()) {
        m_snipper->finishWindow(m_screen->geometry().topLeft() + event->position().toPoint());
        return;
    }
    m_selecting = true;
    m_from = m_screen->geometry().topLeft() + event->position().toPoint();
    m_selection = QRect();
    m_toolbar->hide();
    update();
}

void SnipOverlay::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint global = m_screen->geometry().topLeft() + event->position().toPoint();
    if (m_snipper->windowMode()) {
        m_snipper->selectRegion(m_snipper->windowAt(global).geometry);
        return;
    }
    if (!m_selecting)
        return;
    m_snipper->selectRegion(QRect(m_from, global).normalized());
}

void SnipOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_selecting || event->button() != Qt::LeftButton)
        return;
    m_selecting = false;
    m_snipper->selectRegion(QRect(m_from, m_screen->geometry().topLeft() + event->position().toPoint()).normalized());
    m_selection = m_snipper->selection(m_screen);
    // A click without a drag is not a picture: start again.
    if (m_selection.width() < 4 || m_selection.height() < 4) {
        m_selection = QRect();
        m_snipper->selectRegion({});
        m_toolbar->show();
        update();
        return;
    }
    m_snipper->finishRegion();
}

void SnipOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        m_snipper->cancel();
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (!m_selection.isEmpty())
            m_snipper->finishRegion();
        else if (!m_snipper->windowMode())
            m_snipper->finish(m_frozen);
    }
}

Snipper::Snipper(QObject *parent)
    : QObject(parent)
{
}

bool Snipper::start(bool windowMode)
{
    m_windowMode = windowMode;
    m_frames.clear();
    m_selection = {};
    if (windowMode && !windowSnapshot(&m_windows, &m_error))
        return false;
    // Every screen is grabbed before any overlay shows, so none of them is
    // in another's picture.
    QList<QPair<QScreen *, QImage>> frozen;
    for (QScreen *screen : QGuiApplication::screens()) {
        const QImage image = Capture::grab(screen, &m_error);
        if (image.isNull())
            return false; // Never silently return a partial multi-monitor capture.
        frozen.append({screen, image});
        m_frames.append({screen->geometry(), image});
    }
    if (frozen.isEmpty())
        return false;
    for (const auto &[screen, image] : frozen) {
        auto *overlay = new SnipOverlay(this, screen, image);
        m_overlays.append(overlay);
        overlay->show();
    }
    return true;
}

CaptureWindow Snipper::windowAt(const QPoint &global) const
{
    for (const CaptureWindow &window : m_windows)
        if (window.geometry.contains(global))
            return window;
    return {};
}

void Snipper::finishWindow(const QPoint &global)
{
    const CaptureWindow window = windowAt(global);
    if (!window.geometry.isEmpty())
        finish(Capture::slice(m_frames, window.geometry));
}

QRect Snipper::selection(QScreen *screen) const
{
    return m_selection.intersected(screen->geometry()).translated(-screen->geometry().topLeft());
}

void Snipper::selectRegion(const QRect &global)
{
    m_selection = global;
    for (SnipOverlay *overlay : std::as_const(m_overlays))
        overlay->update();
}

void Snipper::finishRegion()
{
    if (!m_selection.isEmpty())
        finish(Capture::slice(m_frames, m_selection));
}

void Snipper::closeOverlays()
{
    for (SnipOverlay *overlay : std::as_const(m_overlays))
        overlay->close();
    m_overlays.clear();
}

void Snipper::finish(const QImage &image)
{
    closeOverlays();
    Q_EMIT captured(image);
}

void Snipper::cancel()
{
    closeOverlays();
    Q_EMIT cancelled();
}
