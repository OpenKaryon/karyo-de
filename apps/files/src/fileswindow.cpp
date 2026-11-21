// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileswindow.h"
#include "toplevels.h"

#include "archives.h"
#include "detailspane.h"
#include "fileops.h"
#include "filetypes.h"
#include "folderview.h"
#include "places.h"

#include <karyo/ui/desktopentry.h>
#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPainter>
#include <QProcess>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

bool launchProcessExited(qint64 pid)
{
    if (pid <= 0)
        return false;
    QFile status(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!status.open(QIODevice::ReadOnly))
        return !status.exists();
    const QByteArray line = status.readLine();
    const qsizetype state = line.lastIndexOf(')') + 2;
    return state > 1 && state < line.size() && (line.at(state) == 'Z' || line.at(state) == 'X');
}

QSettings filesSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("files"));
}

QString tabTitle(const QString &path)
{
    if (path == QDir::homePath())
        return QObject::tr("Home");
    if (path == Places::trashPath())
        return QObject::tr("Trash");
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

QString tabGlyph(const QString &path)
{
    if (path == QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
        return QStringLiteral("download");
    if (path == QDir::homePath())
        return QStringLiteral("house");
    if (path == Places::trashPath())
        return QStringLiteral("trash-2");
    return QStringLiteral("folder");
}

QLabel *statusLabel()
{
    auto *label = new QLabel;
    label->setFont(Ui::bodyFont(12));
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, Ui::Theme::current().muted);
    label->setPalette(p);
    return label;
}

QString trashInfoPath(const QString &trashedFile)
{
    return QFileInfo(Places::trashPath()).absolutePath() + QStringLiteral("/info/")
           + QFileInfo(trashedFile).fileName() + QStringLiteral(".trashinfo");
}

} // namespace

FilesWindow::FilesWindow(QWidget *parent)
    : KaryoWindow(parent)
    , m_tabs(new Ui::TitleTabs)
    , m_stack(new QStackedWidget)
    , m_breadcrumb(new Breadcrumb)
    , m_search(new Ui::SearchField(tr("Search")))
    , m_places(new Places)
    , m_details(new DetailsPane)
    , m_back(new Ui::IconButton(QStringLiteral("arrow-left"), nullptr, {32, 32}, 16))
    , m_forward(new Ui::IconButton(QStringLiteral("arrow-right"), nullptr, {32, 32}, 16))
    , m_up(new Ui::IconButton(QStringLiteral("arrow-up"), nullptr, {32, 32}, 16))
    , m_paste(new Ui::IconButton(QStringLiteral("clipboard"), nullptr, {32, 32}, 16))
    , m_detailsToggle(new Ui::PillButton(QStringLiteral("panel-right"), tr("Details")))
    , m_trashBar(new QWidget)
    , m_status(statusLabel())
    , m_free(statusLabel())
    , m_progress(statusLabel())
{
    const Ui::Theme &t = Ui::Theme::current();
    setWindowTitle(tr("Files"));
    setBodyColor(t.win);
    setTitleColor(t.pane);
    m_tabs->setColors(t.win, t.text, t.muted);
    titleLayout()->setContentsMargins(14, 0, 8, 0);
    titleLayout()->addWidget(m_tabs, 1);

    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(0);

    // Navigation row: back, forward, up, refresh, breadcrumb, search.
    auto *nav = new QHBoxLayout;
    nav->setContentsMargins(12, 0, 12, 6);
    nav->setSpacing(2);
    auto *refresh = new Ui::IconButton(QStringLiteral("refresh-cw"), nullptr, {32, 32}, 16);
    m_back->setHint(tr("Back"));
    m_forward->setHint(tr("Forward"));
    m_up->setHint(tr("Up"));
    refresh->setHint(tr("Refresh"));
    for (Ui::IconButton *b : {m_back, m_forward, m_up, refresh})
        nav->addWidget(b);
    nav->addSpacing(6);
    nav->addWidget(m_breadcrumb, 1);
    nav->addSpacing(8);
    m_search->setFixedWidth(260);
    nav->addWidget(m_search);
    layout->addLayout(nav);

    // Command bar.
    auto *commands = new QHBoxLayout;
    commands->setContentsMargins(12, 0, 12, 6);
    commands->setSpacing(2);
    auto *newButton = new Ui::PillButton(QStringLiteral("plus"), tr("New"));
    newButton->setKind(Ui::PillButton::Kind::Primary);
    newButton->setChevron(true);
    commands->addWidget(newButton);
    commands->addSpacing(10);
    auto *cut = new Ui::IconButton(QStringLiteral("scissors"), nullptr, {32, 32}, 16);
    auto *copy = new Ui::IconButton(QStringLiteral("copy"), nullptr, {32, 32}, 16);
    auto *rename = new Ui::IconButton(QStringLiteral("pencil"), nullptr, {32, 32}, 16);
    auto *share = new Ui::IconButton(QStringLiteral("share-2"), nullptr, {32, 32}, 16);
    auto *trash = new Ui::IconButton(QStringLiteral("trash-2"), nullptr, {32, 32}, 16);
    cut->setHint(tr("Cut"));
    copy->setHint(tr("Copy"));
    m_paste->setHint(tr("Paste"));
    rename->setHint(tr("Rename"));
    share->setHint(tr("Share"));
    trash->setHint(tr("Move to Trash"));
    for (Ui::IconButton *b : {cut, copy, m_paste, rename, share, trash})
        commands->addWidget(b);
    commands->addSpacing(10);
    auto *sort = new Ui::PillButton(QStringLiteral("arrow-down-up"), tr("Sort"));
    sort->setChevron(true);
    auto *view = new Ui::PillButton(QStringLiteral("layout-list"), tr("View"));
    view->setChevron(true);
    auto *terminal = new Ui::PillButton(QStringLiteral("square-terminal"), tr("Open in Terminal"));
    commands->addWidget(sort);
    commands->addWidget(view);
    commands->addWidget(terminal);
    auto *trashLayout = new QHBoxLayout(m_trashBar);
    trashLayout->setContentsMargins(10, 0, 0, 0);
    auto *restore = new Ui::PillButton(QStringLiteral("rotate-cw"), tr("Restore"));
    auto *empty = new Ui::PillButton(QStringLiteral("trash-2"), tr("Empty Trash"));
    empty->setKind(Ui::PillButton::Kind::Tint);
    trashLayout->addWidget(restore);
    trashLayout->addWidget(empty);
    commands->addWidget(m_trashBar);
    commands->addStretch();
    commands->addWidget(m_detailsToggle);
    layout->addLayout(commands);

    // Places | folder | details.
    auto *main = new QHBoxLayout;
    main->setContentsMargins(8, 0, 10, 0);
    main->setSpacing(10);
    m_places->setFixedWidth(210);
    main->addWidget(m_places);
    main->addWidget(m_stack, 1);
    main->addWidget(m_details);
    layout->addLayout(main, 1);

    auto *status = new QHBoxLayout;
    status->setContentsMargins(16, 4, 16, 6);
    status->addWidget(m_status);
    status->addSpacing(16);
    status->addWidget(m_progress);
    status->addStretch();
    auto *drive = new QLabel;
    drive->setPixmap(Ui::icon(QStringLiteral("hard-drive"), 14, t.muted, devicePixelRatioF()));
    status->addWidget(drive);
    status->addSpacing(6);
    status->addWidget(m_free);
    layout->addLayout(status);
    setCentralWidget(body);

    // Wiring.
    connect(m_tabs, &Ui::TitleTabs::currentChanged, this, [this](int index) {
        if (index >= 0 && index < m_stack->count()) {
            m_stack->setCurrentIndex(index);
            syncChrome();
            setDetailsVisible(true);
        }
    });
    connect(m_tabs, &Ui::TitleTabs::closeRequested, this, &FilesWindow::closeTab);
    connect(m_tabs, &Ui::TitleTabs::newTabRequested, this, [this] { openTab(QDir::homePath()); });
    connect(m_back, &QAbstractButton::clicked, this, [this] { current()->back(); });
    connect(m_forward, &QAbstractButton::clicked, this, [this] { current()->forward(); });
    connect(m_up, &QAbstractButton::clicked, this, [this] { current()->up(); });
    connect(refresh, &QAbstractButton::clicked, this, [this] { current()->refresh(); });
    connect(m_breadcrumb, &Breadcrumb::navigate, this, [this](const QString &path) { current()->setPath(path); });
    connect(m_places, &Places::navigate, this, [this](const QString &path) { current()->setPath(path); });
    connect(m_search->lineEdit(), &QLineEdit::textChanged, this, [this](const QString &text) {
        if (current())
            current()->setFilter(text);
    });
    connect(newButton, &QAbstractButton::clicked, this, &FilesWindow::showNewMenu);
    connect(cut, &QAbstractButton::clicked, this, [this] { copySelection(true); });
    connect(copy, &QAbstractButton::clicked, this, [this] { copySelection(false); });
    connect(m_paste, &QAbstractButton::clicked, this, &FilesWindow::paste);
    connect(rename, &QAbstractButton::clicked, this, [this] { current()->renameSelected(); });
    connect(share, &QAbstractButton::clicked, this, [this] {
        // Share: put the selection's paths on the clipboard, ready to paste anywhere.
        QApplication::clipboard()->setText(current()->selectedPaths().join(QLatin1Char('\n')));
        m_progress->setText(tr("Paths copied to the clipboard"));
    });
    connect(trash, &QAbstractButton::clicked, this, &FilesWindow::trashSelection);
    connect(sort, &QAbstractButton::clicked, this, &FilesWindow::showSortMenu);
    connect(view, &QAbstractButton::clicked, this, &FilesWindow::showViewMenu);
    connect(terminal, &QAbstractButton::clicked, this, [this] { openTerminal(current()->path()); });
    connect(restore, &QAbstractButton::clicked, this, &FilesWindow::restoreFromTrash);
    connect(empty, &QAbstractButton::clicked, this, &FilesWindow::emptyTrash);
    connect(m_detailsToggle, &QAbstractButton::clicked, this, [this] {
        setDetailsVisible(m_details->isHidden());
    });
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        m_openingWindows = new ToplevelManager(this);
        connect(m_openingWindows, &ToplevelManager::toplevelAdded, this,
                [this](Toplevel *window) { windowOpened(window, true); });
        connect(m_openingWindows, &ToplevelManager::toplevelChanged, this,
                [this](Toplevel *window) { windowOpened(window, false); });
    }
    m_openingTimer.setInterval(200);
    connect(&m_openingTimer, &QTimer::timeout, this, [this] {
        for (auto it = m_opening.begin(); it != m_opening.end();) {
            if (launchProcessExited(it->pid) || it->deadline.hasExpired())
                it = m_opening.erase(it);
            else
                ++it;
        }
        updateOpeningCursor();
    });

    connect(m_details, &DetailsPane::openWithRequested, this, [this](const QString &path) {
        showOpenWith(path, QCursor::pos());
    });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &FilesWindow::syncChrome);

    auto shortcut = [this](const QString &keys, auto slot) {
        auto *s = new QShortcut(QKeySequence(keys), this);
        connect(s, &QShortcut::activated, this, slot);
    };
    shortcut(QStringLiteral("Ctrl+C"), [this] { copySelection(false); });
    shortcut(QStringLiteral("Ctrl+X"), [this] { copySelection(true); });
    shortcut(QStringLiteral("Ctrl+V"), [this] { paste(); });
    shortcut(QStringLiteral("Delete"), [this] { inTrash() ? deleteSelection() : trashSelection(); });
    shortcut(QStringLiteral("Shift+Delete"), [this] { deleteSelection(); });
    shortcut(QStringLiteral("F2"), [this] { current()->renameSelected(); });
    shortcut(QStringLiteral("F5"), [this] { current()->refresh(); });
    shortcut(QStringLiteral("Alt+Left"), [this] { current()->back(); });
    shortcut(QStringLiteral("Alt+Right"), [this] { current()->forward(); });
    shortcut(QStringLiteral("Alt+Up"), [this] { current()->up(); });
    shortcut(QStringLiteral("Backspace"), [this] { current()->up(); });
    shortcut(QStringLiteral("Ctrl+L"), [this] { m_breadcrumb->editPath(); });
    shortcut(QStringLiteral("Ctrl+F"), [this] { m_search->setFocus(); });
    shortcut(QStringLiteral("Ctrl+T"), [this] { openTab(current() ? current()->path() : QDir::homePath()); });
    shortcut(QStringLiteral("Ctrl+W"), [this] { closeTab(m_tabs->currentIndex()); });
    shortcut(QStringLiteral("Ctrl+H"), [this] {
        const bool show = !current()->showHidden();
        for (int i = 0; i < m_stack->count(); ++i)
            static_cast<FolderView *>(m_stack->widget(i))->setShowHidden(show);
    });
    shortcut(QStringLiteral("Ctrl+Shift+N"), [this] { newFolder(); });
    shortcut(QStringLiteral("Ctrl+A"), [this] { current()->selectAll(); });
    shortcut(QStringLiteral("Ctrl+N"), [] {
        auto *window = new FilesWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->openTab(QDir::homePath());
        window->show();
    });

    setDetailsVisible(true);
    resizeToFit({1520, 900});
}

FolderView *FilesWindow::current() const { return static_cast<FolderView *>(m_stack->currentWidget()); }

void FilesWindow::openTab(const QString &path, const QString &select)
{
    auto *view = new FolderView;
    const QSettings settings = filesSettings();
    view->setMode(settings.value(QStringLiteral("View/mode")).toString() == QLatin1String("icons")
                      ? FolderView::Mode::Icons : FolderView::Mode::Details);
    view->setShowHidden(settings.value(QStringLiteral("View/hidden"), false).toBool());
    const int index = m_tabs->addTab(tabGlyph(path), tabTitle(path));
    m_stack->addWidget(view);
    connect(view, &FolderView::pathChanged, this, [this, view](const QString &p) {
        const int i = m_stack->indexOf(view);
        m_tabs->setTabTitle(i, tabTitle(p));
        m_tabs->setTabGlyph(i, tabGlyph(p));
        if (view == current()) {
            m_search->lineEdit()->clear();
            syncChrome();
            setDetailsVisible(true);
        }
    });
    connect(view, &FolderView::selectionChanged, this, [this, view] {
        if (view == current()) {
            updateStatus();
            const auto selected = view->selectedInfos();
            m_details->showInfos(selected, view->path());
            if (!selected.isEmpty())
                setDetailsVisible(true);
        }
    });
    connect(view, &FolderView::itemClicked, this, [this, view] {
        if (view == current())
            setDetailsVisible(true);
    });
    connect(view, &FolderView::refreshRequested, m_details, &DetailsPane::clearFolderSizes);
    connect(view, &FolderView::loaded, this, [this, view] {
        if (view == current())
            updateStatus();
    });
    connect(view, &FolderView::fileActivated, this, &FilesWindow::openFile);
    connect(view, &FolderView::contextMenuRequested, this, &FilesWindow::showContextMenu);
    connect(view, &FolderView::filesDropped, this, [this](const QStringList &sources, const QString &target, bool move) {
        runJob(int(move ? FileJob::Kind::Move : FileJob::Kind::Copy), sources, target);
    });
    view->setPath(path);
    if (!select.isEmpty())
        view->selectPath(select);
    m_tabs->setCurrentIndex(index);
    m_stack->setCurrentWidget(view);
    syncChrome();
    setDetailsVisible(true);
}

bool FilesWindow::revealFolder(const QString &path, const QStringList &items)
{
    const QString requested = QFileInfo(path).canonicalFilePath();
    if (requested.isEmpty())
        return false;
    for (int i = 0; i < m_stack->count(); ++i) {
        auto *view = static_cast<FolderView *>(m_stack->widget(i));
        if (QFileInfo(view->path()).canonicalFilePath() != requested)
            continue;
        m_tabs->setCurrentIndex(i);
        m_stack->setCurrentIndex(i);
        if (!items.isEmpty()) {
            m_search->lineEdit()->clear();
            view->setFilter(QString());
            QStringList selected;
            for (const QString &item : items) {
                const QString name = QFileInfo(item).fileName();
                if (name.startsWith(QLatin1Char('.')))
                    view->setShowHidden(true);
                const QString local = QDir(view->path()).filePath(name);
                if (QFileInfo::exists(local) || QFileInfo(local).isSymLink())
                    selected.append(local);
            }
            view->selectPaths(selected);
            setDetailsVisible(true);
        }
        syncChrome();
        return true;
    }
    return false;
}

void FilesWindow::closeTab(int index)
{
    QWidget *widget = m_stack->widget(index);
    if (!widget)
        return;
    if (m_stack->count() == 1) {
        close();
        return;
    }
    m_stack->removeWidget(widget);
    widget->deleteLater();
    m_tabs->removeTab(index);
    m_stack->setCurrentIndex(m_tabs->currentIndex());
    syncChrome();
}

bool FilesWindow::inTrash() const { return current() && current()->path().startsWith(Places::trashPath()); }

void FilesWindow::syncChrome()
{
    FolderView *view = current();
    if (!view)
        return;
    const QString path = view->path();
    setWindowTitle(tabTitle(path));
    m_breadcrumb->setPath(path);
    m_places->setCurrentPath(path);
    m_back->setEnabled(view->canGoBack());
    m_forward->setEnabled(view->canGoForward());
    m_up->setEnabled(!QDir(path).isRoot());
    m_paste->setEnabled(FileClipboard::hasFiles());
    m_search->lineEdit()->setPlaceholderText(tr("Search %1").arg(tabTitle(path)));
    m_trashBar->setVisible(inTrash());
    const QStorageInfo storage(path);
    m_free->setText(storage.isValid() ? tr("%1 free of %2").arg(formatSize(storage.bytesAvailable()),
                                                                  formatSize(storage.bytesTotal()))
                                      : QString());
    m_details->showInfos(view->selectedInfos(), path);
    updateStatus();
}

void FilesWindow::setDetailsVisible(bool visible)
{
    m_details->setVisible(visible);
    m_detailsToggle->setActive(visible);
}

void FilesWindow::updateStatus()
{
    FolderView *view = current();
    if (!view)
        return;
    const QList<QFileInfo> selected = view->selectedInfos();
    QString text = tr("%n items", nullptr, view->itemCount());
    if (!selected.isEmpty()) {
        qint64 size = 0;
        for (const QFileInfo &info : selected)
            size += info.isDir() ? 0 : info.size();
        text += QStringLiteral(" · ") + tr("%n selected", nullptr, int(selected.size()));
        if (size > 0)
            text += QStringLiteral(" · ") + formatSize(size);
    }
    m_status->setText(text);
}

bool FilesWindow::launchFile(const DesktopEntry &app, const QString &path)
{
    const QFileInfo file(path);
    const QString canonical = file.canonicalFilePath();
    const auto key = qMakePair(canonical.isEmpty() ? file.absoluteFilePath() : canonical, app.id);
    if (m_opening.contains(key))
        return true;

    qint64 pid = 0;
    if (!app.launchWith({path}, &pid))
        return false;

    quint64 lastActivation = 0;
    if (m_openingWindows) {
        for (Toplevel *window : m_openingWindows->toplevels())
            lastActivation = qMax(lastActivation, window->activationSerial());
    }
    const bool windowsApp = app.id == QLatin1String("openkaryon-win32.desktop");
    const QFileInfo launchedFile(key.first);
    const QString windowsClass = windowsApp
        ? (launchedFile.suffix().compare(QLatin1String("msi"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("msiexec.exe") : launchedFile.fileName()) : QString();
    m_opening.insert(key, PendingOpen{app, windowsClass, pid, lastActivation,
                                     QDeadlineTimer(windowsApp ? 90000 : 12000)});
    updateOpeningCursor();
    return true;
}

void FilesWindow::windowOpened(Toplevel *window, bool added)
{
    if (window->isMinimized() || window->appId().isEmpty() || m_opening.isEmpty())
        return;
    const QString id = window->appId();
    QString windowClass = id;
    if (id.startsWith(QLatin1String("openkaryon-win32-")) && !id.contains(QLatin1Char('/'))) {
        if (const auto entry = DesktopEntry::find(id + QStringLiteral(".desktop")))
            windowClass = entry->startupWmClass;
    }
    for (auto it = m_opening.begin(); it != m_opening.end();) {
        const bool appeared = added || (window->isActive() && window->activationSerial() > it->lastActivation);
        const bool matches = it->windowsClass.isEmpty() ? it->app.matchesAppId(id)
            : it->windowsClass.compare(windowClass, Qt::CaseInsensitive) == 0;
        if (appeared && matches)
            it = m_opening.erase(it);
        else
            ++it;
    }
    updateOpeningCursor();
}

void FilesWindow::updateOpeningCursor()
{
    if (m_opening.isEmpty()) {
        m_openingTimer.stop();
        m_stack->unsetCursor();
    } else if (!m_openingTimer.isActive()) {
        m_stack->setCursor(Qt::BusyCursor);
        m_openingTimer.start();
    }
}

void FilesWindow::openFile(const QString &path)
{
    static QMimeDatabase db;
    const QString mime = db.mimeTypeForFile(path).name();
    if (Archives::kindOf(path) != Archives::Kind::None) {
        const auto app = DesktopEntry::defaultFor(mime);
        // Respect a real archive handler, but don't hand an archive to a
        // generic URL/browser fallback when no archiver is installed.
        if (app && app->mimeTypes.contains(mime) && app->id != QLatin1String("lxqt-archiver.desktop")
            && launchFile(*app, path))
            return;
        if (const auto manager = DesktopEntry::find(QStringLiteral("karyo-archive.desktop"));
            manager && launchFile(*manager, path))
            return;
        if (app && app->mimeTypes.contains(mime) && launchFile(*app, path))
            return;
        QMessageBox::warning(this, tr("Open archive"), tr("The archive manager is unavailable. Install karyo-archive and try again."));
        return;
    }
    if (const auto app = DesktopEntry::defaultFor(mime); app && launchFile(*app, path))
        return;
    const auto key = qMakePair(QFileInfo(path).absoluteFilePath(), QString());
    if (!m_opening.contains(key) && QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        m_opening.insert(key, PendingOpen{DesktopEntry{}, QString(), 0, 0, QDeadlineTimer(1500)});
        updateOpeningCursor();
    }
}

void FilesWindow::showOpenWith(const QString &path, const QPoint &globalPos)
{
    static QMimeDatabase db;
    const QString mime = db.mimeTypeForFile(path).name();
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    const QList<DesktopEntry> apps = DesktopEntry::forMimeType(mime);
    for (const DesktopEntry &app : apps) {
        QAction *action = menu->addAction(QIcon::fromTheme(app.icon), app.name);
        connect(action, &QAction::triggered, this, [this, app, path] { launchFile(app, path); });
    }
    if (apps.isEmpty())
        menu->addAction(tr("No application for this type"))->setEnabled(false);
    menu->popup(globalPos);
}

void FilesWindow::showContextMenu(const QPoint &globalPos, bool onItem)
{
    FolderView *view = current();
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    const QList<QFileInfo> selected = view->selectedInfos();
    if (onItem && !selected.isEmpty()) {
        const QFileInfo first = selected.first();
        menu->addAction(tr("Open"), this, [this, first] {
            first.isDir() ? current()->setPath(first.absoluteFilePath()) : openFile(first.absoluteFilePath());
        });
        if (!first.isDir()) {
            menu->addAction(tr("Open with…"), this, [this, first, globalPos] {
                showOpenWith(first.absoluteFilePath(), globalPos);
            });
        } else {
            menu->addAction(tr("Open in new tab"), this, [this, first] { openTab(first.absoluteFilePath()); });
        }
        if (!inTrash() && Archives::available())
            addArchiveActions(menu, selected);
        menu->addSeparator();
        if (inTrash()) {
            menu->addAction(tr("Restore"), this, &FilesWindow::restoreFromTrash);
            menu->addAction(tr("Delete permanently"), this, &FilesWindow::deleteSelection);
        } else {
            menu->addAction(tr("Cut"), this, [this] { copySelection(true); })->setShortcut(QKeySequence::Cut);
            menu->addAction(tr("Copy"), this, [this] { copySelection(false); })->setShortcut(QKeySequence::Copy);
            menu->addAction(tr("Rename"), view, &FolderView::renameSelected)->setShortcut(QKeySequence(Qt::Key_F2));
            menu->addAction(tr("Move to Trash"), this, &FilesWindow::trashSelection)->setShortcut(QKeySequence::Delete);
        }
        menu->addSeparator();
        menu->addAction(tr("Copy path"), this, [selected] {
            QStringList paths;
            for (const QFileInfo &info : selected)
                paths.append(info.absoluteFilePath());
            QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
        });
        if (first.isDir()) {
            menu->addAction(tr("Open in Terminal"), this, [this, first] { openTerminal(first.absoluteFilePath()); });
            if (!m_places->pinned().contains(first.absoluteFilePath()))
                menu->addAction(tr("Pin to Quick access"), this, [this, first] { m_places->pin(first.absoluteFilePath()); });
        }
        menu->addAction(tr("Properties"), this, [this] {
            setDetailsVisible(true);
        });
    } else {
        menu->addAction(tr("New folder"), this, &FilesWindow::newFolder);
        menu->addAction(tr("New text file"), this, &FilesWindow::newTextFile);
        QAction *paste = menu->addAction(tr("Paste"), this, &FilesWindow::paste);
        paste->setEnabled(FileClipboard::hasFiles());
        menu->addSeparator();
        menu->addAction(tr("Open in Terminal"), this, [this] { openTerminal(current()->path()); });
        menu->addAction(tr("Select all"), view, &FolderView::selectAll);
        menu->addAction(tr("Refresh"), view, &FolderView::refresh);
    }
    menu->popup(globalPos);
}

void FilesWindow::showNewMenu()
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    menu->addAction(QIcon(Ui::icon(QStringLiteral("folder"), 16, Ui::Theme::current().text, devicePixelRatioF())),
                    tr("Folder"), this, &FilesWindow::newFolder);
    menu->addAction(QIcon(Ui::icon(QStringLiteral("file-text"), 16, Ui::Theme::current().text, devicePixelRatioF())),
                    tr("Text file"), this, &FilesWindow::newTextFile);
    menu->popup(QCursor::pos());
}

void FilesWindow::showSortMenu()
{
    FolderView *view = current();
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    const QList<QPair<QString, int>> columns = {{tr("Name"), 0}, {tr("Date modified"), 3}, {tr("Type"), 2}, {tr("Size"), 1}};
    for (const auto &[label, column] : columns) {
        QAction *action = menu->addAction(label, this, [view, column = column] { view->sortBy(column, view->sortOrder()); });
        action->setCheckable(true);
        action->setChecked(view->sortColumn() == column);
    }
    menu->addSeparator();
    QAction *ascending = menu->addAction(tr("Ascending"), this, [view] { view->sortBy(view->sortColumn(), Qt::AscendingOrder); });
    ascending->setCheckable(true);
    ascending->setChecked(view->sortOrder() == Qt::AscendingOrder);
    QAction *descending = menu->addAction(tr("Descending"), this, [view] { view->sortBy(view->sortColumn(), Qt::DescendingOrder); });
    descending->setCheckable(true);
    descending->setChecked(view->sortOrder() == Qt::DescendingOrder);
    menu->popup(QCursor::pos());
}

void FilesWindow::showViewMenu()
{
    FolderView *view = current();
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    auto setMode = [this](FolderView::Mode mode) {
        for (int i = 0; i < m_stack->count(); ++i)
            static_cast<FolderView *>(m_stack->widget(i))->setMode(mode);
        QSettings settings = filesSettings();
        settings.setValue(QStringLiteral("View/mode"), mode == FolderView::Mode::Icons ? QStringLiteral("icons")
                                                                                      : QStringLiteral("details"));
    };
    QAction *details = menu->addAction(tr("Details"), this, [setMode] { setMode(FolderView::Mode::Details); });
    details->setCheckable(true);
    details->setChecked(view->mode() == FolderView::Mode::Details);
    QAction *icons = menu->addAction(tr("Icons"), this, [setMode] { setMode(FolderView::Mode::Icons); });
    icons->setCheckable(true);
    icons->setChecked(view->mode() == FolderView::Mode::Icons);
    menu->addSeparator();
    QAction *hidden = menu->addAction(tr("Hidden files"), this, [this] {
        const bool show = !current()->showHidden();
        for (int i = 0; i < m_stack->count(); ++i)
            static_cast<FolderView *>(m_stack->widget(i))->setShowHidden(show);
        QSettings settings = filesSettings();
        settings.setValue(QStringLiteral("View/hidden"), show);
    });
    hidden->setCheckable(true);
    hidden->setChecked(view->showHidden());
    hidden->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    menu->popup(QCursor::pos());
}

void FilesWindow::newFolder()
{
    const QString dir = current()->path();
    const QString name = FileJob::freeName(dir, tr("New folder"));
    if (!name.isEmpty() && QDir(dir).mkdir(name))
        current()->selectPath(QDir(dir).filePath(name), true);
}

void FilesWindow::newTextFile()
{
    const QString dir = current()->path();
    const QString name = FileJob::freeName(dir, tr("New text file.txt"));
    if (name.isEmpty())
        return;
    QFile file(QDir(dir).filePath(name));
    if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        file.close();
        current()->selectPath(file.fileName(), true);
    }
}

void FilesWindow::copySelection(bool cut)
{
    const QStringList paths = current()->selectedPaths();
    if (!paths.isEmpty())
        FileClipboard::set(paths, cut);
}

void FilesWindow::paste()
{
    bool cut = false;
    const QStringList sources = FileClipboard::paths(&cut);
    if (sources.isEmpty())
        return;
    QString target = current()->path();
    const QList<QFileInfo> selected = current()->selectedInfos();
    if (selected.size() == 1 && selected.first().isDir() && !sources.contains(selected.first().absoluteFilePath()))
        target = selected.first().absoluteFilePath();
    runJob(int(cut ? FileJob::Kind::Move : FileJob::Kind::Copy), sources, target);
    if (cut)
        QApplication::clipboard()->clear();
}

void FilesWindow::runJob(int kind, const QStringList &sources, const QString &target)
{
    FileJob *job = FileJob::start(FileJob::Kind(kind), sources, target);
    const QString verb = kind == int(FileJob::Kind::Move) ? tr("Moving") : kind == int(FileJob::Kind::Delete) ? tr("Deleting") : tr("Copying");
    connect(job, &FileJob::progress, this, [this, verb, kind](qint64 done, qint64 total, const QString &name) {
        if (kind == int(FileJob::Kind::Delete) || total <= 0)
            m_progress->setText(QStringLiteral("%1 %2…").arg(verb, name));
        else
            m_progress->setText(QStringLiteral("%1 %2 · %3%").arg(verb, name).arg(int(100 * done / total)));
    });
    connect(job, &FileJob::finished, this, [this](const QStringList &errors) {
        m_progress->setText(errors.isEmpty() ? QString() : errors.first());
        if (errors.size() > 1)
            QMessageBox::warning(this, tr("Files"), errors.join(QLatin1Char('\n')));
    });
}

void FilesWindow::addArchiveActions(QMenu *menu, const QList<QFileInfo> &selected)
{
    const QFileInfo first = selected.first();
    const QString dir = current()->path();
    if (selected.size() == 1 && first.isFile()) {
        const Archives::Kind kind = Archives::kindOf(first.absoluteFilePath());
        if (kind == Archives::Kind::Archive) {
            menu->addAction(tr("Extract all"), this, [this, first, dir] {
                runArchive(ArchiveJob::extract(first.absoluteFilePath(), dir), tr("Extracting"));
            });
            menu->addAction(tr("Extract to…"), this, [this, first, dir] {
                const QString target = QFileDialog::getExistingDirectory(this, tr("Extract to"), dir);
                if (!target.isEmpty())
                    runArchive(ArchiveJob::extract(first.absoluteFilePath(), target), tr("Extracting"));
            });
        } else if (kind == Archives::Kind::Compressed) {
            menu->addAction(tr("Decompress"), this, [this, first] {
                runArchive(ArchiveJob::decompress(first.absoluteFilePath()), tr("Decompressing"));
            });
        }
    }
    QStringList paths;
    for (const QFileInfo &info : selected)
        paths.append(info.absoluteFilePath());
    auto *customArchive = menu->addAction(tr("Add to archive…"), this, [this, paths] {
        const QString program = QStandardPaths::findExecutable(QStringLiteral("karyo-archive"));
        QStringList arguments{QStringLiteral("--create"), QStringLiteral("--")}; arguments.append(paths);
        if (program.isEmpty() || !QProcess::startDetached(program, arguments))
            QMessageBox::warning(this, tr("Open archive"), tr("The archive manager is unavailable. Install karyo-archive and try again."));
    });
    customArchive->setObjectName(QStringLiteral("addToArchive"));
    QMenu *compress = menu->addMenu(tr("Compress to"));
    Ui::styleMenu(compress);
    compress->addAction(tr("ZIP file"), this, [this, paths] {
        runArchive(ArchiveJob::compress(paths, ArchiveJob::Format::Zip), tr("Compressing"));
    });
    compress->addAction(tr("TAR.GZ archive"), this, [this, paths] {
        runArchive(ArchiveJob::compress(paths, ArchiveJob::Format::TarGz), tr("Compressing"));
    });
    // One file on its own, as sequencing data is kept: reads.fastq.gz.
    if (selected.size() == 1 && first.isFile() && Archives::kindOf(first.absoluteFilePath()) == Archives::Kind::None) {
        compress->addAction(tr("GZIP file (.gz)"), this, [this, paths] {
            runArchive(ArchiveJob::compress(paths, ArchiveJob::Format::Gzip), tr("Compressing"));
        });
    }
}

void FilesWindow::runArchive(ArchiveJob *job, const QString &verb)
{
    m_progress->setText(verb + QStringLiteral("…"));
    connect(job, &ArchiveJob::progress, this, [this, verb](const QString &name) {
        m_progress->setText(QStringLiteral("%1 %2…").arg(verb, QFileInfo(name).fileName()));
    });
    connect(job, &ArchiveJob::finished, this, [this](const QString &result, const QString &error) {
        m_progress->setText(error);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, tr("Files"), error);
            return;
        }
        // Shows what came out when it is in the folder on screen.
        if (QFileInfo(result).absolutePath() == QFileInfo(current()->path()).absoluteFilePath())
            QTimer::singleShot(300, this, [this, result] { current()->selectPath(result); });
    });
}

void FilesWindow::trashSelection()
{
    if (inTrash()) {
        deleteSelection();
        return;
    }
    QStringList failed;
    for (const QString &path : current()->selectedPaths()) {
        QFile file(path);
        if (!file.moveToTrash())
            failed.append(QFileInfo(path).fileName());
    }
    if (!failed.isEmpty())
        m_progress->setText(tr("Could not move to Trash: %1").arg(failed.join(QStringLiteral(", "))));
}

void FilesWindow::deleteSelection()
{
    const QStringList paths = current()->selectedPaths();
    if (paths.isEmpty())
        return;
    const auto answer = QMessageBox::question(
        this, tr("Delete permanently"),
        tr("Permanently delete %n items? This cannot be undone.", nullptr, int(paths.size())));
    if (answer != QMessageBox::Yes)
        return;
    if (inTrash()) {
        for (const QString &path : paths)
            QFile::remove(trashInfoPath(path));
    }
    runJob(int(FileJob::Kind::Delete), paths, {});
}

void FilesWindow::restoreFromTrash()
{
    for (const QString &path : current()->selectedPaths()) {
        QFile info(trashInfoPath(path));
        QString original;
        if (info.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&info);
            while (!in.atEnd()) {
                const QString line = in.readLine();
                if (line.startsWith(QLatin1String("Path=")))
                    original = QUrl::fromPercentEncoding(line.mid(5).toUtf8());
            }
            info.close();
        }
        if (original.isEmpty())
            continue;
        const QString dir = QFileInfo(original).absolutePath();
        QDir().mkpath(dir);
        const QString target = QDir(dir).filePath(FileJob::freeName(dir, QFileInfo(original).fileName()));
        if (QFile::rename(path, target))
            info.remove();
    }
}

void FilesWindow::emptyTrash()
{
    const auto answer = QMessageBox::question(this, tr("Empty Trash"),
                                              tr("Permanently delete everything in the Trash?"));
    if (answer != QMessageBox::Yes)
        return;
    const QString files = Places::trashPath();
    const QString info = QFileInfo(files).absolutePath() + QStringLiteral("/info");
    QStringList all;
    for (const QString &dir : {files, info}) {
        for (const QString &name : QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
            all.append(QDir(dir).filePath(name));
    }
    runJob(int(FileJob::Kind::Delete), all, {});
}

void FilesWindow::openTerminal(const QString &directory)
{
    QString program = QStandardPaths::findExecutable(QStringLiteral("karyo-terminal"));
    if (program.isEmpty())
        program = QStringLiteral("x-terminal-emulator");
    QProcess::startDetached(program, {}, directory);
}
