// SPDX-License-Identifier: GPL-3.0-or-later
#include "startwidgets.h"

#include "symbols.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QStyleHints>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

constexpr int GridColumns = 6;
constexpr int GridRowHeight = 84;
constexpr int RecentRowHeight = 48;
constexpr int AppRowHeight = 48;
constexpr int HeadingHeight = 30;
constexpr int BestRowHeight = 68;
constexpr int ResultRowHeight = 42;

// Strings of Start's rows and actions, in the StartActions context.
class Text
{
    Q_DECLARE_TR_FUNCTIONS(StartActions)
};

// Text with the first case-insensitive occurrence of query drawn in weight 600.
void drawMatched(QPainter &painter, const QRect &rect, const QString &text, const QString &query,
                 const QFont &font, const QColor &color)
{
    QFont bold = font;
    bold.setWeight(QFont::DemiBold);
    const int at = query.isEmpty() ? -1 : int(text.indexOf(query, 0, Qt::CaseInsensitive));
    painter.setPen(color);
    if (at < 0) {
        painter.setFont(font);
        painter.drawText(rect, Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(font).elidedText(text, Qt::ElideRight, rect.width()));
        return;
    }
    const QString parts[3] = {text.left(at), text.mid(at, query.size()), text.mid(at + query.size())};
    int x = rect.x();
    for (int i = 0; i < 3; ++i) {
        const QFont &f = i == 1 ? bold : font;
        const QFontMetrics metrics(f);
        const int room = rect.right() - x;
        if (room <= 0)
            break;
        const QString piece = metrics.elidedText(parts[i], Qt::ElideRight, room);
        painter.setFont(f);
        painter.drawText(QRect(x, rect.y(), room, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, piece);
        x += metrics.horizontalAdvance(piece);
        if (piece != parts[i])
            break;
    }
}

void drawTag(QPainter &painter, const QRectF &rect, const QString &text, const QFont &font,
             const QColor &fill, const QColor &ink)
{
    Ui::fillPill(painter, rect, fill);
    painter.setFont(font);
    painter.setPen(ink);
    painter.drawText(rect, Qt::AlignCenter, text);
}

qreal tagWidth(const QString &text, const QFont &font)
{
    return QFontMetrics(font).horizontalAdvance(text) + 18;
}

// Where a tool comes from: Debian, or the conda channel (bioconda).
QString sourceLabel(const AppInfo &tool)
{
    if (tool.source == QLatin1String("apt"))
        return Text::tr("Debian");
    return tool.source;
}

// Tile of a file: type fill with its glyph.
void drawFileTile(QPainter &painter, const QRectF &tile, const FileHit &file, qreal dpr)
{
    const auto colors = StartActions::fileTile(file);
    Ui::fillRounded(painter, tile, tile.width() * 11 / 36, colors.first);
    const int size = qRound(tile.width() * 18 / 38);
    painter.drawPixmap(QPointF(tile.center().x() - size / 2.0, tile.center().y() - size / 2.0),
                       Ui::icon(StartActions::fileGlyph(file), size, colors.second, dpr));
}

// Dark mono tile of a command, like the terminal's.
void drawCommandTile(QPainter &painter, const QRectF &tile, const QString &command)
{
    Ui::fillRounded(painter, tile, tile.width() * 11 / 36, Ui::neutral(800));
    painter.setFont(Ui::monoFont(tile.width() * 0.3, QFont::DemiBold));
    painter.setPen(Ui::sage(200));
    painter.drawText(tile, Qt::AlignCenter, command.left(2));
}

// Primary (accent) or outlined action of the preview card.
class ActionButton : public QAbstractButton
{
public:
    ActionButton(const QString &glyph, const QString &text, bool primary, QWidget *parent)
        : QAbstractButton(parent)
        , m_glyph(glyph)
        , m_primary(primary)
    {
        setText(text);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(38);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool hover = underMouse();
        QColor ink = t.text;
        if (m_primary) {
            Ui::fillPill(painter, r, isDown() ? Ui::accent(700) : hover ? Ui::accent(600) : t.accent);
            ink = Ui::neutral(100);
        } else {
            Ui::fillRounded(painter, r, r.height() / 2, isDown() ? t.press : hover ? t.hover : Qt::transparent,
                            Ui::alpha(t.text, 18));
        }
        int x = 16;
        if (!m_glyph.isEmpty()) {
            painter.drawPixmap(x, (height() - 16) / 2, Ui::icon(m_glyph, 16, ink, devicePixelRatioF()));
            x += 16 + 10;
        }
        painter.setFont(m_primary ? Ui::headingFont(14) : Ui::bodyFont(13.5, QFont::DemiBold));
        painter.setPen(ink);
        painter.drawText(QRect(x, 0, width() - x - 12, height()), Qt::AlignVCenter | Qt::AlignLeft,
                         text());
    }

private:
    QString m_glyph;
    bool m_primary;
};

// Tile, name, subtitle and description at the top of the preview card.
class PreviewHeader : public QWidget
{
public:
    explicit PreviewHeader(QWidget *parent)
        : QWidget(parent)
    {
    }

    void set(const SearchResult &result, const QString &subtitle, const QString &description)
    {
        m_result = result;
        m_subtitle = subtitle;
        m_description = description;
        setFixedHeight(heightFor(width() > 0 ? width() : 226));
        update();
    }

    int heightFor(int width) const
    {
        int h = 68 + 14 + 36 + 22;
        if (!m_description.isEmpty()) {
            const QRect bounds = QFontMetrics(Ui::bodyFont(13))
                                     .boundingRect(QRect(0, 0, width, 1000), Qt::TextWordWrap, m_description);
            h += 12 + qMin(bounds.height(), 5 * 20);
        }
        return h;
    }

protected:
    void resizeEvent(QResizeEvent *) override { setFixedHeight(heightFor(width())); }

    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF tile(0, 0, 68, 68);
        switch (m_result.kind) {
        case SearchResult::App:
        case SearchResult::Setting:
            m_result.app.style.paint(painter, tile, devicePixelRatioF());
            break;
        case SearchResult::Command:
        case SearchResult::Run:
            drawCommandTile(painter, tile, m_result.command);
            break;
        case SearchResult::File:
            drawFileTile(painter, tile, m_result.file, devicePixelRatioF());
            break;
        case SearchResult::Symbol:
            StartActions::drawSymbolTile(painter, tile, m_result.command);
            break;
        }
        const QFont title = Ui::headingFont(26);
        painter.setFont(title);
        painter.setPen(t.text);
        int y = 68 + 14;
        painter.drawText(QRect(0, y, width(), 36), Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(title).elidedText(m_result.title, Qt::ElideRight, width()));
        y += 36;
        painter.setFont(Ui::bodyFont(12.5));
        painter.setPen(t.muted);
        painter.drawText(QRect(0, y, width(), 22), Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(painter.font()).elidedText(m_subtitle, Qt::ElideRight, width()));
        y += 22 + 12;
        if (!m_description.isEmpty()) {
            painter.setFont(Ui::bodyFont(13));
            painter.setPen(t.text);
            painter.drawText(QRect(0, y, width(), height() - y), Qt::TextWordWrap | Qt::AlignTop,
                             m_description);
        }
    }

private:
    SearchResult m_result;
    QString m_subtitle;
    QString m_description;
};

// "Category" caption over one or more tags.
class TagsRow : public QWidget
{
public:
    TagsRow(const QString &caption, const QStringList &tags, QWidget *parent)
        : QWidget(parent)
        , m_caption(caption)
        , m_tags(tags)
    {
        setFixedHeight(18 + 8 + 24);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(Ui::bodyFont(12));
        painter.setPen(t.muted);
        painter.drawText(QRect(0, 0, width(), 18), Qt::AlignVCenter | Qt::AlignLeft, m_caption);
        const QFont font = Ui::bodyFont(11.5, QFont::DemiBold);
        qreal x = 0;
        for (const QString &tag : m_tags) {
            const qreal w = tagWidth(tag, font);
            if (x + w > width())
                break;
            drawTag(painter, QRectF(x, 26, w, 24), tag, font, t.tint2, t.ink2);
            x += w + 6;
        }
    }

private:
    QString m_caption;
    QStringList m_tags;
};

} // namespace

// ---------------------------------------------------------------- LinkButton

LinkButton::LinkButton(const QString &text, const QString &leadingGlyph, bool chevron, QWidget *parent)
    : QAbstractButton(parent)
    , m_glyph(leadingGlyph)
    , m_chevron(chevron)
{
    setText(text);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(sizeHint());
}

QSize LinkButton::sizeHint() const
{
    int w = 14 + QFontMetrics(Ui::bodyFont(12.5, QFont::DemiBold)).horizontalAdvance(text()) + 14;
    if (!m_glyph.isEmpty())
        w += 14 + 6;
    if (m_chevron)
        w += 4 + 14 - 4;
    return {w, 30};
}

void LinkButton::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    Ui::fillRounded(painter, r, r.height() / 2,
                    isDown() ? t.press : underMouse() ? t.hover : Ui::alpha(Ui::neutral(100), 60),
                    Ui::alpha(t.text, 14));
    int x = 14;
    const qreal dpr = devicePixelRatioF();
    if (!m_glyph.isEmpty()) {
        painter.drawPixmap(x, (height() - 14) / 2, Ui::icon(m_glyph, 14, t.text, dpr));
        x += 14 + 6;
    }
    const QFont font = Ui::bodyFont(12.5, QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(t.text);
    const int textWidth = QFontMetrics(font).horizontalAdvance(text());
    painter.drawText(QRect(x, 0, textWidth + 1, height()), Qt::AlignVCenter, text());
    if (m_chevron)
        painter.drawPixmap(x + textWidth + 4, (height() - 14) / 2,
                           Ui::icon(QStringLiteral("chevron-right"), 14, t.text, dpr));
}

// ---------------------------------------------------------------- ChipButton

ChipButton::ChipButton(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true);
    setAutoExclusive(true);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(sizeHint());
}

QSize ChipButton::sizeHint() const
{
    return {QFontMetrics(Ui::bodyFont(13, QFont::DemiBold)).horizontalAdvance(text()) + 28, 32};
}

void ChipButton::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor fill = t.card;
    QColor ink = t.text;
    if (isChecked()) {
        fill = t.accent;
        ink = Ui::neutral(100);
    } else if (isDown()) {
        fill = Ui::mix(t.text, t.card, 13);
    } else if (underMouse()) {
        fill = Ui::mix(t.text, t.card, 6);
    }
    Ui::fillPill(painter, rect(), fill);
    painter.setFont(Ui::bodyFont(13, QFont::DemiBold));
    painter.setPen(ink);
    painter.drawText(rect(), Qt::AlignCenter, text());
}

// ---------------------------------------------------------------- PinnedGrid

PinnedGrid::PinnedGrid(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void PinnedGrid::setApps(const QList<AppInfo> &apps)
{
    m_apps = apps.mid(0, 3 * GridColumns);
    m_hover = -1;
    m_pressed = -1;
    m_dragged = -1;
    updateGeometry();
    update();
}

int PinnedGrid::heightForWidth(int) const
{
    const int rows = qMax(1, int((m_apps.size() + GridColumns - 1) / GridColumns));
    return rows * GridRowHeight;
}

QSize PinnedGrid::sizeHint() const { return {640, heightForWidth(640)}; }

QRect PinnedGrid::cellRect(int index) const
{
    const qreal w = width() / qreal(GridColumns);
    const int column = index % GridColumns;
    const int row = index / GridColumns;
    return QRect(qRound(column * w), row * GridRowHeight, qRound(w), GridRowHeight - 4);
}

int PinnedGrid::cellAt(const QPoint &pos) const
{
    for (int i = 0; i < m_apps.size(); ++i) {
        if (cellRect(i).contains(pos))
            return i;
    }
    return -1;
}

int PinnedGrid::dropIndexAt(const QPoint &pos) const
{
    const qreal w = width() / qreal(GridColumns);
    const int column = qBound(0, int(pos.x() / w), GridColumns - 1);
    const int row = qMax(0, pos.y() / GridRowHeight);
    return qBound(0, row * GridColumns + column, int(m_apps.size()) - 1);
}

void PinnedGrid::paintTile(QPainter &painter, const AppInfo &app, const QRect &cell) const
{
    const QFont font = Ui::bodyFont(12);
    const QFontMetrics metrics(font);
    const QRectF tile(cell.center().x() - 18, cell.y() + 8, 36, 36);
    app.style.paint(painter, tile, devicePixelRatioF());
    // Up to two lines, the second elided.
    const QRect label(cell.x() + 6, cell.y() + 50, cell.width() - 12, 2 * metrics.lineSpacing());
    const QString name = app.entry.name;
    QString text = name;
    if (metrics.horizontalAdvance(name) > label.width()) {
        const QRect bounds = metrics.boundingRect(label, Qt::TextWordWrap | Qt::AlignHCenter, name);
        if (bounds.height() > label.height())
            text = metrics.elidedText(name, Qt::ElideRight, label.width() * 2 - 12);
    }
    painter.setFont(font);
    painter.setPen(Ui::Theme::current().text);
    painter.drawText(label, Qt::TextWordWrap | Qt::AlignHCenter | Qt::AlignTop, text);
}

void PinnedGrid::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < m_apps.size(); ++i) {
        const QRect cell = cellRect(i);
        if (i == m_dragged) {
            // The slot the dragged tile will take.
            Ui::fillRounded(painter, cell.adjusted(4, 0, -4, 0), 6, t.hover);
            continue;
        }
        if (i == m_hover && m_dragged < 0)
            Ui::fillRounded(painter, cell.adjusted(4, 0, -4, 0), 6, t.hover);
        paintTile(painter, m_apps.at(i), cell);
    }
    if (m_dragged >= 0) {
        QRect cell = cellRect(m_dragged);
        cell.moveCenter(m_dragPos);
        painter.setOpacity(0.85);
        paintTile(painter, m_apps.at(m_dragged), cell);
    }
}

void PinnedGrid::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = cellAt(event->position().toPoint());
        m_pressPos = event->position().toPoint();
    }
}

void PinnedGrid::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (m_pressed >= 0 && m_dragged < 0 && (event->buttons() & Qt::LeftButton)
        && (pos - m_pressPos).manhattanLength() >= QGuiApplication::styleHints()->startDragDistance()) {
        m_dragged = m_pressed;
        m_hover = -1;
    }
    if (m_dragged >= 0) {
        // The other tiles make room as the tile moves over them.
        m_dragPos = pos;
        const int target = dropIndexAt(pos);
        if (target != m_dragged) {
            m_apps.move(m_dragged, target);
            m_dragged = target;
        }
        update();
        return;
    }
    const int hover = cellAt(pos);
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void PinnedGrid::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const int pressed = m_pressed;
    m_pressed = -1;
    if (m_dragged >= 0) {
        m_dragged = -1;
        m_hover = cellAt(event->position().toPoint());
        update();
        QStringList ids;
        for (const AppInfo &app : std::as_const(m_apps))
            ids.append(app.entry.id);
        Q_EMIT reordered(ids);
        return;
    }
    const int index = cellAt(event->position().toPoint());
    if (index >= 0 && index == pressed)
        Q_EMIT activated(m_apps.at(index));
}

void PinnedGrid::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void PinnedGrid::contextMenuEvent(QContextMenuEvent *event)
{
    const int index = cellAt(event->pos());
    if (index >= 0)
        Q_EMIT contextMenuRequested(m_apps.at(index), event->globalPos());
}

// ---------------------------------------------------------------- RecentList

RecentList::RecentList(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RecentList::setFiles(const QList<FileHit> &files)
{
    m_files = files.mid(0, 6);
    m_hover = -1;
    const int rows = qMax(1, int((m_files.size() + 1) / 2));
    setFixedHeight(rows * RecentRowHeight);
    update();
}

QSize RecentList::sizeHint() const { return {640, height()}; }

QRect RecentList::cellRect(int index) const
{
    const int w = width() / 2;
    return QRect((index % 2) * w, (index / 2) * RecentRowHeight, w - 8, RecentRowHeight - 4);
}

int RecentList::cellAt(const QPoint &pos) const
{
    for (int i = 0; i < m_files.size(); ++i) {
        if (cellRect(i).contains(pos))
            return i;
    }
    return -1;
}

void RecentList::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_files.isEmpty()) {
        painter.setFont(Ui::bodyFont(13));
        painter.setPen(t.muted);
        painter.drawText(rect().adjusted(4, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         Text::tr("Files you open will show up here."));
        return;
    }
    const QFont name = Ui::bodyFont(13, QFont::DemiBold);
    const QFont meta = Ui::bodyFont(12);
    for (int i = 0; i < m_files.size(); ++i) {
        const FileHit &file = m_files.at(i);
        const QRect cell = cellRect(i);
        if (i == m_hover)
            Ui::fillRounded(painter, cell, 6, t.hover);
        drawFileTile(painter, QRectF(cell.x() + 4, cell.center().y() - 16, 32, 32), file,
                     devicePixelRatioF());
        const int x = cell.x() + 4 + 32 + 12;
        const int w = cell.right() - x - 8;
        painter.setFont(name);
        painter.setPen(t.text);
        painter.drawText(QRect(x, cell.y() + 6, w, 18), Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(name).elidedText(QFileInfo(file.path).fileName(), Qt::ElideMiddle, w));
        painter.setFont(meta);
        painter.setPen(t.muted);
        painter.drawText(QRect(x, cell.y() + 24, w, 16), Qt::AlignVCenter | Qt::AlignLeft,
                         StartActions::relativeTime(file.modified));
    }
}

void RecentList::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = cellAt(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void RecentList::mouseReleaseEvent(QMouseEvent *event)
{
    const int index = cellAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index >= 0)
        Q_EMIT activated(m_files.at(index).path);
}

void RecentList::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ---------------------------------------------------------------- AppList

AppList::AppList(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void AppList::setApps(const QList<AppInfo> &apps)
{
    m_apps = apps;
    m_hover = -1;
    setFixedHeight(qMax(1, int(m_apps.size())) * AppRowHeight);
    update();
}

QSize AppList::sizeHint() const { return {400, height()}; }

int AppList::rowAt(const QPoint &pos) const
{
    const int row = pos.y() / AppRowHeight;
    return pos.y() >= 0 && row < m_apps.size() ? row : -1;
}

void AppList::paintEvent(QPaintEvent *event)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QFont name = Ui::bodyFont(13, QFont::DemiBold);
    const QFont meta = Ui::bodyFont(12);
    const QFont tagFont = Ui::monoFont(11, QFont::DemiBold);
    const QFont envFont = Ui::bodyFont(11.5, QFont::DemiBold);
    for (int i = 0; i < m_apps.size(); ++i) {
        const QRect row(0, i * AppRowHeight, width(), AppRowHeight);
        if (!row.intersects(event->rect()))
            continue;
        const AppInfo &app = m_apps.at(i);
        if (i == m_hover)
            Ui::fillRounded(painter, row.adjusted(0, 2, 0, -2), 6, t.hover);
        app.style.paint(painter, QRectF(10, row.center().y() - 16, 32, 32), devicePixelRatioF());
        // At the right: the conda environment, or "Install" for a tool that
        // is missing, then left of it the kind.
        qreal right = width() - 12;
        if (app.isTool() && (!app.installed || !app.envs.isEmpty())) {
            const QString where = app.installed ? app.envs.first() : Text::tr("Install");
            const qreal w = tagWidth(where, envFont);
            const QRectF rect(right - w, row.center().y() - 10, w, 20);
            if (app.installed)
                drawTag(painter, rect, where, envFont, t.tint2, t.ink2);
            else
                drawTag(painter, rect, where, envFont, t.tint1, t.ink1);
            right = rect.left() - 6;
        }
        const QString tag = app.entry.terminal ? QStringLiteral("CLI") : QStringLiteral("GUI");
        const qreal tagW = tagWidth(tag, tagFont);
        const QRectF tagRect(right - tagW, row.center().y() - 10, tagW, 20);
        drawTag(painter, tagRect, tag, tagFont, Ui::alpha(t.text, 8), t.muted);
        const int x = 10 + 32 + 12;
        const int w = int(tagRect.left()) - x - 12;
        painter.setFont(name);
        painter.setPen(t.text);
        painter.drawText(QRect(x, row.y() + 6, w, 18), Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(name).elidedText(app.entry.name, Qt::ElideRight, w));
        const QString description = !app.entry.comment.isEmpty() ? app.entry.comment : app.entry.genericName;
        painter.setFont(meta);
        painter.setPen(t.muted);
        painter.drawText(QRect(x, row.y() + 24, w, 16), Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(meta).elidedText(description, Qt::ElideRight, w));
    }
}

void AppList::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = rowAt(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void AppList::mouseReleaseEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && row >= 0)
        Q_EMIT activated(m_apps.at(row));
}

void AppList::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void AppList::contextMenuEvent(QContextMenuEvent *event)
{
    const int row = rowAt(event->pos());
    if (row >= 0)
        Q_EMIT contextMenuRequested(m_apps.at(row), event->globalPos());
}

// ---------------------------------------------------------------- ResultList

ResultList::ResultList(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void ResultList::setResults(const QList<QPair<QString, QList<SearchResult>>> &sections,
                            const QString &query, const QString &emptyText)
{
    m_rows.clear();
    m_emptyText = emptyText;
    m_query = query;
    m_current = -1;
    m_hover = -1;
    int y = 0;
    for (qsizetype s = 0; s < sections.size(); ++s) {
        const auto &section = sections.at(s);
        if (section.second.isEmpty())
            continue;
        if (s > 0)
            y += 6;
        Row heading;
        heading.heading = true;
        heading.text = section.first;
        heading.y = y;
        heading.height = HeadingHeight;
        m_rows.append(heading);
        y += HeadingHeight;
        for (const SearchResult &result : section.second) {
            Row row;
            row.best = s == 0;
            row.result = result;
            row.y = y;
            row.height = row.best ? BestRowHeight : ResultRowHeight;
            if (m_current < 0)
                m_current = int(m_rows.size());
            m_rows.append(row);
            y += row.height;
        }
    }
    setFixedHeight(qMax(y, 60));
    update();
    Q_EMIT currentChanged();
}

const SearchResult *ResultList::current() const
{
    return m_current >= 0 && m_current < m_rows.size() ? &m_rows.at(m_current).result : nullptr;
}

void ResultList::moveCurrent(int delta)
{
    if (m_current < 0)
        return;
    int index = m_current;
    do {
        index += delta;
    } while (index >= 0 && index < m_rows.size() && m_rows.at(index).heading);
    if (index < 0 || index >= m_rows.size())
        return;
    m_current = index;
    update();
    Q_EMIT currentChanged();
}

QSize ResultList::sizeHint() const { return {360, height()}; }

int ResultList::rowAt(const QPoint &pos) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        if (!row.heading && pos.y() >= row.y && pos.y() < row.y + row.height)
            return i;
    }
    return -1;
}

void ResultList::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal dpr = devicePixelRatioF();
    if (m_rows.isEmpty() && (!m_query.isEmpty() || !m_emptyText.isEmpty())) {
        painter.setFont(Ui::bodyFont(13.5));
        painter.setPen(t.muted);
        painter.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         m_query.isEmpty() ? m_emptyText : Text::tr("No results for “%1”").arg(m_query));
        return;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        const QRect rect(0, row.y, width(), row.height);
        if (row.heading) {
            painter.setFont(Ui::bodyFont(12));
            painter.setPen(t.muted);
            painter.drawText(rect.adjusted(12, 0, 0, 0), Qt::AlignBottom | Qt::AlignLeft, row.text);
            continue;
        }
        const SearchResult &r = row.result;
        if (i == m_current)
            Ui::fillRounded(painter, rect, row.best ? 16 : 12, t.sel);
        else if (i == m_hover)
            Ui::fillRounded(painter, rect, row.best ? 16 : 12, t.hover);

        if (row.best) {
            const QRectF tile(12, rect.center().y() - 22, 44, 44);
            QString detail;
            switch (r.kind) {
            case SearchResult::App:
                r.app.style.paint(painter, tile, dpr);
                if (r.app.isTool())
                    detail = Text::tr("Command-line tool · %1").arg(sourceLabel(r.app));
                else
                    detail = !r.app.entry.genericName.isEmpty() ? r.app.entry.genericName
                                                                : AppIndex::labelFor(r.app.category);
                break;
            case SearchResult::Setting:
                r.app.style.paint(painter, tile, dpr);
                detail = Text::tr("Settings");
                break;
            case SearchResult::Command:
                drawCommandTile(painter, tile, r.command);
                detail = Text::tr("Command-line tool · %1").arg(r.trailing);
                break;
            case SearchResult::Run:
                drawCommandTile(painter, tile, r.command);
                detail = Text::tr("Run in Terminal");
                break;
            case SearchResult::File:
                drawFileTile(painter, tile, r.file, dpr);
                detail = StartActions::fileKind(r.file) + QStringLiteral(" · ") + r.trailing;
                break;
            case SearchResult::Symbol:
                StartActions::drawSymbolTile(painter, tile, r.command);
                detail = Text::tr("Symbol · %1 · Enter copies it").arg(r.trailing);
                break;
            }
            const int x = 12 + 44 + 14;
            const QFont title = (r.kind == SearchResult::Run) ? Ui::monoFont(13.5) : Ui::bodyFont(14.5);
            drawMatched(painter, QRect(x, rect.y() + 13, width() - x - 12, 22), r.title, m_query, title,
                        r.kind == SearchResult::Run ? t.text : t.selInk);
            painter.setFont(Ui::bodyFont(12.5));
            painter.setPen(t.muted);
            painter.drawText(QRect(x, rect.y() + 35, width() - x - 12, 20), Qt::AlignVCenter | Qt::AlignLeft,
                             QFontMetrics(painter.font()).elidedText(detail, Qt::ElideRight, width() - x - 12));
            continue;
        }

        int x = 12;
        switch (r.kind) {
        case SearchResult::App:
        case SearchResult::Setting:
            r.app.style.paint(painter, QRectF(x - 2, rect.center().y() - 11, 22, 22), dpr);
            break;
        case SearchResult::Command:
        case SearchResult::Run:
            painter.drawPixmap(x, rect.center().y() - 9,
                               Ui::icon(QStringLiteral("square-terminal"), 18, t.muted, dpr));
            break;
        case SearchResult::File:
            painter.drawPixmap(x, rect.center().y() - 9,
                               Ui::icon(StartActions::fileGlyph(r.file), 18, t.muted, dpr));
            break;
        case SearchResult::Symbol:
            painter.setFont(Ui::bodyFont(r.command.size() > 1 ? 11.5 : 16, QFont::DemiBold));
            painter.setPen(t.text);
            painter.drawText(QRect(x - 6, rect.y(), 30, rect.height()), Qt::AlignCenter, r.command);
            break;
        }
        x += 18 + 12;
        const QFont trailingFont = Ui::bodyFont(12);
        // +2: elision measures fractional advances that round up.
        const int trailingW = r.trailing.isEmpty()
                                  ? 0
                                  : qMin(QFontMetrics(trailingFont).horizontalAdvance(r.trailing) + 2, width() / 3);
        const bool mono = r.kind == SearchResult::Command || r.kind == SearchResult::Run;
        drawMatched(painter, QRect(x, rect.y(), width() - x - trailingW - 24, rect.height()), r.title, m_query,
                    mono ? Ui::monoFont(13.5) : Ui::bodyFont(13.5), t.text);
        if (trailingW > 0) {
            painter.setFont(trailingFont);
            painter.setPen(t.muted);
            painter.drawText(QRect(width() - 12 - trailingW, rect.y(), trailingW, rect.height()),
                             Qt::AlignVCenter | Qt::AlignRight,
                             QFontMetrics(trailingFont).elidedText(r.trailing, Qt::ElideMiddle, trailingW));
        }
    }
}

void ResultList::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = rowAt(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void ResultList::mouseReleaseEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position().toPoint());
    if (event->button() != Qt::LeftButton || row < 0)
        return;
    m_current = row;
    update();
    Q_EMIT currentChanged();
    Q_EMIT activated(m_rows.at(row).result);
}

void ResultList::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ---------------------------------------------------------------- PreviewCard

PreviewCard::PreviewCard(AppIndex *index, QWidget *parent)
    : QWidget(parent)
    , m_index(index)
    , m_layout(new QVBoxLayout(this))
{
    setFixedWidth(270);
    m_layout->setContentsMargins(22, 22, 22, 22);
    m_layout->setSpacing(8);
}

void PreviewCard::addAction(const QString &glyph, const QString &text, bool primary,
                            const std::function<void()> &run)
{
    auto *button = new ActionButton(glyph, text, primary, this);
    connect(button, &QAbstractButton::clicked, this, [this, run] {
        run();
        Q_EMIT done();
    });
    m_layout->addWidget(button);
    m_parts.append(button);
}

void PreviewCard::setResult(const SearchResult *result)
{
    qDeleteAll(m_parts);
    m_parts.clear();
    m_valid = result != nullptr;
    if (!result) {
        update();
        return;
    }
    m_result = *result;
    const SearchResult r = *result;

    QString subtitle;
    QString description;
    QString caption;
    QStringList tags;
    switch (r.kind) {
    case SearchResult::App:
        if (r.app.isTool()) {
            subtitle = Text::tr("Command-line tool · %1").arg(sourceLabel(r.app));
            description = r.app.entry.comment;
            if (r.app.installed) {
                caption = Text::tr("Installed in");
                tags = r.app.envs;
                if (!AppIndex::commandPath(r.app.entry.exec).isEmpty())
                    tags.prepend(Text::tr("System"));
            }
            break;
        }
        subtitle = r.app.entry.terminal ? Text::tr("Command-line app") : Text::tr("App");
        if (!r.app.entry.genericName.isEmpty())
            subtitle += QStringLiteral(" · ") + r.app.entry.genericName;
        description = r.app.entry.comment;
        caption = Text::tr("Category");
        tags = {AppIndex::labelFor(r.app.category)};
        break;
    case SearchResult::Setting:
        subtitle = Text::tr("Settings");
        description = r.app.entry.comment;
        break;
    case SearchResult::Command:
        subtitle = Text::tr("Command-line tool");
        // A tool's subcommand ("samtools view") describes the tool.
        description = r.app.isTool() ? r.app.entry.comment : AppIndex::commandPath(r.command);
        break;
    case SearchResult::Run:
        subtitle = Text::tr("Command line");
        description = r.command;
        break;
    case SearchResult::Symbol:
        subtitle = Text::tr("Symbol") + QStringLiteral(" · ") + r.trailing;
        description = Text::tr("Copy it, then paste it anywhere with Ctrl+V.");
        break;
    case SearchResult::File:
        subtitle = StartActions::fileKind(r.file);
        if (!r.file.directory)
            subtitle += QStringLiteral(" · ") + QLocale().formattedDataSize(r.file.size);
        description = QDir::toNativeSeparators(QFileInfo(r.file.path).absolutePath());
        caption = Text::tr("Modified");
        tags = {QLocale().toString(r.file.modified, QLocale::ShortFormat)};
        break;
    }

    auto *header = new PreviewHeader(this);
    m_layout->addWidget(header);
    m_parts.append(header);
    header->set(r, subtitle, description);
    auto *gap = new QWidget(this);
    gap->setFixedHeight(6);
    m_layout->addWidget(gap);
    m_parts.append(gap);

    switch (r.kind) {
    case SearchResult::App:
    case SearchResult::Setting: {
        const AppInfo app = r.app;
        if (app.isTool()) {
            const QString id = app.toolId;
            if (!app.installed) {
                addAction(QStringLiteral("package"), Text::tr("Install in App Center"), true,
                          [id] { StartActions::showInAppCenter(id); });
                break;
            }
            addAction(QStringLiteral("square-terminal"), Text::tr("Open in Terminal"), true,
                      [app] { StartActions::launch(app); });
            const QString command = app.entry.exec;
            if (StartActions::hasManual(command))
                addAction(QStringLiteral("book-open"), Text::tr("Read manual"), false,
                          [command] { StartActions::readManual(command); });
            addAction(QStringLiteral("package"), Text::tr("Show in App Center"), false,
                      [id] { StartActions::showInAppCenter(id); });
        } else {
            addAction(QStringLiteral("app-window"), Text::tr("Open"), true, [app] { StartActions::launch(app); });
        }
        if (r.kind == SearchResult::App) {
            const bool pinned = m_index->isPinned(app.entry.id);
            AppIndex *index = m_index;
            addAction(QStringLiteral("pin"), pinned ? Text::tr("Unpin from Start") : Text::tr("Pin to Start"),
                      false, [index, app, pinned] { index->setPinned(app.entry.id, !pinned); });
        }
        break;
    }
    case SearchResult::Command: {
        const QString command = r.command;
        addAction(QStringLiteral("square-terminal"), Text::tr("Open in Terminal"), true,
                  [command] { StartActions::runInTerminal(command); });
        const QString manual = r.app.isTool() ? r.app.entry.exec : command;
        if (StartActions::hasManual(manual))
            addAction(QStringLiteral("book-open"), Text::tr("Read manual"), false,
                      [manual] { StartActions::readManual(manual); });
        break;
    }
    case SearchResult::Run: {
        const QString line = r.command;
        addAction(QStringLiteral("square-terminal"), Text::tr("Run in Terminal"), true,
                  [line] { StartActions::runInTerminal(line); });
        break;
    }
    case SearchResult::File: {
        const QString path = r.file.path;
        addAction(QStringLiteral("app-window"), Text::tr("Open"), true, [path] { StartActions::openFile(path); });
        addAction(QStringLiteral("folder"), Text::tr("Open file location"), false,
                  [path] { StartActions::openLocation(path); });
        addAction(QStringLiteral("copy"), Text::tr("Copy path"), false, [path] { StartActions::copyPath(path); });
        break;
    }
    case SearchResult::Symbol: {
        const QString symbol = r.command;
        addAction(QStringLiteral("copy"), Text::tr("Copy"), true, [symbol] { StartActions::copyPath(symbol); });
        break;
    }
    }
    if (!caption.isEmpty()) {
        auto *spacer = new QWidget(this);
        spacer->setFixedHeight(14);
        m_layout->addWidget(spacer);
        m_parts.append(spacer);
        auto *row = new TagsRow(caption, tags, this);
        m_layout->addWidget(row);
        m_parts.append(row);
    }
    auto *stretch = new QWidget(this);
    stretch->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_layout->addWidget(stretch);
    m_parts.append(stretch);
    update();
}

void PreviewCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Ui::fillRounded(painter, rect(), 8, Ui::Theme::current().card);
}

// ---------------------------------------------------------------- StartActions

namespace StartActions {

namespace {

QString terminalProgram()
{
    const QString configured = qEnvironmentVariable("TERMINAL");
    if (!configured.isEmpty() && !QStandardPaths::findExecutable(configured).isEmpty())
        return configured;
    QString own = QStandardPaths::findExecutable(QStringLiteral("karyo-terminal"));
    if (own.isEmpty())
        own = QStandardPaths::findExecutable(QStringLiteral("openkaryon-terminal"));
    return own.isEmpty() ? QStringLiteral("x-terminal-emulator") : own;
}

} // namespace

QString toolCommandLine(const AppInfo &tool, const QString &arguments)
{
    QString line = tool.entry.exec;
    if (!arguments.isEmpty())
        line += QLatin1Char(' ') + arguments;
    // Only in a conda environment: activate it first.
    if (!tool.envs.isEmpty() && AppIndex::commandPath(tool.entry.exec).isEmpty())
        line = QStringLiteral(". '%1/etc/profile.d/conda.sh' && conda activate '%2' && %3")
                   .arg(tool.condaRoot, tool.envs.first(), line);
    return line;
}

void showInAppCenter(const QString &toolId)
{
    DesktopEntry::launchProgram(QStringLiteral("openkaryon-appcenter"), {QStringLiteral("--app"), toolId});
}

void launch(const AppInfo &app)
{
    if (!app.isTool()) {
        app.entry.launch();
    } else if (app.installed) {
        // Without arguments the tools print their usage; the shell stays.
        runInTerminal(toolCommandLine(app));
    } else {
        showInAppCenter(app.toolId);
    }
}

void openFile(const QString &path)
{
    const QFileInfo info(path);
    const QString mime = info.isDir() ? QStringLiteral("inode/directory")
                                      : QMimeDatabase().mimeTypeForFile(info).name();
    if (const auto entry = DesktopEntry::defaultFor(mime); entry && entry->launchWith({path}))
        return;
    DesktopEntry::launchProgram(QStringLiteral("xdg-open"), {path});
}

void openLocation(const QString &path)
{
    // Our Files opens a file's folder with the file selected; others get the folder.
    const auto entry = DesktopEntry::defaultFor(QStringLiteral("inode/directory"));
    const QString target = entry && entry->id == QLatin1String("openkaryon-files.desktop")
                               ? path
                               : QFileInfo(path).absolutePath();
    if (entry && entry->launchWith({target}))
        return;
    DesktopEntry::launchProgram(QStringLiteral("xdg-open"), {QFileInfo(path).absolutePath()});
}

void runInTerminal(const QString &commandLine)
{
    // The shell stays open after the command, so its output can be read.
    DesktopEntry::launchProgram(terminalProgram(),
                            {QStringLiteral("-e"), QStringLiteral("sh"), QStringLiteral("-c"),
                             commandLine + QStringLiteral("; exec \"${SHELL:-/bin/bash}\"")});
}

void readManual(const QString &command)
{
    DesktopEntry::launchProgram(terminalProgram(), {QStringLiteral("-e"), QStringLiteral("man"), command});
}

bool hasManual(const QString &command)
{
    for (const QString &root : {QStringLiteral("/usr/share/man"), QStringLiteral("/usr/local/share/man")}) {
        for (const QString &section : {QStringLiteral("1"), QStringLiteral("8"), QStringLiteral("6")}) {
            if (QFileInfo::exists(QStringLiteral("%1/man%2/%3.%2.gz").arg(root, section, command))
                || QFileInfo::exists(QStringLiteral("%1/man%2/%3.%2").arg(root, section, command)))
                return true;
        }
    }
    return false;
}

void copyPath(const QString &path) { QGuiApplication::clipboard()->setText(path); }

void drawSymbolTile(QPainter &painter, const QRectF &tile, const QString &symbol)
{
    const Ui::Theme &t = Ui::Theme::current();
    Ui::fillRounded(painter, tile, tile.width() * 0.3, t.card);
    const qreal size = tile.height() * (symbol.size() > 1 ? 0.36 : 0.55);
    painter.setFont(Ui::bodyFont(size, QFont::DemiBold));
    painter.setPen(t.text);
    painter.drawText(tile, Qt::AlignCenter, symbol);
}

QString fileKind(const FileHit &file)
{
    if (file.directory)
        return Text::tr("Folder");
    const QString comment = QMimeDatabase().mimeTypeForFile(file.path, QMimeDatabase::MatchExtension).comment();
    return comment.isEmpty() ? Text::tr("File") : comment;
}

QString fileGlyph(const FileHit &file)
{
    if (file.directory)
        return QStringLiteral("folder");
    const QString name = QFileInfo(file.path).fileName().toLower();
    static const QStringList sequences = {
        QStringLiteral(".fastq"), QStringLiteral(".fq"), QStringLiteral(".fasta"), QStringLiteral(".fa"),
        QStringLiteral(".fna"),   QStringLiteral(".bam"), QStringLiteral(".sam"),  QStringLiteral(".cram"),
        QStringLiteral(".vcf"),   QStringLiteral(".bcf"), QStringLiteral(".gff"),  QStringLiteral(".gtf"),
    };
    for (const QString &ext : sequences) {
        if (name.endsWith(ext) || name.contains(ext + QLatin1Char('.')))
            return QStringLiteral("dna");
    }
    const QString mime = QMimeDatabase().mimeTypeForFile(file.path, QMimeDatabase::MatchExtension).name();
    if (mime.startsWith(QLatin1String("image/")))
        return QStringLiteral("image");
    if (mime == QLatin1String("text/csv") || mime == QLatin1String("text/tab-separated-values")
        || mime.contains(QLatin1String("spreadsheet")))
        return QStringLiteral("table");
    if (mime == QLatin1String("text/html"))
        return QStringLiteral("globe");
    if (mime.contains(QLatin1String("zip")) || mime.contains(QLatin1String("tar"))
        || mime.contains(QLatin1String("compressed")))
        return QStringLiteral("file-archive");
    if (mime.startsWith(QLatin1String("text/x-")) || mime.contains(QLatin1String("yaml"))
        || mime.contains(QLatin1String("json")) || mime.contains(QLatin1String("script"))
        || name == QLatin1String("snakefile") || name == QLatin1String("makefile"))
        return QStringLiteral("file-code");
    return QStringLiteral("file-text");
}

QPair<QColor, QColor> fileTile(const FileHit &file)
{
    const QString glyph = fileGlyph(file);
    if (glyph == QLatin1String("folder"))
        return {Ui::accent(300), Ui::accent(800)};
    if (glyph == QLatin1String("dna"))
        return {Ui::sage(200), Ui::sage(800)};
    if (glyph == QLatin1String("file-code"))
        return {Ui::sage(100), Ui::sage(700)};
    if (glyph == QLatin1String("table"))
        return {Ui::sage(100), Ui::sage(800)};
    if (glyph == QLatin1String("image") || glyph == QLatin1String("globe"))
        return {Ui::accent(100), Ui::accent(700)};
    return {Ui::neutral(200), Ui::neutral(700)};
}

QString relativeTime(const QDateTime &when)
{
    if (!when.isValid())
        return {};
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 seconds = when.secsTo(now);
    const QLocale locale;
    if (seconds < 60)
        return Text::tr("Just now");
    if (seconds < 3600)
        return Text::tr("%n min ago", nullptr, int(seconds / 60));
    if (when.date() == now.date())
        return seconds < 6 * 3600 ? Text::tr("%n h ago", nullptr, int(seconds / 3600))
                                  : Text::tr("Today, %1").arg(locale.toString(when.time(), QLocale::ShortFormat));
    if (when.date() == now.date().addDays(-1))
        return Text::tr("Yesterday, %1").arg(locale.toString(when.time(), QLocale::ShortFormat));
    return locale.toString(when.date(), QStringLiteral("MMM d"));
}

} // namespace StartActions
