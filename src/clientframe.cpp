// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/clientframe.h"

#include "frame.h"
#include "karyo/ui/theme.h"
#include "karyo/ui/window.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

namespace Karyo::Ui {

namespace {

constexpr int TitleHeight = 44;
// Set on a window when it is created: 1 waits for the first show, 2 framed.
const char FrameState[] = "_karyo_client_frame";

bool isExpanded(const QWidget *window) { return window->isMaximized() || window->isFullScreen(); }

// Clears the window outside its rounded outline and draws the outline. It
// stays above the window's own widgets, which paint up to the corners.
class Corners : public QWidget
{
public:
    explicit Corners(QWidget *window)
        : QWidget(window)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (isExpanded(window()))
            return;
        const qreal radius = detail::WindowRadius;
        const QRectF r = rect();
        QPainterPath body;
        body.addRoundedRect(r, radius, radius);
        QPainterPath outside;
        outside.addRect(r);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillPath(outside.subtracted(body), Qt::black);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setPen(QPen(Theme::current().edge, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
    }
};

// The title strip: the window's icon and title, and its controls.
class Title : public detail::TitleBar
{
public:
    explicit Title(QWidget *window)
        : TitleBar(window)
    {
        const bool dialog = window->windowType() == Qt::Dialog;
        auto *strip = new QHBoxLayout(this);
        strip->setContentsMargins(0, 0, 10, 0);
        strip->setSpacing(4);
        strip->addStretch(1);
        QList<IconButton *> controls;
        if (!dialog) {
            auto *minimize = new IconButton(QStringLiteral("minus"), this, {40, 32}, 16);
            m_maximize = new IconButton(QStringLiteral("square"), this, {40, 32}, 14);
            minimize->setHint(QWidget::tr("Minimize"));
            m_maximize->setHint(QWidget::tr("Maximize"));
            connect(minimize, &QAbstractButton::clicked, window, &QWidget::showMinimized);
            connect(m_maximize, &QAbstractButton::clicked, window, [window] {
                isExpanded(window) ? window->showNormal() : window->showMaximized();
            });
            controls = {minimize, m_maximize};
        }
        auto *close = new IconButton(QStringLiteral("x"), this, {40, 32}, 16);
        close->setDangerHover(true);
        close->setHint(QWidget::tr("Close"));
        connect(close, &QAbstractButton::clicked, window, &QWidget::close);
        controls.append(close);
        for (IconButton *button : std::as_const(controls)) {
            button->setFocusPolicy(Qt::NoFocus);
            button->setGlyphColor(Theme::current().muted);
            strip->addWidget(button, 0, Qt::AlignVCenter);
        }
    }

    void sync()
    {
        if (m_maximize)
            m_maximize->setGlyph(isExpanded(window()) ? QStringLiteral("columns-2")
                                                       : QStringLiteral("square"));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = Theme::current();
        QPainter painter(this);
        painter.fillRect(rect(), t.bg);
        int x = 18;
        const QIcon icon = window()->windowIcon();
        if (!icon.isNull()) {
            constexpr int size = 18;
            painter.drawPixmap(x, (height() - size) / 2,
                               icon.pixmap(QSize(size, size), devicePixelRatioF()));
            x += size + 10;
        }
        const QFont font = bodyFont(13.5, QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(t.text);
        const int controlsLeft = childrenRect().isEmpty() ? width() : childrenRect().left();
        const QRect text(x, 0, qMax(0, controlsLeft - x - 12), height());
        painter.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(font).elidedText(window()->windowTitle(), Qt::ElideRight,
                                                       text.width()));
    }

private:
    IconButton *m_maximize = nullptr;
};

class ClientFrame : public QObject
{
public:
    explicit ClientFrame(QWidget *window)
        : QObject(window)
        , m_window(window)
        , m_title(new Title(window))
        , m_corners(new Corners(window))
    {
        const QMargins m = window->contentsMargins();
        window->setContentsMargins(m.left(), m.top() + TitleHeight, m.right(), m.bottom());
        // Keep the size the application chose for its own contents.
        if (window->testAttribute(Qt::WA_Resized) && !isExpanded(window))
            window->resize(window->width(), window->height() + TitleHeight);
        place();
        m_title->sync();
        m_title->show();
        m_corners->show();
        m_corners->raise();
        window->installEventFilter(this);
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_window) {
            switch (event->type()) {
            case QEvent::Resize:
                place();
                break;
            case QEvent::ChildAdded:
                // Widgets added later must not cover the corners.
                if (static_cast<QChildEvent *>(event)->child() != m_corners)
                    m_corners->raise();
                break;
            case QEvent::WindowStateChange:
                m_title->sync();
                m_corners->update();
                break;
            case QEvent::WindowTitleChange:
            case QEvent::WindowIconChange:
                m_title->update();
                break;
            case QEvent::Paint: {
                // A translucent window paints no background of its own, and
                // the application's widgets rely on one.
                QPainter painter(m_window);
                painter.fillRect(static_cast<QPaintEvent *>(event)->rect(),
                                 m_window->palette().color(QPalette::Window));
                break;
            }
            default:
                break;
            }
        }
        return detail::filterResize(m_window, watched, event);
    }

private:
    void place()
    {
        m_title->setGeometry(0, 0, m_window->width(), TitleHeight);
        m_corners->setGeometry(m_window->rect());
    }

    QWidget *m_window;
    Title *m_title;
    Corners *m_corners;
};

class FrameInstaller : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const auto type = event->type();
        if (type != QEvent::Create && type != QEvent::Show)
            return false;
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || !widget->isWindow())
            return false;
        if (type == QEvent::Create) {
            // Sent while the widget is constructed, before it has a native
            // window: the only time translucency and flags take effect.
            const Qt::WindowType kind = widget->windowType();
            const Qt::WindowFlags flags = widget->windowFlags();
            if ((kind != Qt::Window && kind != Qt::Dialog)
                || (flags & (Qt::FramelessWindowHint | Qt::BypassWindowManagerHint))
                || widget->testAttribute(Qt::WA_WState_Created))
                return false;
            widget->setAttribute(Qt::WA_TranslucentBackground);
            widget->overrideWindowFlags(flags | Qt::FramelessWindowHint);
            widget->setProperty(FrameState, 1);
            return false;
        }
        // The first show: the window is fully constructed and laid out.
        if (widget->property(FrameState).toInt() != 1)
            return false;
        widget->setProperty(FrameState, 2);
        new ClientFrame(widget);
        return false;
    }
};

} // namespace

void installClientFrames(QApplication *app)
{
    static FrameInstaller *installer = nullptr;
    if (installer)
        return;
    installer = new FrameInstaller(app);
    app->installEventFilter(installer);
}

} // namespace Karyo::Ui
