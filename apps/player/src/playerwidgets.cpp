// SPDX-License-Identifier: GPL-3.0-or-later
#include "playerwidgets.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWheelEvent>

namespace Ui = Karyo::Ui;

namespace {

constexpr int RowHeight = 52;
constexpr int CardWidth = 232;
constexpr int CardGap = 16;
constexpr int CardTextHeight = 52;

} // namespace

// ---------------------------------------------------------------- SeekBar

SeekBar::SeekBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(20);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
}

void SeekBar::setRange(qint64 maximum)
{
    m_maximum = qMax<qint64>(0, maximum);
    update();
}

void SeekBar::setValue(qint64 value)
{
    if (m_dragging)
        return;
    m_value = qBound<qint64>(0, value, m_maximum);
    update();
}

qint64 SeekBar::valueAt(int x) const
{
    const int span = qMax(1, width() - 12);
    return qBound<qint64>(0, qint64(double(x - 6) / span * m_maximum), m_maximum);
}

void SeekBar::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track(6, height() / 2.0 - 2, width() - 12, 4);
    Ui::fillPill(p, track, Ui::alpha(t.text, 14));
    const qreal fraction = m_maximum > 0 ? double(m_value) / m_maximum : 0;
    const QRectF done(track.left(), track.top(), track.width() * fraction, track.height());
    if (done.width() > 0)
        Ui::fillPill(p, done, isEnabled() ? t.accent : t.muted);
    // The knob shows while the pointer is on the bar.
    if ((underMouse() || m_dragging) && m_maximum > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.accent);
        p.drawEllipse(QPointF(done.right(), height() / 2.0), 7, 7);
    }
}

void SeekBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_maximum <= 0)
        return;
    m_dragging = true;
    m_value = valueAt(event->position().toPoint().x());
    update();
    Q_EMIT moved(m_value);
}

void SeekBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    m_value = valueAt(event->position().toPoint().x());
    update();
    Q_EMIT moved(m_value);
}

void SeekBar::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
    update();
}

void SeekBar::wheelEvent(QWheelEvent *event)
{
    if (m_maximum <= 0)
        return;
    const qint64 step = qMax<qint64>(1, m_maximum / 20);
    m_value = qBound<qint64>(0, m_value + (event->angleDelta().y() > 0 ? step : -step), m_maximum);
    update();
    Q_EMIT moved(m_value);
}

// ---------------------------------------------------------------- PlayButton

PlayButton::PlayButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setFixedSize(48, 48);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::NoFocus);
}

void PlayButton::setPlaying(bool playing)
{
    m_playing = playing;
    setToolTip(playing ? tr("Pause (Space)") : tr("Play (Space)"));
    update();
}

void PlayButton::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Ui::fillPill(p, rect(), isDown() ? Ui::accent(700) : underMouse() ? Ui::accent(600) : t.accent);
    const QString glyph = m_playing ? QStringLiteral("pause") : QStringLiteral("play");
    // The play triangle looks centered a little to the right.
    p.drawPixmap(m_playing ? 14 : 15, 14, Ui::icon(glyph, 20, Ui::neutral(100), devicePixelRatioF()));
}

// ---------------------------------------------------------------- TrackList

TrackList::TrackList(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void TrackList::setItems(const QList<MediaItem> &items)
{
    m_items = items;
    m_hover = -1;
    setFixedHeight(qMax(1, int(m_items.size()) * RowHeight));
    update();
}

void TrackList::setPlaying(const QString &path)
{
    m_playing = path;
    update();
}

QSize TrackList::sizeHint() const { return {600, int(m_items.size()) * RowHeight}; }

void TrackList::paintEvent(QPaintEvent *event)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFont number = Ui::monoFont(12);
    const QFont title = Ui::bodyFont(13.5, QFont::DemiBold);
    const QFont meta = Ui::bodyFont(12.5);
    const int first = qMax(0, event->rect().top() / RowHeight);
    const int last = qMin(int(m_items.size()) - 1, event->rect().bottom() / RowHeight);
    const qreal dpr = devicePixelRatioF();
    for (int i = first; i <= last; ++i) {
        const MediaItem &item = m_items.at(i);
        const QRect row(0, i * RowHeight, width(), RowHeight);
        const bool playing = item.path == m_playing;
        if (playing)
            Ui::fillRounded(p, QRectF(row).adjusted(0, 2, 0, -2), 14, t.sel);
        else if (i == m_hover)
            Ui::fillRounded(p, QRectF(row).adjusted(0, 2, 0, -2), 14, t.hover);
        if (playing) {
            p.drawPixmap(16, row.center().y() - 8, Ui::icon(QStringLiteral("volume-2"), 16, t.accent, dpr));
        } else {
            p.setFont(number);
            p.setPen(t.muted);
            p.drawText(QRect(8, row.y(), 32, RowHeight), Qt::AlignCenter, QString::number(i + 1));
        }
        const int x = 52;
        const int formatWidth = 64;
        const int w = width() - x - formatWidth - 16;
        p.setFont(title);
        p.setPen(playing ? t.selInk : t.text);
        p.drawText(QRect(x, row.y() + 8, w, 20), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(title).elidedText(item.title, Qt::ElideRight, w));
        p.setFont(meta);
        p.setPen(t.muted);
        p.drawText(QRect(x, row.y() + 27, w, 18), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(meta).elidedText(item.subtitle, Qt::ElideRight, w));
        p.setFont(Ui::monoFont(11, QFont::DemiBold));
        p.drawText(QRect(width() - formatWidth - 12, row.y(), formatWidth, RowHeight),
                   Qt::AlignVCenter | Qt::AlignRight, item.format);
    }
}

int TrackList::rowAt(const QPoint &pos) const
{
    const int row = pos.y() / RowHeight;
    return row >= 0 && row < m_items.size() ? row : -1;
}

void TrackList::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = rowAt(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void TrackList::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position().toPoint());
    if (row >= 0)
        Q_EMIT activated(row);
}

void TrackList::mouseReleaseEvent(QMouseEvent *) {}

void TrackList::contextMenuEvent(QContextMenuEvent *event)
{
    const int row = rowAt(event->pos());
    if (row >= 0)
        Q_EMIT contextMenuRequested(row, event->globalPos());
}

void TrackList::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ---------------------------------------------------------------- VideoGrid

VideoGrid::VideoGrid(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void VideoGrid::setItems(const QList<MediaItem> &items)
{
    m_items = items;
    m_hover = -1;
    updateGeometry();
    update();
}

QRect VideoGrid::cardRect(int index, int width) const
{
    const int columns = qMax(1, (width + CardGap) / (CardWidth + CardGap));
    const int cardWidth = (width - (columns - 1) * CardGap) / columns;
    const int cardHeight = cardWidth * 9 / 16 + CardTextHeight;
    return QRect((index % columns) * (cardWidth + CardGap), (index / columns) * (cardHeight + CardGap), cardWidth,
                 cardHeight);
}

int VideoGrid::heightForWidth(int width) const
{
    return m_items.isEmpty() ? 0 : cardRect(int(m_items.size()) - 1, width).bottom() + 1;
}

QSize VideoGrid::sizeHint() const { return {3 * (CardWidth + CardGap), heightForWidth(3 * (CardWidth + CardGap))}; }

void VideoGrid::paintEvent(QPaintEvent *event)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal dpr = devicePixelRatioF();
    const QLocale locale;
    for (int i = 0; i < m_items.size(); ++i) {
        const QRect card = cardRect(i, width());
        if (!card.intersects(event->rect()))
            continue;
        const MediaItem &item = m_items.at(i);
        const QRect picture(card.left(), card.top(), card.width(), card.width() * 9 / 16);
        Ui::fillRounded(p, QRectF(picture), 8, i == m_hover ? Ui::neutral(800) : Ui::neutral(900));
        p.drawPixmap(picture.center().x() - 16, picture.center().y() - 16,
                     Ui::icon(i == m_hover ? QStringLiteral("circle-play") : QStringLiteral("film"), 32,
                              Ui::neutral(300), dpr));
        p.setFont(Ui::monoFont(10.5, QFont::DemiBold));
        p.setPen(Ui::neutral(300));
        p.drawText(picture.adjusted(12, 0, -12, -10), Qt::AlignBottom | Qt::AlignRight, item.format);
        p.setFont(Ui::bodyFont(13.5, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(QRect(card.left() + 4, picture.bottom() + 8, card.width() - 8, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(p.font()).elidedText(item.title, Qt::ElideRight, card.width() - 8));
        p.setFont(Ui::bodyFont(12.5));
        p.setPen(t.muted);
        p.drawText(QRect(card.left() + 4, picture.bottom() + 28, card.width() - 8, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   locale.formattedDataSize(item.size, 1, QLocale::DataSizeTraditionalFormat) + QStringLiteral(" · ")
                       + locale.toString(item.modified.date(), QLocale::ShortFormat));
    }
}

int VideoGrid::cardAt(const QPoint &pos) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (cardRect(i, width()).contains(pos))
            return i;
    }
    return -1;
}

void VideoGrid::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = cardAt(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void VideoGrid::mouseReleaseEvent(QMouseEvent *event)
{
    const int card = cardAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && card >= 0)
        Q_EMIT activated(card);
}

void VideoGrid::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ---------------------------------------------------------------- VideoView

namespace {

inline uchar clamp8(int value) { return uchar(value < 0 ? 0 : value > 255 ? 255 : value); }

// Planar or semi-planar YUV 4:2:0 to RGB32, every step-th pixel. The
// coefficients are BT.709 for HD and BT.601 below it, limited range, in
// 8.8 fixed point.
QImage fromYuv420(const uchar *y, int yStride, const uchar *u, const uchar *v, int uvStride, int uvStep, int width,
                  int height, int step, bool hd)
{
    const int rv = hd ? 459 : 409, gu = hd ? 55 : 100, gv = hd ? 136 : 208, bu = hd ? 541 : 516;
    QImage image(width / step, height / step, QImage::Format_RGB32);
    for (int row = 0; row < image.height(); ++row) {
        const int sy = row * step;
        const uchar *yLine = y + sy * yStride;
        const uchar *uLine = u + (sy / 2) * uvStride;
        const uchar *vLine = v + (sy / 2) * uvStride;
        auto *out = reinterpret_cast<QRgb *>(image.scanLine(row));
        for (int col = 0; col < image.width(); ++col) {
            const int sx = col * step;
            const int c = 298 * (yLine[sx] - 16);
            const int d = uLine[(sx / 2) * uvStep] - 128;
            const int e = vLine[(sx / 2) * uvStep] - 128;
            out[col] = qRgb(clamp8((c + rv * e + 128) >> 8), clamp8((c - gu * d - gv * e + 128) >> 8),
                            clamp8((c + bu * d + 128) >> 8));
        }
    }
    return image;
}

} // namespace

VideoView::VideoView(QWidget *parent)
    : QWidget(parent)
    , m_sink(new QVideoSink(this))
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoView::showFrame);
}

void VideoView::showFrame(const QVideoFrame &source)
{
    // QVideoFrame::toImage() would convert on the GPU and load a software
    // OpenGL (llvmpipe, some 70 MB) where there is none; the common decoder
    // formats are converted here instead.
    QVideoFrame frame(source);
    if (!frame.isValid() || !frame.map(QVideoFrame::ReadOnly)) {
        m_frame = QImage();
        update();
        return;
    }
    const int width = frame.width();
    const int height = frame.height();
    // A frame twice the size of the view is read every other pixel.
    const int step = width >= 2 * this->width() * devicePixelRatioF() && height >= 2 * this->height() * devicePixelRatioF() ? 2 : 1;
    const bool hd = height >= 720;
    switch (frame.pixelFormat()) {
    case QVideoFrameFormat::Format_YUV420P:
        m_frame = fromYuv420(frame.bits(0), frame.bytesPerLine(0), frame.bits(1), frame.bits(2), frame.bytesPerLine(1),
                             1, width, height, step, hd);
        break;
    case QVideoFrameFormat::Format_YV12:
        m_frame = fromYuv420(frame.bits(0), frame.bytesPerLine(0), frame.bits(2), frame.bits(1), frame.bytesPerLine(1),
                             1, width, height, step, hd);
        break;
    case QVideoFrameFormat::Format_NV12:
        m_frame = fromYuv420(frame.bits(0), frame.bytesPerLine(0), frame.bits(1), frame.bits(1) + 1,
                             frame.bytesPerLine(1), 2, width, height, step, hd);
        break;
    case QVideoFrameFormat::Format_NV21:
        m_frame = fromYuv420(frame.bits(0), frame.bytesPerLine(0), frame.bits(1) + 1, frame.bits(1),
                             frame.bytesPerLine(1), 2, width, height, step, hd);
        break;
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRX8888:
        m_frame = QImage(frame.bits(0), width, height, frame.bytesPerLine(0), QImage::Format_RGB32).copy();
        break;
    case QVideoFrameFormat::Format_RGBA8888:
    case QVideoFrameFormat::Format_RGBX8888:
        m_frame = QImage(frame.bits(0), width, height, frame.bytesPerLine(0), QImage::Format_RGBX8888).copy();
        break;
    default:
        frame.unmap();
        m_frame = source.toImage();
        update();
        return;
    }
    frame.unmap();
    update();
}

void VideoView::clear()
{
    m_frame = QImage();
    update();
}

void VideoView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_frame.isNull())
        return;
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QSize size = m_frame.size();
    size.scale(this->size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    p.drawImage(target, m_frame);
}

void VideoView::mouseDoubleClickEvent(QMouseEvent *) { Q_EMIT doubleClicked(); }

// ---------------------------------------------------------------- CoverArt

CoverArt::CoverArt(int size, QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(size, size);
}

void CoverArt::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void CoverArt::setGlyph(const QString &glyph)
{
    m_glyph = glyph;
    update();
}

void CoverArt::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const qreal radius = width() >= 120 ? 28 : 12;
    if (m_image.isNull()) {
        Ui::fillRounded(p, QRectF(rect()), radius, Ui::accent(300));
        const int glyph = width() / 3;
        p.drawPixmap((width() - glyph) / 2, (height() - glyph) / 2,
                     Ui::icon(m_glyph, glyph, Ui::accent(800), devicePixelRatioF()));
        return;
    }
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), radius, radius);
    p.setClipPath(clip);
    const QImage cover = m_image.scaled(size() * devicePixelRatioF(), Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
    p.drawImage(rect(), cover,
                QRect((cover.width() - width() * devicePixelRatioF()) / 2,
                      (cover.height() - height() * devicePixelRatioF()) / 2, width() * devicePixelRatioF(),
                      height() * devicePixelRatioF()));
}
