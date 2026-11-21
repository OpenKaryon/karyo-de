// SPDX-License-Identifier: GPL-3.0-or-later
#include "readerwindow.h"

#include "documentpage.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/theme.h>

#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

constexpr int MaxRecent = 8;

QSettings readerSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("reader"));
}

QLabel *mutedLabel(const QString &text, qreal size, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(Ui::bodyFont(size));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, Ui::Theme::current().muted);
    label->setPalette(palette);
    return label;
}

} // namespace

ReaderWindow::ReaderWindow(QWidget *parent)
    : KaryoWindow(parent)
    , m_tabs(new Ui::TitleTabs)
    , m_stack(new QStackedWidget)
{
    const Ui::Theme &t = Ui::Theme::current();
    setWindowTitle(tr("PDF Reader"));
    setBodyColor(t.win);
    setTitleColor(t.pane);
    setTitleHeight(40);
    setAcceptDrops(true);
    m_tabs->setColors(t.win, t.text, t.muted, t.hover);
    m_tabs->setGlyphColor(t.ink2);
    m_tabs->setMaxTabWidth(220);
    titleLayout()->setContentsMargins(14, 0, 10, 0);
    titleLayout()->addWidget(m_tabs, 1);
    connect(m_tabs, &Ui::TitleTabs::currentChanged, this, [this](int index) { showPage(index); });
    connect(m_tabs, &Ui::TitleTabs::closeRequested, this, &ReaderWindow::closeTab);
    connect(m_tabs, &Ui::TitleTabs::newTabRequested, this, &ReaderWindow::openDialog);

    auto *body = new QWidget;
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Toolbar: sidebar, pages, zoom; search on the right.
    m_toolbar = new QWidget(body);
    auto *bar = new QHBoxLayout(m_toolbar);
    bar->setContentsMargins(14, 8, 14, 8);
    bar->setSpacing(4);
    m_sidebar = new Ui::PillButton(QStringLiteral("panel-left"), QString(), m_toolbar);
    m_sidebar->setChevron(true);
    m_sidebar->setToolTip(tr("Sidebar (F9)"));
    connect(m_sidebar, &QAbstractButton::clicked, this, &ReaderWindow::showSidebarMenu);
    bar->addWidget(m_sidebar);
    bar->addSpacing(10);

    auto *previous = new Ui::IconButton(QStringLiteral("chevron-up"), m_toolbar);
    previous->setHint(tr("Previous page"));
    connect(previous, &QAbstractButton::clicked, this, [this] {
        if (DocumentPage *page = current())
            page->goToPage(page->currentPage() - 1);
    });
    auto *next = new Ui::IconButton(QStringLiteral("chevron-down"), m_toolbar);
    next->setHint(tr("Next page"));
    connect(next, &QAbstractButton::clicked, this, [this] {
        if (DocumentPage *page = current())
            page->goToPage(page->currentPage() + 1);
    });
    m_pageEdit = new QLineEdit(m_toolbar);
    m_pageEdit->setFixedWidth(52);
    m_pageEdit->setAlignment(Qt::AlignCenter);
    m_pageEdit->setFont(Ui::bodyFont(13));
    m_pageEdit->setValidator(new QIntValidator(1, 99999, m_pageEdit));
    connect(m_pageEdit, &QLineEdit::returnPressed, this, [this] {
        if (DocumentPage *page = current()) {
            page->goToPage(m_pageEdit->text().toInt() - 1);
            page->setFocus();
        }
    });
    m_pageCount = mutedLabel(QString(), 13, m_toolbar);
    bar->addWidget(previous);
    bar->addWidget(next);
    bar->addSpacing(4);
    bar->addWidget(m_pageEdit);
    bar->addSpacing(4);
    bar->addWidget(m_pageCount);
    bar->addSpacing(16);

    auto *zoomOut = new Ui::IconButton(QStringLiteral("zoom-out"), m_toolbar);
    zoomOut->setHint(tr("Zoom out (Ctrl+−)"));
    connect(zoomOut, &QAbstractButton::clicked, this, [this] {
        if (DocumentPage *page = current())
            page->zoomBy(1 / 1.25);
    });
    m_zoom = new Ui::PillButton(QString(), QString(), m_toolbar);
    m_zoom->setChevron(true);
    connect(m_zoom, &QAbstractButton::clicked, this, &ReaderWindow::showZoomMenu);
    auto *zoomIn = new Ui::IconButton(QStringLiteral("zoom-in"), m_toolbar);
    zoomIn->setHint(tr("Zoom in (Ctrl++)"));
    connect(zoomIn, &QAbstractButton::clicked, this, [this] {
        if (DocumentPage *page = current())
            page->zoomBy(1.25);
    });
    bar->addWidget(zoomOut);
    bar->addWidget(m_zoom);
    bar->addWidget(zoomIn);
    bar->addStretch(1);

    m_find = new Ui::SearchField(tr("Find in document"), m_toolbar);
    m_find->setFixedWidth(260);
    m_matches = mutedLabel(QString(), 12.5, m_toolbar);
    auto *previousMatch = new Ui::IconButton(QStringLiteral("chevron-left"), m_toolbar);
    previousMatch->setHint(tr("Previous match (Shift+F3)"));
    connect(previousMatch, &QAbstractButton::clicked, this, [this] { findNext(true); });
    auto *nextMatch = new Ui::IconButton(QStringLiteral("chevron-right"), m_toolbar);
    nextMatch->setHint(tr("Next match (F3)"));
    connect(nextMatch, &QAbstractButton::clicked, this, [this] { findNext(false); });
    connect(m_find->lineEdit(), &QLineEdit::textChanged, this, [this](const QString &text) {
        if (DocumentPage *page = current())
            page->search(text);
    });
    connect(m_find->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        findNext(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
    });
    bar->addWidget(m_matches);
    bar->addSpacing(6);
    bar->addWidget(m_find);
    bar->addWidget(previousMatch);
    bar->addWidget(nextMatch);
    v->addWidget(m_toolbar);

    // Without documents: open one, or one of the recent ones.
    auto *empty = new QWidget;
    auto *ev = new QVBoxLayout(empty);
    ev->setContentsMargins(24, 24, 24, 24);
    ev->setSpacing(10);
    ev->addStretch(2);
    auto *icon = new QLabel(empty);
    icon->setPixmap(Ui::icon(QStringLiteral("file-text"), 44, t.ink2, devicePixelRatioF()));
    icon->setAlignment(Qt::AlignCenter);
    ev->addWidget(icon);
    auto *heading = new QLabel(tr("Open a PDF document"), empty);
    heading->setFont(Ui::bodyFont(20, QFont::DemiBold));
    heading->setAlignment(Qt::AlignCenter);
    ev->addWidget(heading);
    auto *hint = mutedLabel(tr("Choose a file or drop one here."), 13.5, empty);
    hint->setAlignment(Qt::AlignCenter);
    ev->addWidget(hint);
    ev->addSpacing(6);
    auto *open = new Ui::PillButton(QStringLiteral("folder-open"), tr("Open file…"), empty);
    open->setKind(Ui::PillButton::Kind::Primary);
    connect(open, &QAbstractButton::clicked, this, &ReaderWindow::openDialog);
    ev->addWidget(open, 0, Qt::AlignHCenter);
    m_recentBox = new QWidget(empty);
    auto *rv = new QVBoxLayout(m_recentBox);
    rv->setContentsMargins(0, 22, 0, 0);
    rv->setSpacing(2);
    auto *recentHeading = mutedLabel(tr("Recent"), 12.5, m_recentBox);
    recentHeading->setAlignment(Qt::AlignCenter);
    rv->addWidget(recentHeading);
    m_recent = new QVBoxLayout;
    m_recent->setSpacing(2);
    rv->addLayout(m_recent);
    ev->addWidget(m_recentBox, 0, Qt::AlignHCenter);
    ev->addStretch(3);
    m_stack->addWidget(empty);

    auto *stackRow = new QHBoxLayout;
    stackRow->setContentsMargins(14, 0, 14, 14);
    stackRow->addWidget(m_stack);
    v->addLayout(stackRow, 1);
    setCentralWidget(body);

    auto shortcut = [this](const QKeySequence &keys, auto slot) {
        connect(new QShortcut(keys, this), &QShortcut::activated, this, slot);
    };
    shortcut(QKeySequence::Open, [this] { openDialog(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_W), [this] {
        if (m_tabs->currentIndex() >= 0)
            closeTab(m_tabs->currentIndex());
    });
    shortcut(QKeySequence::Find, [this] {
        if (current()) {
            m_find->lineEdit()->setFocus();
            m_find->lineEdit()->selectAll();
        }
    });
    shortcut(QKeySequence(Qt::Key_F3), [this] { findNext(false); });
    shortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), [this] { findNext(true); });
    shortcut(QKeySequence(Qt::Key_F9), [this] {
        if (DocumentPage *page = current()) {
            page->setSidebar(page->sidebar() == 0 ? 1 : 0);
            updateToolbar();
        }
    });
    shortcut(QKeySequence::ZoomIn, [this] {
        if (DocumentPage *page = current())
            page->zoomBy(1.25);
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), [this] {
        if (DocumentPage *page = current())
            page->zoomBy(1.25);
    });
    shortcut(QKeySequence::ZoomOut, [this] {
        if (DocumentPage *page = current())
            page->zoomBy(1 / 1.25);
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_0), [this] {
        if (DocumentPage *page = current())
            page->setFit(DocumentPage::Fit::Width);
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), [this] {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), [this] {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });

    updateRecent();
    updateToolbar();
    resizeToFit({1180, 860});
}

DocumentPage *ReaderWindow::current() const
{
    const int index = m_tabs->currentIndex();
    return index >= 0 && index < m_pages.size() ? m_pages.at(index) : nullptr;
}

void ReaderWindow::openFiles(const QStringList &paths)
{
    for (const QString &path : paths) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        bool shown = false;
        for (int i = 0; i < m_pages.size(); ++i) {
            if (m_pages.at(i)->path() == absolute) {
                m_tabs->setCurrentIndex(i);
                shown = true;
                break;
            }
        }
        if (shown)
            continue;
        auto *page = new DocumentPage;
        if (!page->open(absolute)) {
            QMessageBox::warning(this, tr("PDF Reader"),
                                 tr("Cannot open “%1”.").arg(QFileInfo(absolute).fileName()) + QLatin1Char('\n')
                                     + page->error());
            delete page;
            continue;
        }
        connect(page, &DocumentPage::pageChanged, this, [this, page] {
            if (page == current())
                updatePage();
        });
        connect(page, &DocumentPage::zoomChanged, this, [this, page] {
            if (page == current())
                updateZoom();
        });
        connect(page, &DocumentPage::matchesChanged, this, [this, page] {
            if (page == current())
                updateMatches();
        });
        m_pages.append(page);
        m_stack->addWidget(page);
        const int index = m_tabs->addTab(QStringLiteral("file-text"), page->title());
        remember(absolute);
        m_tabs->setCurrentIndex(index);
    }
}

void ReaderWindow::openDialog()
{
    QString dir = current() ? QFileInfo(current()->path()).absolutePath() : QString();
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    openFiles(QFileDialog::getOpenFileNames(this, tr("Open"), dir, tr("PDF documents (*.pdf)")));
}

void ReaderWindow::closeTab(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    DocumentPage *page = m_pages.takeAt(index);
    m_stack->removeWidget(page);
    m_tabs->removeTab(index);
    page->deleteLater();
    showPage(m_tabs->currentIndex());
}

void ReaderWindow::showPage(int index)
{
    DocumentPage *page = index >= 0 && index < m_pages.size() ? m_pages.at(index) : nullptr;
    m_stack->setCurrentWidget(page ? static_cast<QWidget *>(page) : m_stack->widget(0));
    if (page) {
        // The search field follows the tab.
        const QSignalBlocker blocker(m_find->lineEdit());
        m_find->lineEdit()->clear();
        page->search(QString());
        setWindowTitle(page->title() + QStringLiteral(" — ") + tr("PDF Reader"));
    } else {
        setWindowTitle(tr("PDF Reader"));
        updateRecent();
    }
    updateToolbar();
}

void ReaderWindow::showSidebarMenu()
{
    DocumentPage *page = current();
    if (!page)
        return;
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    const QStringList modes{tr("No sidebar"), tr("Pages"), tr("Contents")};
    for (int mode = 0; mode < modes.size(); ++mode) {
        QAction *action = menu->addAction(modes.at(mode), this, [this, page, mode] {
            page->setSidebar(mode);
            updateToolbar();
        });
        action->setCheckable(true);
        action->setChecked(page->sidebar() == mode);
        if (mode == 2 && !page->hasContents()) {
            action->setEnabled(false);
            action->setText(tr("Contents (none in this document)"));
        }
    }
    menu->popup(m_sidebar->mapToGlobal(QPoint(0, m_sidebar->height() + 4)));
}

void ReaderWindow::showZoomMenu()
{
    DocumentPage *page = current();
    if (!page)
        return;
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    QAction *width = menu->addAction(tr("Fit width"), this, [page] { page->setFit(DocumentPage::Fit::Width); });
    width->setCheckable(true);
    width->setChecked(page->fit() == DocumentPage::Fit::Width);
    QAction *whole = menu->addAction(tr("Whole page"), this, [page] { page->setFit(DocumentPage::Fit::Page); });
    whole->setCheckable(true);
    whole->setChecked(page->fit() == DocumentPage::Fit::Page);
    menu->addSeparator();
    for (int percent : {50, 75, 100, 125, 150, 200, 300}) {
        QAction *action = menu->addAction(QStringLiteral("%1 %").arg(percent), this,
                                          [page, percent] { page->zoomTo(percent / 100.0); });
        action->setCheckable(true);
        action->setChecked(page->fit() == DocumentPage::Fit::Custom && qRound(page->zoom() * 100) == percent);
    }
    menu->popup(m_zoom->mapToGlobal(QPoint(0, m_zoom->height() + 4)));
}

void ReaderWindow::findNext(bool backward)
{
    if (DocumentPage *page = current())
        page->nextMatch(backward);
}

void ReaderWindow::updateToolbar()
{
    DocumentPage *page = current();
    m_toolbar->setVisible(page);
    m_sidebar->setActive(page && page->sidebar() != 0);
    updatePage();
    updateZoom();
    updateMatches();
}

void ReaderWindow::updatePage()
{
    DocumentPage *page = current();
    if (!page) {
        m_pageEdit->clear();
        m_pageCount->clear();
        return;
    }
    if (!m_pageEdit->hasFocus())
        m_pageEdit->setText(QString::number(page->currentPage() + 1));
    m_pageCount->setText(tr("of %1").arg(page->pageCount()));
}

void ReaderWindow::updateZoom()
{
    DocumentPage *page = current();
    m_zoom->setText(page ? QStringLiteral("%1 %").arg(qRound(page->zoom() * 100)) : QStringLiteral("100 %"));
    m_zoom->updateGeometry();
}

void ReaderWindow::updateMatches()
{
    DocumentPage *page = current();
    if (!page || m_find->lineEdit()->text().isEmpty()) {
        m_matches->clear();
        return;
    }
    const int count = page->matchCount();
    if (count < 0)
        m_matches->clear();
    else if (count == 0)
        m_matches->setText(tr("No matches"));
    else if (page->currentMatch() >= 0)
        m_matches->setText(tr("%1 of %2").arg(page->currentMatch() + 1).arg(count));
    else
        m_matches->setText(count == 1 ? tr("1 match") : tr("%n matches", nullptr, count));
}

void ReaderWindow::remember(const QString &path)
{
    QSettings settings = readerSettings();
    QStringList recent = settings.value(QStringLiteral("recent")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    settings.setValue(QStringLiteral("recent"), recent.mid(0, MaxRecent));
}

void ReaderWindow::updateRecent()
{
    while (QLayoutItem *item = m_recent->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int shown = 0;
    const QStringList recent = readerSettings().value(QStringLiteral("recent")).toStringList();
    for (const QString &path : recent) {
        if (!QFileInfo::exists(path))
            continue;
        auto *button = new Ui::PillButton(QStringLiteral("file-text"), QFileInfo(path).fileName(), m_recentBox);
        button->setToolTip(QDir::toNativeSeparators(path));
        connect(button, &QAbstractButton::clicked, this, [this, path] { openFiles({path}); });
        m_recent->addWidget(button, 0, Qt::AlignHCenter);
        ++shown;
    }
    m_recentBox->setVisible(shown > 0);
}

void ReaderWindow::closeEvent(QCloseEvent *event)
{
    for (DocumentPage *page : std::as_const(m_pages))
        page->savePosition();
    event->accept();
}

void ReaderWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void ReaderWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    openFiles(paths);
}

ReaderService::ReaderService(ReaderWindow *window)
    : QObject(window)
    , m_window(window)
{
}

void ReaderService::Open(const QStringList &paths)
{
    m_window->openFiles(paths);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}
