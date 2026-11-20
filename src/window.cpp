// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/window.h"

#include "frame.h"
#include "karyo/ui/icons.h"
#include "karyo/ui/paint.h"
#include "karyo/ui/theme.h"
#include "karyo/ui/tooltip.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QScreen>
#include <QWindow>

namespace Karyo::Ui {

namespace detail {

namespace {

constexpr int ResizeMargin = 6;
constexpr int CornerGrab = 18;

Qt::Edges edgesAt(const QWidget *window, const QPoint &globalPos)
{
    if (window->isMaximized() || window->isFullScreen())
        return {};
    const QPoint p = window->mapFromGlobal(globalPos);
    const int width = window->width();
    const int height = window->height();
    if (!window->rect().contains(p))
        return {};
    Qt::Edges edges;
    const bool nearLeft = p.x() < ResizeMargin;
    const bool nearRight = p.x() >= width - ResizeMargin;
    const bool nearTop = p.y() < ResizeMargin;
    const bool nearBottom = p.y() >= height - ResizeMargin;
    // Corners are generous so diagonal resizing is easy to hit.
    const bool cornerX = p.x() < CornerGrab || p.x() >= width - CornerGrab;
    const bool cornerY = p.y() < CornerGrab || p.y() >= height - CornerGrab;
    if (nearLeft || (cornerX && cornerY && p.x() < CornerGrab && (nearTop || nearBottom)))
        edges |= Qt::LeftEdge;
    if (nearRight || (cornerX && cornerY && p.x() >= width - CornerGrab && (nearTop || nearBottom)))
        edges |= Qt::RightEdge;
    if (nearTop || (cornerX && cornerY && p.y() < CornerGrab && (nearLeft || nearRight)))
        edges |= Qt::TopEdge;
    if (nearBottom || (cornerX && cornerY && p.y() >= height - CornerGrab && (nearLeft || nearRight)))
        edges |= Qt::BottomEdge;
    return edges;
}

} // namespace

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QWidget *w = window();
        w->isMaximized() ? w->showNormal() : w->showMaximized();
    }
}

bool filterResize(QWidget *window, QObject *watched, QEvent *event)
{
    const auto type = event->type();
    if (type != QEvent::MouseMove && type != QEvent::MouseButtonPress && type != QEvent::Leave)
        return false;
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != window)
        return false;
    if (type == QEvent::Leave) {
        if (widget == window)
            window->unsetCursor();
        return false;
    }
    auto *mouse = static_cast<QMouseEvent *>(event);
    const Qt::Edges edges = edgesAt(window, mouse->globalPosition().toPoint());
    if (type == QEvent::MouseMove && mouse->buttons() == Qt::NoButton) {
        Qt::CursorShape shape = Qt::ArrowCursor;
        if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
            shape = Qt::SizeFDiagCursor;
        else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
            shape = Qt::SizeBDiagCursor;
        else if (edges & (Qt::LeftEdge | Qt::RightEdge))
            shape = Qt::SizeHorCursor;
        else if (edges & (Qt::TopEdge | Qt::BottomEdge))
            shape = Qt::SizeVerCursor;
        if (edges)
            window->setCursor(shape);
        else if (window->testAttribute(Qt::WA_SetCursor))
            window->unsetCursor();
        return false;
    }
    if (type == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton && edges
        && window->windowHandle()) {
        window->windowHandle()->startSystemResize(edges);
        return true;
    }
    return false;
}

} // namespace detail

namespace {

using detail::TitleBar;
using detail::WindowRadius;

QPainterPath roundedTop(const QRectF &r, qreal radius)
{
    QPainterPath path;
    path.moveTo(r.left(), r.bottom());
    path.lineTo(r.left(), r.top() + radius);
    path.quadTo(r.left(), r.top(), r.left() + radius, r.top());
    path.lineTo(r.right() - radius, r.top());
    path.quadTo(r.right(), r.top(), r.right(), r.top() + radius);
    path.lineTo(r.right(), r.bottom());
    path.closeSubpath();
    return path;
}

} // namespace

// ---------------------------------------------------------------- IconButton

IconButton::IconButton(const QString &glyph, QWidget *parent, QSize size, int glyphSize)
    : QAbstractButton(parent)
    , m_glyph(glyph)
    , m_glyphSize(glyphSize)
{
    setFixedSize(size);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::TabFocus);
    connect(ThemeNotifier::instance(), &ThemeNotifier::changed, this, [this] {
        m_color = Theme::follow(m_color);
        m_hoverColor = Theme::follow(m_hoverColor);
        update();
    });
}

void IconButton::setGlyph(const QString &glyph)
{
    m_glyph = glyph;
    update();
}

void IconButton::setHint(const QString &hint)
{
    m_hint = hint;
    setAccessibleName(hint);
}

bool IconButton::event(QEvent *event)
{
    if (event->type() == QEvent::HoverEnter && !m_hint.isEmpty() && isEnabled())
        ToolTip::showAbove(this, m_hint, {}, 6);
    else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::MouseButtonPress
             || event->type() == QEvent::Hide)
        ToolTip::hideFor(this);
    return QAbstractButton::event(event);
}

void IconButton::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    const QRectF r = rect();
    const qreal radius = qMin(r.width(), r.height()) / 2.0;
    QColor fill = Qt::transparent;
    QColor ink = m_color.isValid() ? m_color : t.text;
    const QColor hover = m_hoverColor.isValid() ? m_hoverColor : t.hover;
    if (m_active)
        fill = hover;
    if (isEnabled() && isDown())
        fill = m_danger ? accent(700) : (m_hoverColor.isValid() ? m_hoverColor : t.press);
    else if (isEnabled() && underMouse())
        fill = m_danger ? t.accent : hover;
    if (m_danger && isEnabled() && underMouse())
        ink = neutral(100);
    if (!isEnabled())
        ink = alpha(ink, 40);
    fillRounded(painter, r, radius, fill);
    const QPixmap pixmap = ::Karyo::Ui::icon(m_glyph, m_glyphSize, ink, devicePixelRatioF());
    painter.drawPixmap(QPointF((width() - m_glyphSize) / 2.0, (height() - m_glyphSize) / 2.0), pixmap);
}

// ---------------------------------------------------------------- PillButton

PillButton::PillButton(const QString &glyph, const QString &text, QWidget *parent)
    : QAbstractButton(parent)
    , m_glyph(glyph)
{
    setText(text);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::TabFocus);
    setFixedHeight(32);
}

void PillButton::setChevron(bool chevron)
{
    m_chevron = chevron;
    updateGeometry();
}

QSize PillButton::sizeHint() const
{
    const QFont font = m_kind == Kind::Primary ? headingFont(13) : bodyFont(13, QFont::DemiBold);
    int w = 12 + QFontMetrics(font).horizontalAdvance(text()) + 12;
    if (!m_glyph.isEmpty())
        w += 16 + (text().isEmpty() ? 0 : 7);
    if (m_chevron)
        w += 6 + 14;
    return {w, 32};
}

void PillButton::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QRectF r = rect();
    QColor fill = Qt::transparent;
    QColor ink = t.text;
    const bool hover = isEnabled() && underMouse();
    const bool down = isEnabled() && isDown();
    switch (m_kind) {
    case Kind::Primary:
        fill = down ? accent(700) : hover ? accent(600) : t.accent;
        ink = neutral(100);
        break;
    case Kind::Secondary:
        fill = down ? mix(t.text, t.card, 13) : hover ? mix(t.text, t.card, 6) : t.card;
        break;
    case Kind::Tint:
        fill = down ? alpha(t.accent, 30) : hover ? alpha(t.accent, 22) : t.tint1;
        ink = t.ink1;
        break;
    case Kind::Ghost:
        fill = down ? t.press : (hover || m_active) ? t.hover : Qt::transparent;
        break;
    }
    if (!isEnabled())
        ink = alpha(ink, 45);
    fillRounded(painter, r, r.height() / 2.0, fill);

    int x = 12;
    const qreal dpr = devicePixelRatioF();
    if (!m_glyph.isEmpty()) {
        painter.drawPixmap(x, (height() - 16) / 2, ::Karyo::Ui::icon(m_glyph, 16, m_kind == Kind::Tint ? t.accent : ink, dpr));
        x += 16 + (text().isEmpty() ? 0 : 7);
    }
    const QFont font = m_kind == Kind::Primary ? headingFont(13) : bodyFont(13, QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(ink);
    const int textWidth = QFontMetrics(font).horizontalAdvance(text());
    painter.drawText(QRect(x, 0, textWidth + 1, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    x += textWidth;
    if (m_chevron)
        painter.drawPixmap(x + 6, (height() - 14) / 2, ::Karyo::Ui::icon(QStringLiteral("chevron-down"), 14, ink, dpr));
}

// ---------------------------------------------------------------- TitleTabs

TitleTabs::TitleTabs(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    const Theme &t = Theme::current();
    m_activeFill = t.win;
    m_text = t.text;
    m_muted = t.muted;
    connect(ThemeNotifier::instance(), &ThemeNotifier::changed, this, [this] {
        for (QColor *color : {&m_activeFill, &m_text, &m_muted, &m_hoverFill, &m_glyphColor})
            *color = Theme::follow(*color);
        update();
    });
}

int TitleTabs::addTab(const QString &glyph, const QString &title)
{
    m_tabs.append({glyph, title, false});
    update();
    return int(m_tabs.size()) - 1;
}

void TitleTabs::removeTab(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return;
    m_tabs.removeAt(index);
    if (m_current >= m_tabs.size())
        m_current = int(m_tabs.size()) - 1;
    else if (index < m_current)
        --m_current;
    m_hover = m_hoverClose = -1;
    update();
    Q_EMIT currentChanged(m_current);
}

void TitleTabs::setTabTitle(int index, const QString &title)
{
    if (index >= 0 && index < m_tabs.size()) {
        m_tabs[index].title = title;
        update();
    }
}

void TitleTabs::setTabGlyph(int index, const QString &glyph)
{
    if (index >= 0 && index < m_tabs.size()) {
        m_tabs[index].glyph = glyph;
        update();
    }
}

void TitleTabs::setTabDirty(int index, bool dirty)
{
    if (index >= 0 && index < m_tabs.size()) {
        m_tabs[index].dirty = dirty;
        update();
    }
}

void TitleTabs::setCurrentIndex(int index)
{
    if (index == m_current || index < 0 || index >= m_tabs.size())
        return;
    m_current = index;
    update();
    Q_EMIT currentChanged(index);
}

void TitleTabs::setColors(const QColor &activeFill, const QColor &text, const QColor &muted,
                          const QColor &hover)
{
    m_activeFill = activeFill;
    m_text = text;
    m_muted = muted;
    m_hoverFill = hover;
    update();
}

void TitleTabs::setShowNewMenu(bool show)
{
    m_showNewMenu = show;
    update();
}

QSize TitleTabs::sizeHint() const { return {400, 34}; }

int TitleTabs::tabWidth() const
{
    const int extras = 40 + (m_showNewMenu ? 30 : 0);
    if (m_tabs.isEmpty())
        return 240;
    return qBound(120, (width() - extras) / int(m_tabs.size()), m_maxTabWidth);
}

QRect TitleTabs::tabRect(int index) const
{
    const int w = tabWidth();
    return QRect(index * w, 6, w - 2, height() - 6);
}

QRect TitleTabs::closeRect(int index) const
{
    const QRect r = tabRect(index);
    return QRect(r.right() - 34, r.top() + (r.height() - 26) / 2, 26, 26);
}

QRect TitleTabs::plusRect() const
{
    const int x = int(m_tabs.size()) * tabWidth() + 6;
    return QRect(x, 6 + (height() - 6 - 30) / 2, 30, 30);
}

QRect TitleTabs::menuRect() const
{
    const QRect plus = plusRect();
    return QRect(plus.right() + 2, plus.top(), 28, 30);
}

void TitleTabs::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal dpr = devicePixelRatioF();
    for (int i = 0; i < m_tabs.size(); ++i) {
        const QRect r = tabRect(i);
        const bool active = i == m_current;
        if (active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_activeFill);
            painter.drawPath(roundedTop(QRectF(r).adjusted(0, 0, 0, 1), 8));
        } else if (i == m_hover) {
            fillRounded(painter, QRectF(r).adjusted(2, 4, -2, -6), 6,
                        m_hoverFill.isValid() ? m_hoverFill : t.hover);
        }
        const Tab &tab = m_tabs.at(i);
        int x = r.left() + 16;
        const QColor ink = active ? m_text : m_muted;
        if (!tab.glyph.isEmpty()) {
            painter.drawPixmap(x, r.center().y() - 8, ::Karyo::Ui::icon(tab.glyph, 16, active ? (m_glyphColor.isValid() ? m_glyphColor : t.accent) : m_muted, dpr));
            x += 16 + 10;
        }
        // An unsaved tab keeps its dot until the pointer comes for the close button.
        const bool showClose = (active && !tab.dirty) || i == m_hover;
        const int right = showClose ? closeRect(i).left() - 4 : r.right() - 12;
        painter.setFont(bodyFont(13, active ? QFont::DemiBold : QFont::Normal));
        painter.setPen(ink);
        const QRect textRect(x, r.top(), right - x, r.height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(tab.title, Qt::ElideRight, textRect.width()));
        if (tab.dirty && !showClose) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(t.accent);
            painter.drawEllipse(QPointF(r.right() - 20, r.center().y()), 4, 4);
        }
        if (showClose) {
            const QRect c = closeRect(i);
            if (i == m_hoverClose)
                fillRounded(painter, c, 6, m_hoverFill.isValid() ? m_hoverFill : t.press);
            painter.drawPixmap(c.left() + 6, c.top() + 6, ::Karyo::Ui::icon(QStringLiteral("x"), 14, m_muted, dpr));
        }
    }
    const QRect plus = plusRect();
    const QColor hoverFill = m_hoverFill.isValid() ? m_hoverFill : t.hover;
    if (m_hoverExtra == 1)
        fillRounded(painter, plus, 6, hoverFill);
    painter.drawPixmap(plus.left() + 6, plus.top() + 6, ::Karyo::Ui::icon(QStringLiteral("plus"), 18, m_text, dpr));
    if (m_showNewMenu) {
        const QRect menu = menuRect();
        if (m_hoverExtra == 2)
            fillRounded(painter, menu, 6, hoverFill);
        painter.drawPixmap(menu.left() + 7, menu.top() + 8, ::Karyo::Ui::icon(QStringLiteral("chevron-down"), 14, m_text, dpr));
    }
}

void TitleTabs::mousePressEvent(QMouseEvent *event)
{
    const QPoint p = event->position().toPoint();
    if (event->button() == Qt::MiddleButton) {
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (tabRect(i).contains(p)) {
                Q_EMIT closeRequested(i);
                return;
            }
        }
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (!tabRect(i).contains(p))
            continue;
        if ((i == m_current || i == m_hover) && closeRect(i).contains(p))
            return;  // handled on release
        setCurrentIndex(i);
        return;
    }
    if (plusRect().contains(p) || (m_showNewMenu && menuRect().contains(p)))
        return;
    // Empty strip: let the title bar move the window.
    event->ignore();
}

void TitleTabs::mouseReleaseEvent(QMouseEvent *event)
{
    const QPoint p = event->position().toPoint();
    if (event->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < m_tabs.size(); ++i) {
        if ((i == m_current || i == m_hover) && closeRect(i).contains(p)) {
            Q_EMIT closeRequested(i);
            return;
        }
    }
    if (plusRect().contains(p))
        Q_EMIT newTabRequested();
    else if (m_showNewMenu && menuRect().contains(p))
        Q_EMIT newMenuRequested(mapToGlobal(menuRect().bottomLeft()));
}

void TitleTabs::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint p = event->position().toPoint();
    int hover = -1;
    int hoverClose = -1;
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (tabRect(i).contains(p)) {
            hover = i;
            if (closeRect(i).contains(p))
                hoverClose = i;
        }
    }
    const int extra = plusRect().contains(p) ? 1 : (m_showNewMenu && menuRect().contains(p)) ? 2 : 0;
    if (hover != m_hover || hoverClose != m_hoverClose || extra != m_hoverExtra) {
        m_hover = hover;
        m_hoverClose = hoverClose;
        m_hoverExtra = extra;
        update();
    }
}

void TitleTabs::leaveEvent(QEvent *)
{
    m_hover = m_hoverClose = -1;
    m_hoverExtra = 0;
    update();
}

// ---------------------------------------------------------------- SearchField

SearchField::SearchField(const QString &placeholder, QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
    , m_fill(Theme::current().card)
{
    setFixedHeight(34);
    connect(ThemeNotifier::instance(), &ThemeNotifier::changed, this, [this] {
        m_fill = Theme::follow(m_fill);
        update();
    });
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(38, 0, 14, 0);
    m_edit->setFrame(false);
    m_edit->setPlaceholderText(placeholder);
    m_edit->setFont(bodyFont(13));
    QPalette p = m_edit->palette();
    p.setColor(QPalette::Base, Qt::transparent);
    m_edit->setPalette(p);
    m_edit->setAttribute(Qt::WA_TranslucentBackground);
    layout->addWidget(m_edit);
    setFocusProxy(m_edit);
    m_edit->installEventFilter(this);
}

bool SearchField::eventFilter(QObject *watched, QEvent *event)
{
    // The focus ring is drawn here, around the line edit.
    if (watched == m_edit && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
        update();
    return QWidget::eventFilter(watched, event);
}

void SearchField::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    const QRectF r = rect();
    fillRounded(painter, r, r.height() / 2.0, m_fill);
    if (m_edit->hasFocus()) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(t.accent, 2));
        painter.setBrush(Qt::NoBrush);
        const QRectF ring = r.adjusted(1, 1, -1, -1);
        painter.drawRoundedRect(ring, ring.height() / 2.0, ring.height() / 2.0);
    }
    painter.drawPixmap(15, (height() - 16) / 2, ::Karyo::Ui::icon(QStringLiteral("search"), 16, t.muted, devicePixelRatioF()));
}

// ---------------------------------------------------------------- KaryoWindow

KaryoWindow::KaryoWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_titleBar(new TitleBar(this))
    , m_titleLayout(new QHBoxLayout)
    , m_layout(new QVBoxLayout(this))
    , m_body(Theme::current().win)
    , m_title(Qt::transparent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    connect(ThemeNotifier::instance(), &ThemeNotifier::changed, this, [this] {
        m_body = Theme::follow(m_body);
        m_title = Theme::follow(m_title);
        update();
    });
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    auto *strip = new QHBoxLayout(m_titleBar);
    strip->setContentsMargins(0, 0, 14, 0);
    strip->setSpacing(4);
    m_titleLayout->setContentsMargins(14, 0, 8, 0);
    m_titleLayout->setSpacing(8);
    strip->addLayout(m_titleLayout, 1);

    auto *minimize = new IconButton(QStringLiteral("minus"), m_titleBar, {40, 34}, 16);
    m_maximize = new IconButton(QStringLiteral("square"), m_titleBar, {40, 34}, 14);
    auto *close = new IconButton(QStringLiteral("x"), m_titleBar, {40, 34}, 16);
    for (IconButton *button : {minimize, m_maximize, close}) {
        button->setFocusPolicy(Qt::NoFocus);
        button->setGlyphColor(Theme::current().muted);
        strip->addWidget(button, 0, Qt::AlignVCenter);
    }
    close->setDangerHover(true);
    m_controls = {minimize, m_maximize, close};
    minimize->setHint(tr("Minimize"));
    m_maximize->setHint(tr("Maximize"));
    close->setHint(tr("Close"));
    connect(minimize, &QAbstractButton::clicked, this, &QWidget::showMinimized);
    connect(m_maximize, &QAbstractButton::clicked, this, &KaryoWindow::toggleMaximized);
    connect(close, &QAbstractButton::clicked, this, &QWidget::close);

    setTitleHeight(40);
    m_layout->addWidget(m_titleBar);
    qApp->installEventFilter(this);
}

void KaryoWindow::setCentralWidget(QWidget *widget)
{
    if (m_central) {
        m_layout->removeWidget(m_central);
        m_central->deleteLater();
    }
    m_central = widget;
    m_layout->addWidget(widget, 1);
}

void KaryoWindow::setTitleHeight(int height) { m_titleBar->setFixedHeight(height); }

void KaryoWindow::resizeToFit(const QSize &preferred)
{
    QScreen *screen = this->screen() ? this->screen() : QGuiApplication::primaryScreen();
    if (!screen) {
        resize(preferred);
        return;
    }
    // Leave a margin so the window reads as a window, not a maximized one.
    const QSize room = screen->availableGeometry().size() * 0.92;
    resize(preferred.boundedTo(room));
}

void KaryoWindow::setBodyColor(const QColor &color)
{
    m_body = color;
    update();
}

void KaryoWindow::setTitleColor(const QColor &color)
{
    m_title = color;
    update();
}

void KaryoWindow::setControlColors(const QColor &glyph, const QColor &hover)
{
    for (IconButton *button : std::as_const(m_controls)) {
        button->setGlyphColor(glyph);
        button->setHoverColor(hover);
    }
}

qreal KaryoWindow::cornerRadius() const
{
    return (isMaximized() || isFullScreen()) ? 0 : WindowRadius;
}

void KaryoWindow::toggleMaximized() { isMaximized() ? showNormal() : showMaximized(); }

void KaryoWindow::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal radius = cornerRadius();
    QPainterPath body;
    body.addRoundedRect(QRectF(rect()), radius, radius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_body);
    painter.drawPath(body);
    if (m_title.alpha()) {
        painter.save();
        painter.setClipPath(body);
        painter.fillRect(QRect(0, 0, width(), m_titleBar->height()), m_title);
        painter.restore();
    }
    if (radius > 0) {
        painter.setPen(QPen(t.edge, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
    }
}

void KaryoWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        m_maximize->setGlyph(isMaximized() ? QStringLiteral("columns-2") : QStringLiteral("square"));
        update();
    }
    QWidget::changeEvent(event);
}

bool KaryoWindow::eventFilter(QObject *watched, QEvent *event)
{
    return detail::filterResize(this, watched, event);
}

} // namespace Karyo::Ui
