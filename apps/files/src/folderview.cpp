// SPDX-License-Identifier: GPL-3.0-or-later
#include "folderview.h"

#include "filetypes.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QCollator>
#include <QDir>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QListView>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace Ui = Karyo::Ui;

namespace {

enum Column { NameColumn = 0, SizeColumn = 1, TypeColumn = 2, DateColumn = 3 };

const FileType &typeOf(const QFileInfo &info)
{
    // Type lookups hit the MIME database; cache them per path and mtime.
    static QHash<QString, QPair<qint64, FileType>> cache;
    const QString key = info.absoluteFilePath();
    const qint64 stamp = info.lastModified().toMSecsSinceEpoch();
    auto it = cache.find(key);
    if (it == cache.end() || it->first != stamp) {
        if (cache.size() > 20000)
            cache.clear();
        it = cache.insert(key, {stamp, FileType::of(info)});
    }
    return it->second;
}

void drawTile(QPainter *painter, const QRectF &tile, const FileType &type, int glyph, qreal radius)
{
    Ui::fillRounded(*painter, tile, radius, type.tile);
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    painter->drawPixmap(QPointF(tile.center().x() - glyph / 2.0, tile.center().y() - glyph / 2.0),
                        Ui::icon(type.glyph, glyph, type.ink, dpr));
}

} // namespace

// Folders first, then natural order ("run2" before "run10").
class FolderProxy : public QSortFilterProxyModel
{
public:
    explicit FolderProxy(QObject *parent)
        : QSortFilterProxyModel(parent)
    {
        m_collator.setNumericMode(true);
        m_collator.setCaseSensitivity(Qt::CaseInsensitive);
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setRecursiveFilteringEnabled(false);
    }
    void setNameFilter(const QString &text)
    {
        m_filter = text;
        invalidateFilter();
    }
    void setRootPath(const QString &path) { m_root = path; }
    void setDropHandler(std::function<void(const QStringList &, const QString &, bool)> handler)
    {
        m_dropHandler = std::move(handler);
    }
    // QFileSystemModel copies single files only, and blocks while it does; a
    // file job copies folders too, in its thread, with progress.
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        if (!m_dropHandler || (action != Qt::CopyAction && action != Qt::MoveAction))
            return QSortFilterProxyModel::dropMimeData(data, action, row, column, parent);
        QStringList sources;
        for (const QUrl &url : data->urls()) {
            if (url.isLocalFile())
                sources.append(url.toLocalFile());
        }
        if (sources.isEmpty())
            return false;
        auto *fs = static_cast<QFileSystemModel *>(sourceModel());
        const QModelIndex folder = mapToSource(parent);
        const QString target = folder.isValid() && fs->isDir(folder) ? fs->filePath(folder) : m_root;
        m_dropHandler(sources, target, action == Qt::MoveAction);
        return true;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            switch (section) {
            case NameColumn: return FolderView::tr("Name");
            case DateColumn: return FolderView::tr("Date modified");
            case TypeColumn: return FolderView::tr("Type");
            case SizeColumn: return FolderView::tr("Size");
            }
        }
        return QSortFilterProxyModel::headerData(section, orientation, role);
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (m_filter.isEmpty())
            return true;
        auto *fs = static_cast<QFileSystemModel *>(sourceModel());
        const QModelIndex index = fs->index(row, 0, parent);
        // Only children of the shown folder are filtered; its ancestors must stay.
        if (fs->filePath(parent) != m_root)
            return true;
        return fs->fileName(index).contains(m_filter, Qt::CaseInsensitive);
    }
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        auto *fs = static_cast<QFileSystemModel *>(sourceModel());
        const bool leftDir = fs->isDir(left);
        const bool rightDir = fs->isDir(right);
        if (leftDir != rightDir)
            return sortOrder() == Qt::AscendingOrder ? leftDir : rightDir;
        switch (left.column()) {
        case SizeColumn:
            if (fs->size(left) != fs->size(right))
                return fs->size(left) < fs->size(right);
            break;
        case DateColumn:
            if (fs->lastModified(left) != fs->lastModified(right))
                return fs->lastModified(left) < fs->lastModified(right);
            break;
        case TypeColumn: {
            const int c = m_collator.compare(typeOf(fs->fileInfo(left)).label, typeOf(fs->fileInfo(right)).label);
            if (c != 0)
                return c < 0;
            break;
        }
        default:
            break;
        }
        return m_collator.compare(fs->fileName(left), fs->fileName(right)) < 0;
    }

private:
    QCollator m_collator;
    QString m_filter;
    QString m_root;
    std::function<void(const QStringList &, const QString &, bool)> m_dropHandler;
};

namespace {

class RenameDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        if (index.column() != NameColumn)
            return nullptr;
        m_editing = index;
        auto *editor = new QLineEdit(parent);
        editor->setObjectName(QStringLiteral("fileRenameEditor"));
        editor->setFont(Ui::bodyFont(13));
        editor->setAutoFillBackground(true);
        const Ui::Theme &t = Ui::Theme::current();
        QPalette palette = editor->palette();
        palette.setColor(QPalette::Base, t.win);
        palette.setColor(QPalette::Text, t.text);
        palette.setColor(QPalette::Highlight, t.selInk);
        palette.setColor(QPalette::HighlightedText, t.win);
        editor->setPalette(palette);
        editor->setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                                             "border-radius: 2px; padding: 1px 3px; }")
                                 .arg(t.win.name(), t.text.name(), t.accent.name()));
        connect(editor, &QObject::destroyed, parent, [this, parent] {
            m_editing = QPersistentModelIndex();
            parent->update();
        });
        return editor;
    }

    void setEditorData(QWidget *widget, const QModelIndex &index) const override
    {
        QStyledItemDelegate::setEditorData(widget, index);
        auto *editor = qobject_cast<QLineEdit *>(widget);
        if (!editor)
            return;
        const auto *proxy = static_cast<const QSortFilterProxyModel *>(index.model());
        const auto *fs = static_cast<const QFileSystemModel *>(proxy->sourceModel());
        const QFileInfo info = fs->fileInfo(proxy->mapToSource(index));
        const QString name = info.fileName();
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        const int length = !info.isDir() && dot > 0 ? dot : name.size();
        editor->setSelection(0, length);
        // The view gives the editor focus after setEditorData(), which can
        // select the whole QLineEdit again. Apply basename selection last.
        QTimer::singleShot(0, editor, [editor, length] { editor->setSelection(0, length); });
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        editor->setGeometry(editorRect(option));
    }

protected:
    virtual QRect editorRect(const QStyleOptionViewItem &option) const = 0;
    bool editing(const QModelIndex &index) const { return m_editing == index; }

private:
    mutable QPersistentModelIndex m_editing;
};

class DetailsDelegate : public RenameDelegate
{
public:
    using RenameDelegate::RenameDelegate;

    QRect editorRect(const QStyleOptionViewItem &option) const override
    {
        return option.rect.adjusted(34, 2, -4, -2);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(30);
        return s;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

        const auto *proxy = static_cast<const QSortFilterProxyModel *>(index.model());
        const auto *fs = static_cast<const QFileSystemModel *>(proxy->sourceModel());
        const QModelIndex source = proxy->mapToSource(index);
        const QFileInfo info = fs->fileInfo(source);
        const bool selected = option.state & QStyle::State_Selected;
        const QRect r = option.rect;
        painter->save();
        switch (source.column()) {
        case NameColumn: {
            const FileType &type = typeOf(info);
            drawTile(painter, QRectF(r.left() + 8, r.center().y() - 10, 20, 20), type, 13, 6);
            if (editing(index))
                break;
            painter->setFont(Ui::bodyFont(13, selected ? QFont::DemiBold : QFont::Normal));
            painter->setPen(info.isHidden() ? t.muted : t.text);
            const QRect text(r.left() + 36, r.top(), r.width() - 42, r.height());
            painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                              painter->fontMetrics().elidedText(info.fileName(), Qt::ElideMiddle, text.width()));
            break;
        }
        case DateColumn:
            painter->setFont(Ui::bodyFont(12.5));
            painter->setPen(t.muted);
            painter->drawText(r.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                              QLocale().toString(info.lastModified(), QStringLiteral("MMM d, yyyy HH:mm")));
            break;
        case TypeColumn:
            painter->setFont(Ui::bodyFont(12.5));
            painter->setPen(t.muted);
            painter->drawText(r.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                              painter->fontMetrics().elidedText(typeOf(info).label, Qt::ElideRight, r.width() - 16));
            break;
        case SizeColumn:
            if (!info.isDir()) {
                painter->setFont(Ui::bodyFont(12.5));
                painter->setPen(t.muted);
                painter->drawText(r.adjusted(8, 0, -14, 0), Qt::AlignVCenter | Qt::AlignRight, formatSize(info.size()));
            }
            break;
        }
        painter->restore();
    }
};

class IconsDelegate : public RenameDelegate
{
public:
    using RenameDelegate::RenameDelegate;

    QRect editorRect(const QStyleOptionViewItem &option) const override
    {
        return QRect(option.rect.left() + 7, option.rect.top() + 67, option.rect.width() - 14, 28);
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {116, 112}; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const Ui::Theme &t = Ui::Theme::current();
        const auto *proxy = static_cast<const QSortFilterProxyModel *>(index.model());
        const auto *fs = static_cast<const QFileSystemModel *>(proxy->sourceModel());
        const QFileInfo info = fs->fileInfo(proxy->mapToSource(index));
        const QRectF r = QRectF(option.rect).adjusted(3, 3, -3, -3);
        if (option.state & QStyle::State_Selected)
            Ui::fillRounded(*painter, r, 8, t.sel);
        else if (option.state & QStyle::State_MouseOver)
            Ui::fillRounded(*painter, r, 8, t.hover);
        drawTile(painter, QRectF(r.center().x() - 26, r.top() + 8, 52, 52), typeOf(info), 26, 16);
        if (editing(index))
            return;
        painter->save();
        painter->setFont(Ui::bodyFont(12.5));
        painter->setPen(info.isHidden() ? t.muted : t.text);
        painter->drawText(QRectF(r.left() + 6, r.top() + 66, r.width() - 12, 36),
                          Qt::AlignHCenter | Qt::AlignTop | Qt::TextWrapAnywhere,
                          painter->fontMetrics().elidedText(info.fileName(), Qt::ElideMiddle, int(r.width() - 12) * 2));
        painter->restore();
    }
};

} // namespace

FolderView::FolderView(QWidget *parent)
    : QWidget(parent)
    , m_model(new QFileSystemModel(this))
    , m_proxy(new FolderProxy(this))
    , m_stack(new QStackedWidget(this))
    , m_details(new QTreeView)
    , m_icons(new QListView)
{
    m_model->setReadOnly(false);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    m_model->setOption(QFileSystemModel::DontUseCustomDirectoryIcons);
    m_proxy->setSourceModel(m_model);
    m_proxy->setDynamicSortFilter(true);
    m_proxy->setDropHandler([this](const QStringList &sources, const QString &target, bool move) {
        Q_EMIT filesDropped(sources, target, move);
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    for (QAbstractItemView *view : {static_cast<QAbstractItemView *>(m_details), static_cast<QAbstractItemView *>(m_icons)}) {
        view->setModel(m_proxy);
        view->setFrameShape(QFrame::NoFrame);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
        view->setDragDropMode(QAbstractItemView::DragDrop);
        view->setDefaultDropAction(Qt::MoveAction);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        view->setAttribute(Qt::WA_TranslucentBackground);
        QPalette palette = view->palette();
        palette.setColor(QPalette::Base, Qt::transparent);
        view->setPalette(palette);
        connect(view, &QAbstractItemView::activated, this, &FolderView::activate);
        connect(view, &QAbstractItemView::clicked, this, &FolderView::itemClicked);
        connect(view, &QWidget::customContextMenuRequested, this, [this, view](const QPoint &pos) {
            Q_EMIT contextMenuRequested(view->viewport()->mapToGlobal(pos), view->indexAt(pos).isValid());
        });
        m_stack->addWidget(view);
    }

    m_details->setItemDelegate(new DetailsDelegate(m_details));
    m_details->setRootIsDecorated(false);
    m_details->setItemsExpandable(false);
    m_details->setUniformRowHeights(true);
    m_details->setSortingEnabled(true);
    m_details->setAllColumnsShowFocus(true);
    QHeaderView *header = m_details->header();
    header->setStretchLastSection(false);
    header->setSectionsMovable(false);
    // Design order: Name, Date modified, Type, Size.
    header->moveSection(header->visualIndex(DateColumn), 1);
    header->moveSection(header->visualIndex(TypeColumn), 2);
    header->moveSection(header->visualIndex(SizeColumn), 3);
    header->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    header->resizeSection(DateColumn, 190);
    header->resizeSection(TypeColumn, 170);
    header->resizeSection(SizeColumn, 100);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_details->sortByColumn(NameColumn, Qt::AscendingOrder);

    m_icons->setItemDelegate(new IconsDelegate(m_icons));
    m_icons->setViewMode(QListView::IconMode);
    m_icons->setResizeMode(QListView::Adjust);
    m_icons->setMovement(QListView::Static);
    m_icons->setGridSize(QSize(120, 116));
    m_icons->setUniformItemSizes(true);
    m_icons->setSpacing(4);
    m_icons->setWordWrap(true);

    auto relay = [this] { Q_EMIT selectionChanged(); };
    connect(m_details->selectionModel(), &QItemSelectionModel::selectionChanged, this, relay);
    connect(m_icons->selectionModel(), &QItemSelectionModel::selectionChanged, this, relay);
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &dir) {
        m_loadedFolders.insert(dir);
        if (dir != m_path)
            return;
        if (!m_pendingSelect.isEmpty()) {
            const QStringList target = m_pendingSelect;
            m_pendingSelect.clear();
            selectPaths(target, m_pendingEdit);
        }
        Q_EMIT loaded();
    });
    // New folders and files arrive through the model's watcher after creation.
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this] {
        if (m_pendingSelect.isEmpty())
            return;
        QMetaObject::invokeMethod(this, [this] {
            if (m_pendingSelect.isEmpty())
                return;
            const QStringList target = m_pendingSelect;
            const bool edit = m_pendingEdit;
            m_pendingSelect.clear();
            selectPaths(target, edit);
        }, Qt::QueuedConnection);
    });
}

void FolderView::setPath(const QString &path, bool record)
{
    const QString clean = QDir::cleanPath(path);
    if (!QFileInfo(clean).isDir())
        return;
    if (record && !m_path.isEmpty() && clean != m_path) {
        m_back.append(m_path);
        m_forward.clear();
    }
    m_pendingSelect.clear();
    m_pendingEdit = false;
    m_path = clean;
    m_proxy->setRootPath(clean);
    m_proxy->setNameFilter(QString());
    const QModelIndex root = m_proxy->mapFromSource(m_model->setRootPath(clean));
    m_details->setRootIndex(root);
    m_icons->setRootIndex(root);
    m_details->clearSelection();
    m_icons->clearSelection();
    currentView()->setFocus();
    Q_EMIT pathChanged(clean);
    Q_EMIT selectionChanged();
}

void FolderView::back()
{
    if (m_back.isEmpty())
        return;
    m_forward.append(m_path);
    const QString previous = m_back.takeLast();
    const QString child = m_path;
    setPath(previous, false);
    selectPath(child);
}

void FolderView::forward()
{
    if (m_forward.isEmpty())
        return;
    m_back.append(m_path);
    setPath(m_forward.takeLast(), false);
}

void FolderView::up()
{
    const QDir dir(m_path);
    if (dir.isRoot())
        return;
    const QString child = m_path;
    setPath(QFileInfo(m_path).absolutePath());
    selectPath(child);
}

void FolderView::refresh()
{
    Q_EMIT refreshRequested();
    // QFileSystemModel watches folders; a root reset re-reads one that changed unwatched.
    const QString path = m_path;
    m_loadedFolders.remove(path);
    m_model->setRootPath(QString());
    setPath(path, false);
}

void FolderView::setFilter(const QString &text) { m_proxy->setNameFilter(text); }

void FolderView::setShowHidden(bool show)
{
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs;
    if (show)
        filters |= QDir::Hidden;
    m_model->setFilter(filters);
}

bool FolderView::showHidden() const { return m_model->filter() & QDir::Hidden; }

void FolderView::setMode(Mode mode)
{
    const QStringList selected = selectedPaths();
    m_mode = mode;
    m_stack->setCurrentWidget(mode == Mode::Details ? static_cast<QWidget *>(m_details) : m_icons);
    selectPaths(selected);
}

void FolderView::sortBy(int column, Qt::SortOrder order) { m_details->sortByColumn(column, order); }
int FolderView::sortColumn() const { return m_proxy->sortColumn(); }
Qt::SortOrder FolderView::sortOrder() const { return m_proxy->sortOrder(); }

QAbstractItemView *FolderView::currentView() const
{
    return m_mode == Mode::Details ? static_cast<QAbstractItemView *>(m_details) : m_icons;
}

QModelIndex FolderView::sourceIndex(const QModelIndex &proxyIndex) const { return m_proxy->mapToSource(proxyIndex); }

QStringList FolderView::selectedPaths() const
{
    QStringList paths;
    for (const QModelIndex &index : currentView()->selectionModel()->selectedIndexes()) {
        if (index.column() != NameColumn)
            continue;
        paths.append(m_model->filePath(sourceIndex(index)));
    }
    return paths;
}

QList<QFileInfo> FolderView::selectedInfos() const
{
    QList<QFileInfo> infos;
    for (const QString &path : selectedPaths())
        infos.append(QFileInfo(path));
    return infos;
}

int FolderView::itemCount() const
{
    return m_proxy->rowCount(currentView()->rootIndex());
}

void FolderView::selectPath(const QString &path, bool edit)
{
    selectPaths({path}, edit);
}

void FolderView::selectPaths(const QStringList &paths, bool edit)
{
    // index(path) may be valid before directoryLoaded; selecting at that
    // point can be overwritten as QFileSystemModel finishes its first load.
    if (!paths.isEmpty() && !m_loadedFolders.contains(m_path)) {
        m_pendingSelect = paths;
        m_pendingEdit = edit;
        return;
    }
    QList<QModelIndex> indexes;
    for (const QString &path : paths) {
        const QModelIndex source = m_model->index(path);
        const QModelIndex index = m_proxy->mapFromSource(source);
        if (!source.isValid() || !index.isValid() || QFileInfo(path).absolutePath() != m_path) {
            m_pendingSelect = paths;
            m_pendingEdit = edit;
            return;
        }
        indexes.append(index);
    }
    m_pendingSelect.clear();
    m_pendingEdit = false;
    QAbstractItemView *view = currentView();
    view->selectionModel()->clearSelection();
    for (const QModelIndex &index : std::as_const(indexes))
        view->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    if (!indexes.isEmpty()) {
        view->selectionModel()->setCurrentIndex(indexes.first(), QItemSelectionModel::NoUpdate);
        view->scrollTo(indexes.first());
        view->setFocus();
        if (edit)
            view->edit(indexes.first());
    }
}

void FolderView::renameSelected()
{
    QAbstractItemView *view = currentView();
    const QModelIndex current = view->currentIndex();
    if (current.isValid())
        view->edit(current.sibling(current.row(), NameColumn));
}

void FolderView::selectAll() { currentView()->selectAll(); }

void FolderView::activate(const QModelIndex &proxyIndex)
{
    const QModelIndex source = sourceIndex(proxyIndex);
    const QString path = m_model->filePath(source);
    if (m_model->isDir(source))
        setPath(path);
    else
        Q_EMIT fileActivated(path);
}
