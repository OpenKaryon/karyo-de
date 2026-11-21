// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas.h"

#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QTextOption>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QResizeEvent>
#include <QPainterPathStroker>
#include <QtMath>

namespace Ui = Karyo::Ui;

namespace {

QRectF resizedRect(const QRectF &before, int handle, const QPointF &at, const QRectF &bounds)
{
    QRectF r = before;
    if (handle == 0 || handle == 6 || handle == 7) r.setLeft(qMin(at.x(), r.right() - 4));
    if (handle == 2 || handle == 3 || handle == 4) r.setRight(qMax(at.x(), r.left() + 4));
    if (handle == 0 || handle == 1 || handle == 2) r.setTop(qMin(at.y(), r.bottom() - 4));
    if (handle == 4 || handle == 5 || handle == 6) r.setBottom(qMax(at.y(), r.top() + 4));
    return r.intersected(bounds);
}

QPointF moveDelta(const QRectF &rect, QPointF delta, const QRectF &bounds)
{
    delta.setX(rect.width() <= bounds.width() ? qBound(bounds.left() - rect.left(), delta.x(), bounds.right() - rect.right()) : 0);
    delta.setY(rect.height() <= bounds.height() ? qBound(bounds.top() - rect.top(), delta.y(), bounds.bottom() - rect.bottom()) : 0);
    return delta;
}

Qt::CursorShape resizeCursor(int handle)
{
    if (handle == 0 || handle == 4) return Qt::SizeFDiagCursor;
    if (handle == 2 || handle == 6) return Qt::SizeBDiagCursor;
    if (handle == 1 || handle == 5) return Qt::SizeVerCursor;
    return Qt::SizeHorCursor;
}

// Three separable box passes approximate a Gaussian, with work proportional
// to the selected area rather than the kernel size. Only the flattened pixels
// inside the selection are sampled, including any earlier annotations.
QImage blur(QImage image)
{
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    constexpr int radius = 12;
    for (int pass = 0; pass < 3; ++pass) {
        for (int axis = 0; axis < 2; ++axis) {
            QImage out(image.size(), image.format());
            const int length = axis ? image.height() : image.width();
            const int lines = axis ? image.width() : image.height();
            for (int line = 0; line < lines; ++line) {
                auto pixel = [&](int pos) {
                    pos = qBound(0, pos, length - 1);
                    return image.pixel(axis ? line : pos, axis ? pos : line);
                };
                int r = 0, g = 0, b = 0, a = 0;
                auto add = [&](QRgb p, int sign) {
                    r += sign * qRed(p); g += sign * qGreen(p);
                    b += sign * qBlue(p); a += sign * qAlpha(p);
                };
                for (int i = -radius; i <= radius; ++i)
                    add(pixel(i), 1);
                constexpr int count = 2 * radius + 1;
                for (int pos = 0; pos < length; ++pos) {
                    out.setPixel(axis ? line : pos, axis ? pos : line, qRgba(r / count, g / count, b / count, a / count));
                    add(pixel(pos - radius), -1);
                    add(pixel(pos + radius + 1), 1);
                }
            }
            image = out;
        }
    }
    return image;
}

} // namespace

Canvas::Canvas(QWidget *parent)
    : QWidget(parent)
    , m_color(0xc6, 0x71, 0x39)
    , m_textEditor(new QPlainTextEdit(this))
{
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_textEditor->setObjectName(QStringLiteral("inlineTextEditor"));
    m_textEditor->setFrameShape(QFrame::NoFrame);
    m_textEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEditor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEditor->setWordWrapMode(QTextOption::NoWrap);
    m_textEditor->document()->setDocumentMargin(0);
    m_textEditor->setStyleSheet(QStringLiteral("QPlainTextEdit { background: transparent; border: 0; padding: 0; }"));
    m_textEditor->viewport()->setAutoFillBackground(false);
    m_textEditor->setToolTip(tr("Enter — finish text · Shift+Enter — new line · Esc — cancel"));
    m_textEditor->installEventFilter(this);
    m_textEditor->hide();
    connect(m_textEditor, &QPlainTextEdit::textChanged, this, &Canvas::positionTextEditor);
}

void Canvas::setImage(const QImage &image)
{
    finishText(false);
    m_pendingCrop = {};
    m_selected = -1;
    m_drag = Drag::None;
    m_base = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_base.setDevicePixelRatio(1);
    m_rendered = {};
    m_drawing = false;
    m_steps.clear();
    m_history.clear();
    m_undone.clear();
    update();
    Q_EMIT changed();
    Q_EMIT selectionChanged();
}

QRectF Canvas::cropRect() const
{
    // The last crop wins; crops nest because each is drawn on the result.
    QRectF crop(QPointF(0, 0), QSizeF(m_base.size()));
    for (const Step &step : m_steps) {
        if (step.tool == Tool::Crop)
            crop = QRectF(step.points.value(0), step.points.value(1)).normalized().intersected(crop);
    }
    return crop;
}

QRectF Canvas::target() const
{
    // The cropped picture fitted into the widget, never larger than 1:1.
    const QRectF crop = cropRect();
    if (crop.isEmpty())
        return {};
    const qreal dpr = devicePixelRatioF();
    const qreal scale = qMax(0.0, qMin(1.0 / dpr, qMin((width() - 32) / crop.width(), (height() - 32) / crop.height())));
    const QSizeF size = crop.size() * scale;
    return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

QPointF Canvas::toImage(const QPointF &widget) const
{
    const QRectF t = target();
    const QRectF crop = cropRect();
    if (t.isEmpty())
        return {};
    return crop.topLeft() + (widget - t.topLeft()) * (crop.width() / t.width());
}

QPointF Canvas::fromImage(const QPointF &image) const
{
    return target().topLeft() + (image - cropRect().topLeft()) * (target().width() / cropRect().width());
}

QPointF Canvas::bounded(const QPointF &at) const
{
    const QRectF crop = cropRect();
    return {qBound(crop.left(), at.x(), crop.right()), qBound(crop.top(), at.y(), crop.bottom())};
}

void Canvas::paintStep(QPainter &painter, const Step &step, qreal scale)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    switch (step.tool) {
    case Tool::Pen:
    case Tool::Highlighter: {
        QColor color = step.color;
        const bool marker = step.tool == Tool::Highlighter;
        if (marker)
            color.setAlphaF(0.38);
        painter.setPen(QPen(color, (marker ? step.width * 5.5 : step.width) * scale, Qt::SolidLine,
                            marker ? Qt::SquareCap : Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        // One path, so a marker stroke does not darken where it crosses itself.
        QPainterPath path;
        path.addPolygon(step.points);
        painter.drawPath(path);
        break;
    }
    case Tool::Box:
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(step.color, step.width * scale));
        painter.drawRect(QRectF(step.points.value(0), step.points.value(1)).normalized());
        break;
    case Tool::Arrow: {
        const QPointF from = step.points.value(0);
        const QPointF to = step.points.value(1);
        const qreal width = step.width * scale;
        painter.setPen(QPen(step.color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal angle = std::atan2(to.y() - from.y(), to.x() - from.x());
        const qreal head = qMin(6 * width, QLineF(from, to).length() * 0.45);
        // The shaft stops where the head begins, so its end does not show.
        const QPointF base = to - QPointF(std::cos(angle), std::sin(angle)) * head * 0.8;
        painter.drawLine(from, base);
        QPolygonF arrow;
        arrow << to << to - QPointF(std::cos(angle - 0.45), std::sin(angle - 0.45)) * head
              << to - QPointF(std::cos(angle + 0.45), std::sin(angle + 0.45)) * head;
        painter.setBrush(step.color);
        painter.drawPolygon(arrow);
        break;
    }
    case Tool::Text: {
        QFont font(QStringLiteral("IBM Plex Sans"));
        font.setPixelSize(qRound(step.textSize * scale));
        painter.setFont(font);
        painter.setPen(step.color);
        const QFontMetricsF metrics(font);
        QPointF at = step.points.value(0) + QPointF(0, metrics.ascent());
        for (const QString &line : step.text.split(QLatin1Char('\n'))) {
            painter.drawText(at, line);
            at.ry() += metrics.lineSpacing();
        }
        break;
    }
    case Tool::Blur:
    case Tool::Crop:
    case Tool::Select:
        break;
    }
    painter.restore();
}

QImage Canvas::rendered() const
{
    if (m_base.isNull())
        return {};
    if (!m_rendered.isNull())
        return m_rendered;
    QImage result = m_base;
    for (int i = 0; i < m_steps.size(); ++i) {
        if (m_textEditor->isVisible() && i == m_textIndex)
            continue;
        const Step &step = m_steps.at(i);
        if (step.tool == Tool::Blur) {
            const QRect area = step.points.boundingRect().toAlignedRect().intersected(result.rect());
            if (!area.isEmpty()) {
                const QImage blurred = blur(result.copy(area));
                QPainter painter(&result);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.drawImage(area.topLeft(), blurred);
            }
        } else {
            QPainter painter(&result);
            paintStep(painter, step, 1.0);
        }
    }
    m_rendered = result;
    return result;
}

QImage Canvas::image() const
{
    return rendered().copy(cropRect().toAlignedRect());
}

void Canvas::commit(const Step &step)
{
    const auto before = m_steps;
    m_steps.append(step);
    record(before);
}

void Canvas::record(const QList<Step> &before)
{
    if (m_steps == before)
        return;
    m_history.append(before);
    m_undone.clear();
    m_rendered = {};
    update();
    Q_EMIT changed();
}

void Canvas::addText(const QPointF &at, const QString &text)
{
    if (!m_base.isNull() && !text.trimmed().isEmpty())
        commit({Tool::Text, m_color, QPolygonF{at}, m_width, m_textSize, text});
}

void Canvas::undo()
{
    finishEditing();
    m_pendingCrop = {};
    m_drag = Drag::None;
    m_drawing = false;
    select(-1);
    if (m_history.isEmpty()) {
        update();
        return;
    }
    m_undone.append(m_steps);
    m_steps = m_history.takeLast();
    m_rendered = {};
    update();
    Q_EMIT changed();
}

void Canvas::redo()
{
    finishEditing();
    m_pendingCrop = {};
    select(-1);
    if (m_undone.isEmpty())
        return;
    m_history.append(m_steps);
    m_steps = m_undone.takeLast();
    m_rendered = {};
    update();
    Q_EMIT changed();
}


void Canvas::setTool(Tool tool)
{
    finishEditing();
    m_tool = tool;
    m_pendingCrop = {};
    m_drawing = false;
    select(-1);
    setFocus(Qt::OtherFocusReason);
    update();
}

void Canvas::setColor(const QColor &color)
{
    m_color = color;
    if (m_textEditor->isVisible()) {
        m_textDraft.color = color;
        positionTextEditor();
    } else if (m_selected >= 0) {
        const auto before = m_steps;
        m_steps[m_selected].color = color;
        record(before);
    }
}

void Canvas::setWidth(int width)
{
    m_width = width;
    if (m_selected >= 0 && m_steps[m_selected].tool != Tool::Text) {
        const auto before = m_steps;
        m_steps[m_selected].width = width;
        record(before);
    }
}

void Canvas::setTextSize(int size)
{
    m_textSize = size;
    if (m_textEditor->isVisible()) {
        m_textDraft.textSize = size;
        positionTextEditor();
    } else if (textSelected()) {
        const auto before = m_steps;
        m_steps[m_selected].textSize = size;
        record(before);
    }
}

bool Canvas::textSelected() const
{
    return m_selected >= 0 && m_steps.at(m_selected).tool == Tool::Text;
}

void Canvas::select(int index)
{
    m_selected = index;
    if (index >= 0) {
        const Step &step = m_steps.at(index);
        m_color = step.color;
        m_width = step.width;
        m_textSize = step.textSize;
    }
    update();
    Q_EMIT selectionChanged();
}

QPolygonF Canvas::handles(const QRectF &r) const
{
    return {r.topLeft(), {r.center().x(), r.top()}, r.topRight(), {r.right(), r.center().y()},
            r.bottomRight(), {r.center().x(), r.bottom()}, r.bottomLeft(), {r.left(), r.center().y()}};
}

QPolygonF Canvas::selectedHandles() const
{
    if (m_selected < 0) return {};
    const Step &step = m_steps.at(m_selected);
    if (step.tool == Tool::Arrow) return step.points;
    if (step.tool == Tool::Box) return handles(step.points.boundingRect());
    return {};
}

int Canvas::hitHandle(const QPointF &widget, const QPolygonF &points) const
{
    for (int i = 0; i < points.size(); ++i)
        if (QLineF(widget, fromImage(points.at(i))).length() <= 8)
            return i;
    return -1;
}

int Canvas::hitObject(const QPointF &widget) const
{
    if (target().isEmpty()) return -1;
    const QPointF at = toImage(widget);
    const qreal slop = 7 * cropRect().width() / target().width();
    for (int i = m_steps.size() - 1; i >= 0; --i) {
        const Step &step = m_steps.at(i);
        if (step.tool == Tool::Box) {
            const QRectF rect = step.points.boundingRect();
            if (rect.adjusted(-slop, -slop, slop, slop).contains(at)
                && (m_tool == Tool::Select || i == m_selected || !rect.adjusted(slop, slop, -slop, -slop).contains(at)))
                return i;
        } else if (step.tool == Tool::Arrow) {
            QPainterPath path;
            path.moveTo(step.points.value(0));
            path.lineTo(step.points.value(1));
            QPainterPathStroker stroker;
            stroker.setWidth(qMax(2 * slop, qreal(step.width)));
            if (stroker.createStroke(path).contains(at)) return i;
        } else if (step.tool == Tool::Text) {
            QFont font(QStringLiteral("IBM Plex Sans"));
            font.setPixelSize(step.textSize);
            const QFontMetricsF fm(font);
            qreal width = 0;
            const auto lines = step.text.split(QLatin1Char('\n'));
            for (const QString &line : lines) width = qMax(width, fm.horizontalAdvance(line));
            if (QRectF(step.points.first(), QSizeF(width, lines.size() * fm.lineSpacing())).contains(at)) return i;
        }
    }
    return -1;
}

void Canvas::drawHandles(QPainter &p, const QPolygonF &points) const
{
    const auto &theme = Ui::Theme::current();
    p.setPen(QPen(theme.accent, 1.5));
    p.setBrush(theme.win);
    for (const QPointF &point : points) {
        const QPointF at = fromImage(point);
        p.drawRect(QRectF(at - QPointF(4, 4), QSizeF(8, 8)));
    }
}

void Canvas::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF target = this->target();
    if (target.isEmpty()) return;
    const QRectF crop = cropRect();
    const qreal scale = target.width() / crop.width();
    p.save();
    p.setClipRect(target);
    p.translate(target.topLeft());
    p.scale(scale, scale);
    p.translate(-crop.topLeft());
    p.drawImage(QPointF(0, 0), rendered());
    if (m_drawing && m_current.tool != Tool::Crop && m_current.tool != Tool::Blur)
        paintStep(p, m_current, 1.0);
    p.restore();
    p.setPen(QPen(t.edge, 1));
    p.drawRect(target.adjusted(-0.5, -0.5, 0.5, 0.5));

    QRectF selection = m_pendingCrop;
    if (m_drawing && (m_current.tool == Tool::Crop || m_current.tool == Tool::Blur))
        selection = m_current.points.boundingRect();
    p.setRenderHint(QPainter::Antialiasing);
    if (!selection.isEmpty()) {
        const QRectF chosen(fromImage(selection.topLeft()), fromImage(selection.bottomRight()));
        if (m_tool == Tool::Crop) {
            QRegion outside(target.toAlignedRect());
            outside -= QRegion(chosen.toAlignedRect());
            p.setClipRegion(outside);
            p.fillRect(target, QColor(0, 0, 0, 110));
            p.setClipping(false);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.accent, 1.5, Qt::DashLine));
        p.drawRect(chosen);
        if (!m_pendingCrop.isEmpty()) {
            drawHandles(p, handles(m_pendingCrop));
            const QString hint = tr("Enter — crop · Esc — cancel");
            p.setFont(Ui::bodyFont(12));
            const QRect text = QFontMetrics(p.font()).boundingRect(hint).adjusted(-10, -5, 10, 5);
            const QRectF pill((width() - text.width()) / 2.0, height() - text.height() - 4, text.width(), text.height());
            Ui::fillPill(p, pill, t.glassStrong);
            p.setPen(t.text);
            p.drawText(pill, Qt::AlignCenter, hint);
        }
    }
    if (m_selected >= 0 && !m_textEditor->isVisible()) {
        const Step &step = m_steps.at(m_selected);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.accent, 1, Qt::DashLine));
        if (step.tool == Tool::Box)
            p.drawRect(QRectF(fromImage(step.points.boundingRect().topLeft()), fromImage(step.points.boundingRect().bottomRight())));
        drawHandles(p, selectedHandles());
    }
    if (m_textEditor->isVisible()) {
        p.setPen(QPen(t.accent, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(m_textEditor->geometry()).adjusted(-2, -2, 2, 2));
    }
}

void Canvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_base.isNull()) return;
    finishText(true);
    setFocus(Qt::MouseFocusReason);
    const QPointF widget = event->position();
    const QPointF at = bounded(toImage(widget));
    if (m_tool == Tool::Crop && !m_pendingCrop.isEmpty()) {
        m_handle = hitHandle(widget, handles(m_pendingCrop));
        if (m_handle >= 0 || m_pendingCrop.contains(at)) {
            m_drag = Drag::Crop;
            m_cropBefore = m_pendingCrop;
            m_dragFrom = at;
            return;
        }
    }
    const bool editable = m_tool == Tool::Select || m_tool == Tool::Arrow || m_tool == Tool::Box || m_tool == Tool::Text;
    if (editable) {
        m_handle = hitHandle(widget, selectedHandles());
        const int hit = m_handle >= 0 ? m_selected : hitObject(widget);
        if (hit >= 0) {
            select(hit);
            m_drag = Drag::Object;
            m_dragBefore = m_steps;
            m_dragFrom = at;
            return;
        }
    }
    select(-1);
    if (!target().contains(widget) || m_tool == Tool::Select) return;
    if (m_tool == Tool::Text) {
        beginText(at);
        return;
    }
    m_pendingCrop = {};
    m_drawing = true;
    m_current = {m_tool, m_color, QPolygonF{at, at}, m_width, m_textSize, {}};
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF at = bounded(toImage(event->position()));
    if (m_drag == Drag::Crop) {
        m_pendingCrop = m_handle >= 0 ? resizedRect(m_cropBefore, m_handle, at, cropRect())
                                     : m_cropBefore.translated(moveDelta(m_cropBefore, at - m_dragFrom, cropRect()));
        update();
        return;
    }
    if (m_drag == Drag::Object) {
        Step &step = m_steps[m_selected];
        const Step &before = m_dragBefore.at(m_selected);
        if (m_handle >= 0 && step.tool == Tool::Arrow) {
            step.points = before.points;
            if (QLineF(at, before.points.at(1 - m_handle)).length() >= 2)
                step.points[m_handle] = at;
        } else if (m_handle >= 0 && step.tool == Tool::Box) {
            const QRectF r = resizedRect(before.points.boundingRect(), m_handle, at, cropRect());
            step.points = {r.topLeft(), r.bottomRight()};
        } else {
            step.points = before.points.translated(moveDelta(before.points.boundingRect(), at - m_dragFrom, cropRect()));
        }
        m_rendered = {};
        update();
        return;
    }
    if (m_drawing) {
        if (m_current.tool == Tool::Pen || m_current.tool == Tool::Highlighter) m_current.points.append(at);
        else m_current.points[1] = at;
        update();
        return;
    }
    const bool editable = m_tool == Tool::Select || m_tool == Tool::Arrow || m_tool == Tool::Box || m_tool == Tool::Text;
    const int handle = m_tool == Tool::Crop ? hitHandle(event->position(), handles(m_pendingCrop))
                                          : editable ? hitHandle(event->position(), selectedHandles()) : -1;
    if (handle >= 0 && (m_tool != Tool::Crop || !m_pendingCrop.isEmpty()))
        setCursor(m_tool != Tool::Crop && m_selected >= 0 && m_steps[m_selected].tool == Tool::Arrow ? Qt::CrossCursor : resizeCursor(handle));
    else if ((m_tool == Tool::Crop && m_pendingCrop.contains(at)) || (editable && hitObject(event->position()) >= 0))
        setCursor(Qt::SizeAllCursor);
    else setCursor(m_tool == Tool::Text ? Qt::IBeamCursor : m_tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_drag != Drag::None) {
        mouseMoveEvent(event);
        if (m_drag == Drag::Object) record(m_dragBefore);
        m_drag = Drag::None;
        return;
    }
    if (!m_drawing) return;
    mouseMoveEvent(event);
    m_drawing = false;
    const QRectF span = m_current.points.boundingRect();
    const bool areaTool = m_current.tool == Tool::Crop || m_current.tool == Tool::Blur;
    const bool tooSmall = areaTool ? span.width() < 4 || span.height() < 4 : span.width() < 2 && span.height() < 2;
    if (!tooSmall) {
        if (m_current.tool == Tool::Crop) m_pendingCrop = span;
        else {
            commit(m_current);
            if (m_current.tool == Tool::Arrow || m_current.tool == Tool::Box) select(m_steps.size() - 1);
        }
    }
    update();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || (m_tool != Tool::Text && m_tool != Tool::Select)) return;
    m_drag = Drag::None;
    const int index = hitObject(event->position());
    if (index >= 0 && m_steps.at(index).tool == Tool::Text) beginText(m_steps.at(index).points.first(), index);
}

void Canvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        m_pendingCrop = {};
        if (m_drag == Drag::Object) { m_steps = m_dragBefore; m_rendered = {}; }
        m_drag = Drag::None;
        m_drawing = false;
        select(-1);
        update();
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !m_pendingCrop.isEmpty()) {
        const QRectF crop = m_pendingCrop;
        m_pendingCrop = {};
        commit({Tool::Crop, {}, {crop.topLeft(), crop.bottomRight()}, 4, 28, {}});
    } else if (m_selected >= 0 && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
        const auto before = m_steps;
        m_steps.removeAt(m_selected);
        select(-1);
        record(before);
    } else if (m_selected >= 0 && event->key() >= Qt::Key_Left && event->key() <= Qt::Key_Down) {
        const int distance = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        QPointF delta;
        if (event->key() == Qt::Key_Left) delta.setX(-distance);
        if (event->key() == Qt::Key_Right) delta.setX(distance);
        if (event->key() == Qt::Key_Up) delta.setY(-distance);
        if (event->key() == Qt::Key_Down) delta.setY(distance);
        const auto before = m_steps;
        m_steps[m_selected].points.translate(moveDelta(m_steps[m_selected].points.boundingRect(), delta, cropRect()));
        record(before);
    } else QWidget::keyPressEvent(event);
}

void Canvas::beginText(const QPointF &at, int index)
{
    finishText(true);
    select(index);
    m_textIndex = index;
    m_textDraft = index < 0 ? Step{Tool::Text, m_color, {at}, m_width, m_textSize, {}} : m_steps.at(index);
    m_textEditor->setPlainText(m_textDraft.text);
    m_textEditor->show();
    m_rendered = {};
    positionTextEditor();
    m_textEditor->setFocus(Qt::MouseFocusReason);
    m_textEditor->selectAll();
    update();
}

void Canvas::positionTextEditor()
{
    if (!m_textEditor->isVisible() || target().isEmpty()) return;
    const qreal scale = target().width() / cropRect().width();
    QFont font(QStringLiteral("IBM Plex Sans"));
    font.setPixelSize(qMax(1, qRound(m_textDraft.textSize * scale)));
    m_textEditor->setFont(font);
    QPalette palette = m_textEditor->palette();
    palette.setColor(QPalette::Text, m_textDraft.color);
    palette.setColor(QPalette::Base, Qt::transparent);
    m_textEditor->setPalette(palette);
    const auto lines = m_textEditor->toPlainText().split(QLatin1Char('\n'));
    const QFontMetricsF fm(font);
    qreal width = 100;
    for (const QString &line : lines) width = qMax(width, fm.horizontalAdvance(line) + 8);
    const QPointF at = fromImage(m_textDraft.points.first());
    const int availableWidth = qMax(1, qFloor(target().right() - at.x()));
    const int availableHeight = qMax(1, qFloor(target().bottom() - at.y()));
    m_textEditor->setGeometry(qRound(at.x()), qRound(at.y()), qMin(qCeil(width), availableWidth),
                              qMin(qCeil(fm.lineSpacing() * lines.size() + 8), availableHeight));
    update();
}

void Canvas::finishText(bool accept)
{
    if (!m_textEditor->isVisible() || m_finishingText) return;
    m_finishingText = true;
    const QString text = m_textEditor->toPlainText();
    m_textEditor->hide();
    const int index = m_textIndex;
    m_textIndex = -1;
    m_rendered = {};
    if (accept) {
        const auto before = m_steps;
        if (text.trimmed().isEmpty()) {
            if (index >= 0) m_steps.removeAt(index);
            select(-1);
        } else {
            m_textDraft.text = text;
            if (index >= 0) m_steps[index] = m_textDraft;
            else m_steps.append(m_textDraft);
            select(index >= 0 ? index : m_steps.size() - 1);
        }
        record(before);
    }
    m_preedit = false;
    m_finishingText = false;
    update();
}

void Canvas::finishEditing()
{
    finishText(true);
    if (m_drag == Drag::Object) record(m_dragBefore);
    m_drag = Drag::None;
}

bool Canvas::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_textEditor) {
        if (event->type() == QEvent::InputMethod)
            m_preedit = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
        if (event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            // Native text selection/copy/undo and letters belong to the input
            // widget. Save/New remain window actions and commit the draft.
            if (!(key->modifiers().testFlag(Qt::ControlModifier) && (key->key() == Qt::Key_S || key->key() == Qt::Key_N))) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape) {
                finishText(false);
                setFocus();
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !key->modifiers().testFlag(Qt::ShiftModifier) && !m_preedit) {
                finishText(true);
                setFocus();
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut) finishText(true);
    }
    return QWidget::eventFilter(object, event);
}

void Canvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionTextEditor();
}
