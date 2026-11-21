// SPDX-License-Identifier: GPL-3.0-or-later
#include "documentpage.h"

#include "thumbnails.h"

#include <karyo/ui/menu.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QDesktopServices>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUrl>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace Ui = Karyo::Ui;

namespace {

QSettings readerSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("reader"));
}

QString positionKey(const QString &path)
{
    return QStringLiteral("positions/")
           + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

} // namespace

DocumentPage::DocumentPage(QWidget *parent)
    : QWidget(parent)
    , m_document(new QPdfDocument(this))
    , m_view(new QPdfView)
    , m_search(nullptr)
    , m_searchTimer(new QTimer(this))
    , m_bookmarks(new QPdfBookmarkModel(this))
    , m_sidebar(new Ui::InsetPanel)
    , m_sidebarPages(new QStackedWidget)
    , m_thumbnails(new Thumbnails(m_document))
    , m_thumbScroll(new QScrollArea)
    , m_contents(new QTreeView)
{
    const Ui::Theme &t = Ui::Theme::current();
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);

    // Sidebar: thumbnails or the contents.
    m_sidebar->setFixedWidth(200);
    auto *sidebarLayout = new QVBoxLayout(m_sidebar);
    sidebarLayout->setContentsMargins(0, 8, 0, 8);
    m_thumbScroll->setFrameShape(QFrame::NoFrame);
    m_thumbScroll->setWidgetResizable(true);
    m_thumbScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbScroll->setWidget(m_thumbnails);
    m_thumbScroll->viewport()->setAutoFillBackground(false);
    m_thumbnails->setAutoFillBackground(false);
    m_sidebarPages->addWidget(m_thumbScroll);
    m_contents->setModel(m_bookmarks);
    m_contents->setHeaderHidden(true);
    m_contents->setFrameShape(QFrame::NoFrame);
    m_contents->setFont(Ui::bodyFont(13));
    m_contents->setAutoFillBackground(false);
    m_contents->viewport()->setAutoFillBackground(false);
    m_contents->setWordWrap(true);
    m_sidebarPages->addWidget(m_contents);
    sidebarLayout->addWidget(m_sidebarPages);
    row->addWidget(m_sidebar);

    // The pages on the desk color, as paper on a table.
    m_bookmarks->setDocument(m_document);
    m_view->setDocument(m_document);
    m_view->setPageMode(QPdfView::PageMode::MultiPage);
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    m_view->setPageSpacing(14);
    m_view->setDocumentMargins(QMargins(24, 20, 24, 20));
    m_view->setFrameShape(QFrame::NoFrame);
    QPalette palette = m_view->palette();
    palette.setColor(QPalette::Dark, t.desk);
    m_view->setPalette(palette);
    m_view->viewport()->installEventFilter(this);
    row->addWidget(m_view, 1);

    connect(m_view->pageNavigator(), &QPdfPageNavigator::currentPageChanged, this, [this](int page) {
        m_thumbnails->setCurrent(page);
        // Keep the current page's thumbnail in sight.
        const int top = m_thumbnails->pageTop(page);
        m_thumbScroll->ensureVisible(0, top + 90, 0, 110);
        Q_EMIT pageChanged();
    });
    connect(m_view, &QPdfView::zoomFactorChanged, this, &DocumentPage::zoomChanged);
    connect(m_thumbnails, &Thumbnails::activated, this, &DocumentPage::goToPage);
    connect(m_contents, &QTreeView::activated, this, [this](const QModelIndex &index) {
        goToPage(index.data(int(QPdfBookmarkModel::Role::Page)).toInt());
    });
    connect(m_contents, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        goToPage(index.data(int(QPdfBookmarkModel::Role::Page)).toInt());
    });
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchTimer, &QTimer::timeout, this, &DocumentPage::runSearch);
    runSearch();

    setSidebar(readerSettings().value(QStringLiteral("sidebar"), 1).toInt());
}

DocumentPage::~DocumentPage() { savePosition(); }

bool DocumentPage::open(const QString &path)
{
    m_path = QFileInfo(path).absoluteFilePath();
    QPdfDocument::Error error = m_document->load(m_path);
    // A locked PDF: ask until the password fits or the reader gives up.
    while (error == QPdfDocument::Error::IncorrectPassword) {
        bool ok = false;
        const QString password = QInputDialog::getText(
            this, tr("Password"), tr("“%1” is protected. Password:").arg(QFileInfo(path).fileName()),
            QLineEdit::Password, QString(), &ok);
        if (!ok) {
            m_error = tr("The document is protected by a password.");
            return false;
        }
        m_document->setPassword(password);
        error = m_document->load(m_path);
    }
    switch (error) {
    case QPdfDocument::Error::None:
        break;
    case QPdfDocument::Error::FileNotFound:
        m_error = tr("The file does not exist.");
        return false;
    case QPdfDocument::Error::InvalidFileFormat:
        m_error = tr("This is not a PDF document.");
        return false;
    default:
        m_error = tr("The document could not be read.");
        return false;
    }
    m_thumbnails->reset();
    m_contents->expandToDepth(0);
    // The page and zoom come back once the view has its size.
    m_restorePending = true;
    if (isVisible())
        QTimer::singleShot(0, this, &DocumentPage::restorePosition);
    return true;
}

QString DocumentPage::title() const { return QFileInfo(m_path).fileName(); }

int DocumentPage::pageCount() const { return m_document->pageCount(); }

int DocumentPage::currentPage() const { return m_view->pageNavigator()->currentPage(); }

void DocumentPage::goToPage(int page)
{
    page = qBound(0, page, qMax(0, pageCount() - 1));
    m_view->pageNavigator()->jump(page, QPointF(), m_view->zoomMode() == QPdfView::ZoomMode::Custom ? m_view->zoomFactor() : 0);
}

void DocumentPage::setFit(Fit fit)
{
    m_fit = fit;
    m_view->setZoomMode(fit == Fit::Width  ? QPdfView::ZoomMode::FitToWidth
                        : fit == Fit::Page ? QPdfView::ZoomMode::FitInView
                                           : QPdfView::ZoomMode::Custom);
    Q_EMIT zoomChanged();
}

void DocumentPage::zoomTo(qreal factor)
{
    const int page = currentPage();
    m_fit = Fit::Custom;
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
    m_view->setZoomFactor(qBound(0.1, factor, 8.0));
    goToPage(page);
    Q_EMIT zoomChanged();
}

void DocumentPage::zoomBy(qreal factor) { zoomTo(zoom() * factor); }

qreal DocumentPage::zoom() const
{
    if (m_fit == Fit::Custom)
        return m_view->zoomFactor();
    // The fit modes leave zoomFactor alone; work it out as QPdfView lays out.
    const QSizeF points = m_document->pagePointSize(qMax(0, currentPage()));
    if (points.isEmpty())
        return 1.0;
    const qreal resolution = m_view->logicalDpiY() / 72.0;
    const QMargins margins = m_view->documentMargins();
    const QSize viewport = m_view->viewport()->size();
    const qreal byWidth = (viewport.width() - margins.left() - margins.right()) / (points.width() * resolution);
    if (m_fit == Fit::Width)
        return byWidth;
    return qMin(byWidth, (viewport.height() - margins.top() - margins.bottom()) / (points.height() * resolution));
}

void DocumentPage::setSidebar(int mode)
{
    m_sidebarMode = mode;
    m_sidebar->setVisible(mode != 0);
    if (mode != 0)
        m_sidebarPages->setCurrentIndex(mode - 1);
    readerSettings().setValue(QStringLiteral("sidebar"), mode);
}

bool DocumentPage::hasContents() const { return m_bookmarks->rowCount(QModelIndex()) > 0; }

void DocumentPage::search(const QString &text)
{
    m_match = -1;
    m_searchText = text;
    // Typing restarts the wait; clearing the field clears at once.
    if (text.isEmpty())
        runSearch();
    else
        m_searchTimer->start();
    Q_EMIT matchesChanged();
}

void DocumentPage::runSearch()
{
    m_searchTimer->stop();
    // A fresh model for every search: QPdfSearchModel (Qt 6.8) loses results
    // when its string changes while it is still going through the pages.
    auto *model = new QPdfSearchModel(this);
    model->setDocument(m_document);
    // QPdfView repaints itself, not its viewport, when results come in.
    connect(model, &QPdfSearchModel::countChanged, this, [this] {
        m_view->viewport()->update();
        Q_EMIT matchesChanged();
    });
    m_view->setSearchModel(model);
    m_view->viewport()->update();
    if (m_search)
        m_search->deleteLater();
    m_search = model;
    m_match = -1;
    model->setSearchString(m_searchText);
    Q_EMIT matchesChanged();
}

int DocumentPage::matchCount() const { return m_searchTimer->isActive() ? -1 : int(m_search->count()); }

void DocumentPage::nextMatch(bool backward)
{
    if (m_searchTimer->isActive())
        runSearch();
    const int count = matchCount();
    if (count <= 0)
        return;
    m_match = backward ? (m_match <= 0 ? count - 1 : m_match - 1) : (m_match + 1) % count;
    showMatch();
}

void DocumentPage::showMatch()
{
    m_view->setCurrentSearchResultIndex(m_match);
    const QPdfLink link = m_search->resultAtIndex(m_match);
    if (link.isValid())
        m_view->pageNavigator()->jump(link);
    Q_EMIT matchesChanged();
}

void DocumentPage::savePosition() const
{
    if (m_path.isEmpty() || m_document->status() != QPdfDocument::Status::Ready)
        return;
    QSettings settings = readerSettings();
    settings.setValue(positionKey(m_path), QStringList{QString::number(currentPage()), QString::number(int(m_fit)),
                                                       QString::number(m_view->zoomFactor())});
}

void DocumentPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_restorePending)
        QTimer::singleShot(0, this, &DocumentPage::restorePosition);
}

void DocumentPage::restorePosition()
{
    if (!m_restorePending)
        return;
    m_restorePending = false;
    const QStringList saved = readerSettings().value(positionKey(m_path)).toStringList();
    if (saved.size() != 3)
        return;
    const Fit fit = Fit(qBound(0, saved.at(1).toInt(), 2));
    if (fit == Fit::Custom)
        zoomTo(saved.at(2).toDouble());
    else
        setFit(fit);
    goToPage(saved.at(0).toInt());
}

void DocumentPage::showPageMenu(const QPoint &globalPos)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    const int page = currentPage();
    menu->addAction(tr("Copy text of page %1").arg(page + 1), this, [this, page] {
        QGuiApplication::clipboard()->setText(m_document->getAllText(page).text());
    });
    menu->addAction(tr("Show in Files"), this, [this] {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
            QStringLiteral("/org/freedesktop/FileManager1"),
            QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
        message << QStringList{QUrl::fromLocalFile(m_path).toString()} << QString();
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, path = m_path] {
            QDBusPendingReply<> reply = *watcher;
            if (reply.isError())
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
            watcher->deleteLater();
        });
    });
    menu->popup(globalPos);
}

bool DocumentPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view->viewport()) {
        if (event->type() == QEvent::Resize && m_fit != Fit::Custom) {
            Q_EMIT zoomChanged();
        } else if (event->type() == QEvent::Wheel) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            // Ctrl and the wheel zoom, as everywhere.
            if (wheel->modifiers() & Qt::ControlModifier) {
                zoomBy(wheel->angleDelta().y() > 0 ? 1.1 : 1 / 1.1);
                return true;
            }
        } else if (event->type() == QEvent::ContextMenu) {
            showPageMenu(static_cast<QContextMenuEvent *>(event)->globalPos());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
