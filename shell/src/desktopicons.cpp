// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktopicons.h"

#include "fileops.h"
#include "filetypes.h"

#include <karyo/ui/desktopentry.h>
#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QClipboard>
#include <QCollator>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPainter>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace Ui = Karyo::Ui;

namespace {

constexpr int CellWidth = 100;
constexpr int CellHeight = 108;
constexpr int Margin = 12;
// The taskbar's BarHeight: its exclusive zone does not shrink a background surface.
constexpr int TaskbarHeight = 48;
constexpr int TileSize = 52;
constexpr int LabelFlags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWrapAnywhere;

QFont labelFont() { return Ui::bodyFont(12.5); }

QStringList localPaths(const QMimeData *mime)
{
    QStringList paths;
    if (!mime)
        return paths;
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile())
            paths.append(QDir::cleanPath(url.toLocalFile()));
    }
    return paths;
}

bool sameDir(const QString &a, const QString &b) { return QDir(a).canonicalPath() == QDir(b).canonicalPath(); }

void openPath(const QString &path)
{
    static QMimeDatabase db;
    const QString mime = db.mimeTypeForFile(path).name();
    if (const auto app = DesktopEntry::defaultFor(mime); app && app->launchWith({path}))
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace

DesktopIcons::DesktopIcons(QWidget *parent)
    : QWidget(parent)
    , m_dir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    QDir().mkpath(m_dir);
    // A copy or an unpacked archive changes the folder many times in a row.
    m_reload.setSingleShot(true);
    m_reload.setInterval(150);
    connect(&m_reload, &QTimer::timeout, this, &DesktopIcons::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reload, qOverload<>(&QTimer::start));
    // Tiles take their colors from the theme.
    connect(Ui::ThemeNotifier::instance(), &Ui::ThemeNotifier::changed, this, &DesktopIcons::reload);
    reload();
}

void DesktopIcons::reload()
{
    // The folder may have been removed and created again.
    if (!QFileInfo(m_dir).isDir())
        QDir().mkpath(m_dir);
    if (!m_watcher.directories().contains(m_dir))
        m_watcher.addPath(m_dir);

    QFileInfoList infos = QDir(m_dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(infos.begin(), infos.end(), [&collator](const QFileInfo &a, const QFileInfo &b) {
        if (a.isDir() != b.isDir())
            return a.isDir();
        return collator.compare(a.fileName(), b.fileName()) < 0;
    });

    const qreal dpr = devicePixelRatioF();
    const QFontMetrics metrics(labelFont());
    m_items.clear();
    QSet<QString> present;
    for (const QFileInfo &info : std::as_const(infos)) {
        Item item;
        item.info = info;
        QString name = info.fileName();
        // Launchers run only when executable, as in other desktops: a .desktop
        // file that was just downloaded opens as text.
        if (info.isFile() && info.isExecutable() && info.suffix() == QLatin1String("desktop")) {
            if (const auto entry = DesktopEntry::load(info.absoluteFilePath(), name)) {
                item.launcher = true;
                name = entry->name;
                const QIcon icon = QFileInfo(entry->icon).isAbsolute() ? QIcon(entry->icon) : QIcon::fromTheme(entry->icon);
                if (!icon.isNull())
                    item.image = icon.pixmap(QSize(48, 48), dpr);
            }
        }
        if (item.image.isNull()) {
            const FileType type = FileType::of(info);
            item.tile = type.tile;
            item.image = Ui::icon(item.launcher ? QStringLiteral("app-window") : type.glyph, 26, type.ink, dpr);
        }
        item.label = metrics.elidedText(name, Qt::ElideMiddle, (CellWidth - 14) * 2);
        present.insert(info.absoluteFilePath());
        m_items.append(item);
    }

    m_selected.intersect(present);
    m_hover = -1;
    if (m_editor && !present.contains(m_editing))
        finishRename(false);
    else if (m_editor)
        m_editor->move(labelRect(indexOf(m_editing)).topLeft() - QPoint(2, 2));
    update();
    if (!m_pendingRename.isEmpty()) {
        if (const int index = indexOf(m_pendingRename); index >= 0) {
            m_pendingRename.clear();
            setSelection({m_items.at(index).info.absoluteFilePath()});
            startRename(index);
        }
    }
}

QRect DesktopIcons::cellRect(int index) const
{
    // Columns fill from the top left, as on other desktops.
    const int rows = std::max(1, (height() - 2 * Margin - TaskbarHeight) / CellHeight);
    return QRect(Margin + index / rows * CellWidth, Margin + index % rows * CellHeight, CellWidth, CellHeight);
}

QRect DesktopIcons::labelRect(int index) const
{
    const QRect cell = cellRect(index).adjusted(3, 3, -3, -3);
    return QRect(cell.left() + 4, cell.top() + 66, cell.width() - 8, 36);
}

int DesktopIcons::itemAt(const QPoint &pos) const
{
    const int rows = std::max(1, (height() - 2 * Margin - TaskbarHeight) / CellHeight);
    if (pos.x() < Margin || pos.y() < Margin)
        return -1;
    const int column = (pos.x() - Margin) / CellWidth;
    const int row = (pos.y() - Margin) / CellHeight;
    if (row >= rows)
        return -1;
    const int index = column * rows + row;
    if (index >= m_items.size() || !cellRect(index).adjusted(3, 3, -3, -3).contains(pos))
        return -1;
    return index;
}

int DesktopIcons::indexOf(const QString &path) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).info.absoluteFilePath() == path)
            return i;
    }
    return -1;
}

void DesktopIcons::paintTile(QPainter &p, const QRectF &tile, const Item &item) const
{
    if (item.tile.isValid())
        Ui::fillRounded(p, tile, 16, item.tile);
    const QSizeF size = item.image.deviceIndependentSize();
    p.drawPixmap(QPointF(tile.center().x() - size.width() / 2, tile.center().y() - size.height() / 2), item.image);
}

void DesktopIcons::paintEvent(QPaintEvent *event)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(labelFont());
    for (int i = 0; i < m_items.size(); ++i) {
        const QRect cell = cellRect(i);
        if (!event->rect().intersects(cell))
            continue;
        const Item &item = m_items.at(i);
        const QRectF r = QRectF(cell).adjusted(3, 3, -3, -3);
        const bool selected = m_selected.contains(item.info.absoluteFilePath()) || i == m_dropTarget;
        if (selected)
            Ui::fillRounded(p, r, 10, t.sel);
        else if (i == m_hover)
            Ui::fillRounded(p, r, 10, t.hover);
        paintTile(p, QRectF(r.center().x() - TileSize / 2.0, r.top() + 8, TileSize, TileSize), item);
        if (m_editor && item.info.absoluteFilePath() == m_editing)
            continue;
        // The label sits on glass so it stays readable over any picture.
        const QRect text = labelRect(i);
        if (!selected && i != m_hover) {
            const QRect bounds = p.fontMetrics().boundingRect(text, LabelFlags, item.label);
            Ui::fillRounded(p, QRectF(bounds).adjusted(-5, -1, 5, 1), 7, t.glass);
        }
        p.setPen(t.text);
        p.drawText(text, LabelFlags, item.label);
    }
    if (m_banding && !m_band.isEmpty())
        Ui::fillRounded(p, QRectF(m_band).adjusted(0.5, 0.5, -0.5, -0.5), 4, Ui::alpha(t.accent, 14),
                        Ui::alpha(t.accent, 60));
}

void DesktopIcons::updateItem(int index)
{
    if (index >= 0 && index < m_items.size())
        update(cellRect(index));
}

void DesktopIcons::setSelection(const QSet<QString> &paths)
{
    for (int i = 0; i < m_items.size(); ++i) {
        const QString path = m_items.at(i).info.absoluteFilePath();
        if (m_selected.contains(path) != paths.contains(path))
            updateItem(i);
    }
    m_selected = paths;
}

QStringList DesktopIcons::selectedPaths() const
{
    QStringList paths;
    for (const Item &item : m_items) {
        if (m_selected.contains(item.info.absoluteFilePath()))
            paths.append(item.info.absoluteFilePath());
    }
    return paths;
}

void DesktopIcons::mousePressEvent(QMouseEvent *event)
{
    if (m_editor)
        finishRename(true);
    setFocus(Qt::MouseFocusReason);
    const QPoint pos = event->position().toPoint();
    const int index = itemAt(pos);
    const QString path = index >= 0 ? m_items.at(index).info.absoluteFilePath() : QString();
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    m_pressPos = pos;
    m_pressed = -1;
    if (event->button() == Qt::LeftButton) {
        if (index >= 0) {
            m_pressed = index;
            if (ctrl) {
                QSet<QString> selected = m_selected;
                if (!selected.remove(path))
                    selected.insert(path);
                setSelection(selected);
            } else if (!m_selected.contains(path)) {
                setSelection({path});
            }
        } else {
            if (!ctrl)
                setSelection({});
            m_banding = true;
            m_bandBase = m_selected;
            m_band = QRect();
        }
    } else if (event->button() == Qt::RightButton && !m_selected.contains(path)) {
        setSelection(index >= 0 ? QSet<QString>{path} : QSet<QString>{});
    }
}

void DesktopIcons::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (!(event->buttons() & Qt::LeftButton)) {
        if (const int index = itemAt(pos); index != m_hover) {
            updateItem(m_hover);
            m_hover = index;
            updateItem(m_hover);
        }
        return;
    }
    if (m_pressed >= 0 && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        const int pressed = std::exchange(m_pressed, -1);
        startDrag(pressed);
        return;
    }
    if (!m_banding)
        return;
    const QRect old = m_band;
    m_band = QRect(m_pressPos, pos).normalized();
    QSet<QString> selected = m_bandBase;
    for (int i = 0; i < m_items.size(); ++i) {
        if (cellRect(i).adjusted(3, 3, -3, -3).intersects(m_band))
            selected.insert(m_items.at(i).info.absoluteFilePath());
    }
    setSelection(selected);
    update(old.united(m_band).adjusted(-1, -1, 1, 1));
}

void DesktopIcons::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    // A click on one of several selected icons selects just it, unless it starts a drag.
    const int index = itemAt(event->position().toPoint());
    if (m_pressed >= 0 && index == m_pressed && !(event->modifiers() & Qt::ControlModifier))
        setSelection({m_items.at(index).info.absoluteFilePath()});
    m_pressed = -1;
    if (m_banding) {
        m_banding = false;
        update(m_band.adjusted(-1, -1, 1, 1));
        m_band = QRect();
    }
}

void DesktopIcons::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (const int index = itemAt(event->position().toPoint()); index >= 0)
        open(m_items.at(index));
}

void DesktopIcons::leaveEvent(QEvent *)
{
    updateItem(std::exchange(m_hover, -1));
}

void DesktopIcons::contextMenuEvent(QContextMenuEvent *event)
{
    int index = itemAt(event->pos());
    if (event->reason() == QContextMenuEvent::Keyboard) {
        const QStringList selected = selectedPaths();
        index = selected.isEmpty() ? -1 : indexOf(selected.first());
    }
    showMenu(event->globalPos(), index);
}

void DesktopIcons::keyPressEvent(QKeyEvent *event)
{
    // The rename editor leaves Escape to its parent.
    if (event->key() == Qt::Key_Escape) {
        if (m_editor)
            finishRename(false);
        else
            setSelection({});
    } else if (event->matches(QKeySequence::SelectAll)) {
        QSet<QString> all;
        for (const Item &item : std::as_const(m_items))
            all.insert(item.info.absoluteFilePath());
        setSelection(all);
    } else if (event->matches(QKeySequence::Copy)) {
        copySelection(false);
    } else if (event->matches(QKeySequence::Cut)) {
        copySelection(true);
    } else if (event->matches(QKeySequence::Paste)) {
        paste();
    } else if (event->matches(QKeySequence::Delete)) {
        trashSelection();
    } else if (event->key() == Qt::Key_F2) {
        const QStringList selected = selectedPaths();
        if (!selected.isEmpty())
            startRename(indexOf(selected.first()));
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        for (const QString &path : selectedPaths())
            open(m_items.at(indexOf(path)));
    } else {
        QWidget::keyPressEvent(event);
    }
}

void DesktopIcons::open(const Item &item)
{
    const QString path = item.info.absoluteFilePath();
    if (item.launcher) {
        if (const auto entry = DesktopEntry::load(path, item.info.fileName()); entry && entry->launch())
            return;
    }
    openPath(path);
}

void DesktopIcons::showMenu(const QPoint &globalPos, int index)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    if (index >= 0) {
        // Items are taken by value: the folder may be reread while the menu is open.
        const QList<Item> items = [this] {
            QList<Item> selected;
            for (const Item &item : std::as_const(m_items)) {
                if (m_selected.contains(item.info.absoluteFilePath()))
                    selected.append(item);
            }
            return selected;
        }();
        const QString first = m_items.at(index).info.absoluteFilePath();
        menu->addAction(tr("Open"), this, [this, items] {
            for (const Item &item : items)
                open(item);
        });
        menu->addSeparator();
        menu->addAction(tr("Cut"), this, [this] { copySelection(true); })->setShortcut(QKeySequence::Cut);
        menu->addAction(tr("Copy"), this, [this] { copySelection(false); })->setShortcut(QKeySequence::Copy);
        QAction *rename = menu->addAction(tr("Rename"), this, [this, first] {
            if (const int i = indexOf(first); i >= 0)
                startRename(i);
        });
        rename->setShortcut(QKeySequence(Qt::Key_F2));
        rename->setEnabled(items.size() == 1);
        menu->addAction(tr("Move to Trash"), this, &DesktopIcons::trashSelection)->setShortcut(QKeySequence::Delete);
    } else {
        menu->addAction(tr("New folder"), this, &DesktopIcons::newFolder);
        QAction *paste = menu->addAction(tr("Paste"), this, &DesktopIcons::paste);
        paste->setShortcut(QKeySequence::Paste);
        paste->setEnabled(FileClipboard::hasFiles());
        menu->addSeparator();
        menu->addAction(tr("Open in Files"), this, [this] { openPath(m_dir); });
    }
    menu->popup(globalPos);
}

void DesktopIcons::startRename(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    if (m_editor)
        finishRename(true);
    const Item &item = m_items.at(index);
    const Ui::Theme &t = Ui::Theme::current();
    m_editing = item.info.absoluteFilePath();
    m_editor = new QLineEdit(item.info.fileName(), this);
    m_editor->setFont(labelFont());
    m_editor->setAlignment(Qt::AlignHCenter);
    m_editor->setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3;"
                                           " border-radius: 6px; padding: 1px 3px; selection-background-color: %3; }")
                                .arg(t.card.name(), t.text.name(), t.accent.name()));
    const QRect label = labelRect(index);
    m_editor->setGeometry(label.left() - 2, label.top() - 2, label.width() + 4, m_editor->sizeHint().height());
    // The name without its extensions, as in Files: reads.fastq.gz -> "reads".
    const qsizetype dot = item.info.isDir() ? -1 : item.info.fileName().indexOf(QLatin1Char('.'), 1);
    m_editor->setSelection(0, dot > 0 ? int(dot) : int(item.info.fileName().size()));
    connect(m_editor, &QLineEdit::editingFinished, this, [this, editor = m_editor] {
        if (editor == m_editor)
            finishRename(true);
    });
    m_editor->show();
    m_editor->setFocus();
    updateItem(index);
}

void DesktopIcons::finishRename(bool commit)
{
    QLineEdit *editor = std::exchange(m_editor, nullptr);
    if (!editor)
        return;
    const QString path = std::exchange(m_editing, QString());
    const QString name = editor->text().trimmed();
    editor->hide();
    editor->deleteLater();
    setFocus();
    updateItem(indexOf(path));
    const QString old = QFileInfo(path).fileName();
    if (!commit || name.isEmpty() || name == old)
        return;
    if (name.contains(QLatin1Char('/')) || name == QLatin1String(".") || name == QLatin1String("..")) {
        warn(tr("A name cannot contain “/” or be “.” or “..”."));
        return;
    }
    const QString target = QDir(m_dir).filePath(name);
    if (QFileInfo::exists(target)) {
        warn(tr("“%1” already exists on the desktop.").arg(name));
        return;
    }
    if (!QFile::rename(path, target)) {
        warn(tr("Could not rename “%1”.").arg(old));
        return;
    }
    m_selected = {target};
}

void DesktopIcons::trashSelection()
{
    QStringList failed;
    for (const QString &path : selectedPaths()) {
        QFile file(path);
        if (!file.moveToTrash())
            failed.append(QFileInfo(path).fileName());
    }
    if (!failed.isEmpty())
        warn(tr("Could not move to Trash: %1").arg(failed.join(QStringLiteral(", "))));
}

void DesktopIcons::newFolder()
{
    const QString name = FileJob::freeName(m_dir, tr("New folder"));
    if (!QDir(m_dir).mkdir(name)) {
        warn(tr("Could not create “%1”.").arg(name));
        return;
    }
    m_pendingRename = QDir(m_dir).filePath(name);
    reload();
}

void DesktopIcons::copySelection(bool cut)
{
    const QStringList paths = selectedPaths();
    if (!paths.isEmpty())
        FileClipboard::set(paths, cut);
}

void DesktopIcons::paste()
{
    bool cut = false;
    const QStringList sources = FileClipboard::paths(&cut);
    if (sources.isEmpty())
        return;
    copyOrMove(sources, m_dir, cut);
    if (cut)
        QApplication::clipboard()->clear();
}

void DesktopIcons::startDrag(int index)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty() || index < 0 || index >= m_items.size())
        return;
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls.append(QUrl::fromLocalFile(path));
    mime->setUrls(urls);

    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(QSize(TileSize, TileSize) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    {
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        paintTile(p, QRectF(0, 0, TileSize, TileSize), m_items.at(index));
    }
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(TileSize / 2, TileSize / 2));
    drag->exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::CopyAction);
}

bool DesktopIcons::dropTarget(const QPoint &pos, const QMimeData *mime, bool ownDrag, int *target) const
{
    *target = -1;
    const QStringList paths = localPaths(mime);
    if (paths.isEmpty())
        return false;
    if (const int index = itemAt(pos); index >= 0) {
        const Item &item = m_items.at(index);
        if ((item.info.isDir() || item.launcher) && !paths.contains(item.info.absoluteFilePath())) {
            *target = index;
            return true;
        }
    }
    // The grid arranges itself: own icons dropped on the desktop stay where they are.
    return !ownDrag;
}

Qt::DropAction DesktopIcons::dropAction(const QDropEvent *event, bool ownDrag) const
{
    // Copy by default and move with Shift, as Files does with other folders;
    // icons dragged into a desktop folder move unless Ctrl is held.
    const Qt::KeyboardModifiers modifiers = event->modifiers() | QGuiApplication::queryKeyboardModifiers();
    const bool move = ownDrag ? !(modifiers & Qt::ControlModifier) : bool(modifiers & Qt::ShiftModifier);
    Qt::DropAction action = move ? Qt::MoveAction : Qt::CopyAction;
    if (!(event->possibleActions() & action))
        action = event->possibleActions() & Qt::CopyAction ? Qt::CopyAction : Qt::MoveAction;
    return action;
}

void DesktopIcons::dragEnterEvent(QDragEnterEvent *event)
{
    if (localPaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    dragMoveEvent(event);
    // Moves keep coming while the pointer is over icons that refuse the drop.
    event->accept();
}

void DesktopIcons::dragMoveEvent(QDragMoveEvent *event)
{
    const bool own = event->source() == this;
    int target = -1;
    const bool accepted = dropTarget(event->position().toPoint(), event->mimeData(), own, &target);
    if (target != m_dropTarget) {
        updateItem(m_dropTarget);
        m_dropTarget = target;
        updateItem(m_dropTarget);
    }
    if (!accepted) {
        event->setDropAction(Qt::IgnoreAction);
        event->ignore();
        return;
    }
    event->setDropAction(target >= 0 && m_items.at(target).launcher ? Qt::CopyAction : dropAction(event, own));
    event->accept();
}

void DesktopIcons::dragLeaveEvent(QDragLeaveEvent *)
{
    updateItem(std::exchange(m_dropTarget, -1));
}

void DesktopIcons::dropEvent(QDropEvent *event)
{
    const bool own = event->source() == this;
    int target = -1;
    const bool accepted = dropTarget(event->position().toPoint(), event->mimeData(), own, &target);
    updateItem(std::exchange(m_dropTarget, -1));
    if (!accepted) {
        event->ignore();
        return;
    }
    const QStringList paths = localPaths(event->mimeData());
    if (target >= 0 && m_items.at(target).launcher) {
        const Item &item = m_items.at(target);
        if (const auto entry = DesktopEntry::load(item.info.absoluteFilePath(), item.info.fileName()))
            entry->launchWith(paths);
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    const QString dir = target >= 0 ? m_items.at(target).info.absoluteFilePath() : m_dir;
    const Qt::DropAction action = dropAction(event, own);
    // Files already in the folder stay as they are rather than turn into "name (2)".
    QStringList sources;
    for (const QString &path : paths) {
        if (!sameDir(QFileInfo(path).absolutePath(), dir))
            sources.append(path);
    }
    copyOrMove(sources, dir, action == Qt::MoveAction);
    event->setDropAction(action);
    event->accept();
}

void DesktopIcons::copyOrMove(const QStringList &sources, const QString &targetDir, bool move)
{
    if (sources.isEmpty())
        return;
    FileJob *job = FileJob::start(move ? FileJob::Kind::Move : FileJob::Kind::Copy, sources, targetDir);
    connect(job, &FileJob::finished, this, [this](const QStringList &errors) {
        if (!errors.isEmpty())
            warn(errors.join(QLatin1Char('\n')));
    });
}

void DesktopIcons::warn(const QString &text)
{
    // A window of its own: dialogs cannot be children of a layer surface.
    auto *box = new QMessageBox(QMessageBox::Warning, tr("Desktop"), text, QMessageBox::Ok);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->show();
}
