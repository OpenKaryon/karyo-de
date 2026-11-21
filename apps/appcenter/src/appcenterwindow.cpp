// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcenterwindow.h"
#include "pages.h"
#include "widgets.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <karyo/ui/icons.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
namespace Ui = Karyo::Ui;
namespace
{
// The app's 30px accent tile with the package glyph.
class AppTile : public QWidget
{
public:
    AppTile() { setFixedSize(30, 30); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(painter, rect(), 9, Ui::accent(500));
        painter.drawPixmap(
            6, 6, Ui::icon(QStringLiteral("package"), 18, Ui::neutral(100), devicePixelRatioF()));
    }
};

// "Sources": where packages come from, at the bottom of the sidebar.
class SourcesCard : public QWidget
{
public:
    explicit SourcesCard(const QStringList &sources)
        : m_sources(sources.join(QStringLiteral(" · ")))
    {
        setFixedHeight(84);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, rect(), 8, t.card);
        p.setFont(Ui::bodyFont(13, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(QRect(16, 14, width() - 32, 18), Qt::AlignVCenter, AppCenterWindow::tr("Sources"));
        p.setFont(Ui::bodyFont(12));
        p.setPen(t.muted);
        p.drawText(QRect(16, 36, width() - 32, 40), Qt::TextWordWrap | Qt::AlignTop, m_sources);
    }

private:
    QString m_sources;
};

QWidget *scrolled(Page *page)
{
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setWidget(page);
    // setWidget() turns the fill on; pages sit on the inset panel.
    page->setAutoFillBackground(false);
    return scroll;
}

} // namespace

AppCenterWindow::AppCenterWindow()
    : m_packages(new PackageManager(this))
    , m_search(new Ui::SearchField(tr("Search apps, tools and packages")))
    , m_back(new Ui::IconButton(QStringLiteral("arrow-left"), nullptr, { 36, 34 }, 18))
    , m_title(new QLabel(tr("App Center")))
    , m_debounce(new QTimer(this))
{
    setWindowTitle(tr("App Center"));
    m_packages->setObjectName("appcenterPackages");
    m_search->setObjectName("appcenterSearch");
    m_search->setMinimumWidth(240);
    m_search->setMaximumWidth(620);
    m_back->setObjectName("appcenterBack");
    m_back->setHint(tr("Back"));
    m_title->setFont(Ui::bodyFont(14, QFont::DemiBold));
    titleLayout()->setContentsMargins(16, 0, 8, 0);
    titleLayout()->addWidget(m_back);
    titleLayout()->addWidget(new AppTile);
    titleLayout()->addWidget(m_title);
    titleLayout()->addStretch(1);
    titleLayout()->addWidget(m_search, 2);
    titleLayout()->addStretch(1);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(450);
    connect(m_debounce, &QTimer::timeout, this, &AppCenterWindow::search);
    connect(m_search->lineEdit(), &QLineEdit::textChanged, this,
        [this](const QString &text)
        {
            if (text.trimmed().size() >= 2)
                m_debounce->start();
            else
            {
                m_debounce->stop();
                if (m_view == View::Search)
                    showIndex(m_current);
            }
        });
    connect(m_search->lineEdit(), &QLineEdit::returnPressed, this,
        [this]
        {
            m_debounce->stop();
            search();
        });
    connect(m_back, &QAbstractButton::clicked, this, &AppCenterWindow::back);
    connect(m_packages, &PackageManager::updatesChanged, this, &AppCenterWindow::updateBadge);
    connect(m_packages, &PackageManager::queryFailed, this, &AppCenterWindow::message);
    connect(m_packages, &PackageManager::jobFailed, this, [this](const CatalogApp &app, const QString &error)
        { message(tr("Could not finish %1: %2").arg(app.name.isEmpty() ? app.package : app.name, error)); });
    connect(m_packages, &PackageManager::environmentExported, this,
        [this](const QString &file) { message(tr("Environment saved to %1").arg(file)); });
    connect(Ui::ThemeNotifier::instance(), &Ui::ThemeNotifier::changed, this,
        [this] { QTimer::singleShot(0, this, &AppCenterWindow::build); });
    m_notice = m_packages->catalogError();
    build();
    m_packages->refresh();
    m_packages->checkUpdates();
}

void AppCenterWindow::build()
{
    const auto &t = Ui::Theme::current();
    const auto previous = m_view;
    m_pages.clear();
    m_extra = nullptr; // Old body owns its old details wrapper.
    setBodyColor(t.pane);
    setControlColors(t.muted, t.hover);
    m_back->setGlyphColor(t.text);
    m_search->setFillColor(t.card);
    QPalette titlePalette = m_title->palette();
    titlePalette.setColor(QPalette::WindowText, t.text);
    m_title->setPalette(titlePalette);
    auto *body = new QWidget;
    auto *row = new QHBoxLayout(body);
    row->setContentsMargins(12, 0, 12, 12);
    row->setSpacing(12);
    auto *side = new QVBoxLayout;
    m_nav = new Ui::NavList;
    m_nav->setObjectName("appcenterNavigation");
    m_nav->setFixedWidth(216);
    m_nav->addItem("compass", tr("Discover"));
    m_nav->addItem("dna", tr("Bioinformatics"));
    m_nav->addItem("layout-grid", tr("Applications"));
    m_nav->addItem("square-terminal", tr("Command-line tools"));
    m_nav->addItem("boxes", tr("Environments"));
    m_nav->addItem("download", tr("Updates"));
    m_nav->addItem("circle-check", tr("Installed"));
    side->addWidget(m_nav);
    side->addStretch(1);
    side->addWidget(new SourcesCard(m_packages->sources()));
    row->addLayout(side);
    auto *panel = new Ui::InsetPanel;
    auto *column = new QVBoxLayout(panel);
    column->setContentsMargins(0, 0, 0, 0);
    m_message = new QLabel(m_notice);
    m_message->setObjectName("appcenterStatus");
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_message->setMargin(12);
    m_message->setVisible(!m_notice.isEmpty());
    column->addWidget(m_message);
    m_stack = new QStackedWidget;
    m_stack->setObjectName("appcenterPages");
    column->addWidget(m_stack, 1);
    row->addWidget(panel, 1);
    m_pages = { createDiscoverPage(m_packages),
        createCategoryPage(m_packages, "bio", tr("Bioinformatics"),
            tr("Aligners, quantifiers, viewers, assemblers and workflow managers from your system "
               "repositories and bioconda.")),
        createCategoryPage(
            m_packages, "apps", tr("Applications"), tr("Office, graphics, statistics and everyday apps.")),
        createCategoryPage(m_packages, "cli", tr("Command-line tools"),
            tr("Tools for the terminal: data transfer, search, containers and more.")),
        createEnvironmentsPage(m_packages), createUpdatesPage(m_packages), createInstalledPage(m_packages) };
    for (auto *page : std::as_const(m_pages))
    {
        m_stack->addWidget(scrolled(page));
        connect(page, &Page::detailsRequested, this, &AppCenterWindow::openDetails);
        connect(page, &Page::navigate, this, &AppCenterWindow::showSection);
    }
    m_searchPage = new SearchPage(m_packages);
    m_searchScroll = scrolled(m_searchPage);
    m_stack->addWidget(m_searchScroll);
    connect(m_searchPage, &Page::detailsRequested, this, &AppCenterWindow::openDetails);
    connect(m_nav, &Ui::NavList::currentChanged, this, &AppCenterWindow::showIndex);
    // Re-selecting the current category must also leave search/details.
    connect(m_nav, &Ui::NavList::activated, this, &AppCenterWindow::showIndex);
    setCentralWidget(body);
    showIndex(m_current);
    if (previous == View::Search)
        search();
    else if (previous == View::Details)
    {
        const auto before = m_beforeDetails;
        if (before == View::Search)
            search();
        openDetails(m_detail);
        m_beforeDetails = before;
    }
    updateBadge();
}

void AppCenterWindow::message(const QString &text)
{
    m_notice = text;
    if (m_message)
    {
        m_message->setText(text);
        m_message->setVisible(!text.isEmpty());
    }
}

void AppCenterWindow::clearExtra()
{
    if (m_extra)
    {
        m_stack->removeWidget(m_extra);
        m_extra->deleteLater();
        m_extra = nullptr;
    }
}

void AppCenterWindow::showIndex(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    m_debounce->stop();
    clearExtra();
    m_current = index;
    m_view = View::Section;
    m_stack->setCurrentIndex(index);
    if (m_nav->currentIndex() != index)
        m_nav->setCurrentIndex(index);
    m_pages.at(index)->refresh();
    m_back->setEnabled(false);
}

void AppCenterWindow::showSection(const QString &key) { showIndex(int(m_keys.indexOf(key))); }

void AppCenterWindow::openDetails(const CatalogApp &app)
{
    if (m_view != View::Details)
        m_beforeDetails = m_view;
    clearExtra();
    m_detail = app;
    m_extra = scrolled(new DetailsPage(m_packages, app));
    m_stack->addWidget(m_extra);
    m_stack->setCurrentWidget(m_extra);
    m_view = View::Details;
    m_back->setEnabled(true);
}

void AppCenterWindow::showApp(const QString &id)
{
    for (const auto &app : m_packages->catalog())
        if (app.id == id)
        {
            openDetails(app);
            return;
        }
    message(tr("The app “%1” is not in the current catalog.").arg(id));
}

void AppCenterWindow::search()
{
    const auto query = m_search->lineEdit()->text().trimmed();
    if (query.size() < 2)
        return;
    clearExtra();
    m_searchPage->search(query);
    m_stack->setCurrentWidget(m_searchScroll);
    m_view = View::Search;
    m_back->setEnabled(true);
}

void AppCenterWindow::back()
{
    const bool toSearch = m_view == View::Details && m_beforeDetails == View::Search;
    clearExtra();
    if (toSearch)
    {
        m_stack->setCurrentWidget(m_searchScroll);
        m_view = View::Search;
        m_back->setEnabled(true);
    }
    else
        showIndex(m_current);
}

void AppCenterWindow::updateBadge()
{
    const auto count = m_packages->updates().size();
    m_nav->setBadge(5, count > 0 ? QString::number(count) : QString());
}
