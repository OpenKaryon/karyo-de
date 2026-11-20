// SPDX-License-Identifier: GPL-3.0-or-later
#include "integrations.h"
#include "startmenu.h"

#include "layersurface.h"
#include "sessionlocker.h"
#include "startwidgets.h"
#include "symbols.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <pwd.h>
#include <unistd.h>

#include <algorithm>

namespace Ui = Karyo::Ui;

namespace {

constexpr int CardWidth = 640;
constexpr int CardHeight = 680;
constexpr int CardRadius = 8;
constexpr int FooterHeight = 60;
constexpr int TaskbarHeight = 48;
constexpr int GapAboveTaskbar = 20;

// The --k-card panel that holds a list (all apps of a category).
class CardPanel : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(painter, rect(), 8, Ui::Theme::current().card);
    }
};

// Avatar with initials and the user's full name.
class UserBadge : public QWidget
{
public:
    explicit UserBadge(QWidget *parent)
        : QWidget(parent)
    {
        const passwd *pw = getpwuid(getuid());
        const QString login = pw ? QString::fromLocal8Bit(pw->pw_name) : qEnvironmentVariable("USER");
        const QString gecos = pw ? QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0)
                                 : QString();
        m_name = gecos.trimmed().isEmpty() ? login : gecos.trimmed();
        const QStringList words = m_name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &word : words.mid(0, 2))
            m_initials += word.at(0).toUpper();
        setFixedHeight(40);
        setMinimumWidth(36 + 12 + QFontMetrics(Ui::bodyFont(14, QFont::DemiBold)).horizontalAdvance(m_name));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF avatar(0, (height() - 36) / 2.0, 36, 36);
        Ui::fillPill(painter, avatar, Ui::sage(500));
        painter.setFont(Ui::bodyFont(13, QFont::Bold));
        painter.setPen(Ui::neutral(100));
        painter.drawText(avatar, Qt::AlignCenter, m_initials);
        painter.setFont(Ui::bodyFont(14, QFont::DemiBold));
        painter.setPen(t.text);
        painter.drawText(QRect(48, 0, width() - 48, height()), Qt::AlignVCenter | Qt::AlignLeft, m_name);
    }

private:
    QString m_name;
    QString m_initials;
};

QScrollArea *scrollFor(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setAutoFillBackground(false);
    scroll->setWidget(content);
    // setWidget() turns the fill on; the list sits on the card.
    content->setAutoFillBackground(false);
    return scroll;
}

int score(const AppInfo &app, const QString &query)
{
    const QString name = app.entry.name;
    if (name.startsWith(query, Qt::CaseInsensitive))
        return 100;
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &word : words) {
        if (word.startsWith(query, Qt::CaseInsensitive))
            return 80;
    }
    if (name.contains(query, Qt::CaseInsensitive))
        return 60;
    if (app.entry.genericName.contains(query, Qt::CaseInsensitive))
        return 45;
    for (const QString &keyword : app.entry.keywords) {
        if (keyword.startsWith(query, Qt::CaseInsensitive))
            return 40;
    }
    const QStringList command = app.entry.command();
    if (!command.isEmpty() && QFileInfo(command.first()).fileName().startsWith(query, Qt::CaseInsensitive))
        return 40;
    return 0;
}

QList<SearchResult> matchApps(const QList<AppInfo> &apps, const QString &query, SearchResult::Kind kind,
                              int limit, int *best)
{
    QList<QPair<int, AppInfo>> scored;
    for (const AppInfo &app : apps) {
        const int s = score(app, query);
        if (s > 0)
            scored.append({s, app});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    *best = scored.isEmpty() ? 0 : scored.first().first;
    QList<SearchResult> out;
    for (const auto &item : scored.mid(0, limit)) {
        SearchResult result;
        result.kind = kind;
        result.title = item.second.entry.name;
        result.app = item.second;
        result.trailing = kind == SearchResult::App ? AppIndex::labelFor(item.second.category) : QString();
        out.append(result);
    }
    return out;
}

} // namespace

StartMenu::StartMenu(AppIndex *index, QScreen *screen, TaskbarPins taskbarPins)
    : Flyout(screen)
    , m_index(index)
    , m_taskbarPins(std::move(taskbarPins))
{
    // Created here, not in the initializer list: Flyout filters the whole
    // application from its constructor on, and eventFilter() runs while
    // they are constructed, so it must see them as null until then.
    m_search = new Ui::SearchField(tr("Search apps, files, commands and settings"), this);
    m_pages = new QStackedWidget(this);
    setWindowTitle(QStringLiteral("karyo-start"));
    m_index->refresh();

    const QMarginsF shadow = Ui::largeShadowMargins();
    const int room = screen->geometry().height() - TaskbarHeight - 2 * GapAboveTaskbar;
    setFixedSize(CardWidth + int(shadow.left() + shadow.right()),
                 qMin(CardHeight, room) + int(shadow.top() + shadow.bottom()));
    place(LayerSurface::AnchorBottom,
          QMargins(0, 0, 0, TaskbarHeight + GapAboveTaskbar - int(shadow.bottom())));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(shadow.toMargins());
    outer->setSpacing(0);
    auto *content = new QWidget(this);
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(24, 20, 24, 10);
    column->setSpacing(0);
    m_search->setFixedHeight(36);
    m_search->setFillColor(Ui::Theme::current().card);
    column->addWidget(m_search);
    column->addSpacing(16);
    column->addWidget(m_pages, 1);
    outer->addWidget(content, 1);
    outer->addWidget(buildFooter());

    m_pages->addWidget(buildHome());
    m_pages->addWidget(buildAllApps());
    m_pages->addWidget(buildSearch());
    refreshHome();

    // Settings > Personalization > Start layout: what opens first.
    const QString layout = QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                                     QStringLiteral("shell"))
                               .value(QStringLiteral("Start/layout"))
                               .toString();
    if (layout == QLatin1String("categories"))
        m_browsePage = Page::AllApps;
    else if (layout == QLatin1String("search"))
        m_browsePage = Page::Search;
    showPage(m_browsePage);
    if (m_browsePage == Page::Search)
        runSearch();

    // Keys in the field reach eventFilter() through the application filter.
    QLineEdit *edit = m_search->lineEdit();
    connect(edit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.trimmed().isEmpty()) {
            showPage(m_browsePage);
            if (m_browsePage == Page::Search)
                runSearch();
            return;
        }
        showPage(Page::Search);
        runSearch();
    });
    connect(m_index, &AppIndex::filesFound, this, [this](const QString &query, const QList<FileHit> &hits) {
        m_fileHits = hits;
        m_fileQuery = query;
        if (m_page == Page::Search && query == m_search->lineEdit()->text().trimmed())
            runSearch();
    });
    connect(m_index, &AppIndex::changed, this, &StartMenu::refreshHome);
    edit->setFocus();
}

QLabel *StartMenu::heading(const QString &text, QWidget *parent) const
{
    auto *label = new QLabel(text, parent);
    label->setFont(Ui::headingFont(15));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, Ui::Theme::current().text);
    label->setPalette(palette);
    return label;
}

QWidget *StartMenu::buildHome()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *pinnedRow = new QHBoxLayout;
    pinnedRow->setContentsMargins(8, 0, 8, 0);
    pinnedRow->addWidget(heading(tr("Pinned"), page));
    pinnedRow->addStretch(1);
    auto *allApps = new LinkButton(tr("All apps"), QString(), true, page);
    connect(allApps, &QAbstractButton::clicked, this, [this] {
        m_browsePage = Page::AllApps;
        showPage(Page::AllApps);
    });
    pinnedRow->addWidget(allApps);
    v->addLayout(pinnedRow);
    v->addSpacing(12);

    m_pinned = new PinnedGrid(page);
    connect(m_pinned, &PinnedGrid::activated, this, [this](const AppInfo &app) {
        StartActions::launch(app);
        close();
    });
    connect(m_pinned, &PinnedGrid::contextMenuRequested, this, &StartMenu::showAppMenu);
    connect(m_pinned, &PinnedGrid::reordered, m_index, &AppIndex::setPinnedOrder);
    v->addWidget(m_pinned);
    v->addSpacing(10);

    auto *recentRow = new QHBoxLayout;
    recentRow->setContentsMargins(8, 0, 8, 0);
    recentRow->addWidget(heading(tr("Recommended"), page));
    recentRow->addStretch(1);
    v->addLayout(recentRow);
    v->addSpacing(8);
    m_recent = new RecentList(page);
    connect(m_recent, &RecentList::activated, this, [this](const QString &path) {
        StartActions::openFile(path);
        close();
    });
    v->addWidget(m_recent);
    v->addStretch(1);
    return page;
}

QWidget *StartMenu::buildAllApps()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(8, 0, 0, 0);
    header->setSpacing(10);
    header->addWidget(heading(tr("All apps"), page), 0, Qt::AlignBaseline);
    m_allAppsCount = new QLabel(page);
    m_allAppsCount->setFont(Ui::bodyFont(13));
    QPalette muted = m_allAppsCount->palette();
    muted.setColor(QPalette::WindowText, Ui::Theme::current().muted);
    m_allAppsCount->setPalette(muted);
    header->addWidget(m_allAppsCount, 0, Qt::AlignBaseline);
    header->addStretch(1);
    auto *back = new LinkButton(tr("Back"), QStringLiteral("arrow-left"), false, page);
    connect(back, &QAbstractButton::clicked, this, [this] {
        m_browsePage = Page::Home;
        showPage(Page::Home);
    });
    header->addWidget(back);
    v->addLayout(header);
    v->addSpacing(16);

    auto *body = new QHBoxLayout;
    body->setSpacing(16);
    // Installed and not installed categories together can be taller than Start.
    m_categories = new Ui::NavList(page);
    m_categories->setRowHeight(36);
    connect(m_categories, &Ui::NavList::currentChanged, this, &StartMenu::showCategory);
    auto *categories = scrollFor(m_categories, page);
    categories->setFixedWidth(200);
    body->addWidget(categories);

    auto *panel = new CardPanel(page);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);
    m_appList = new AppList(panel);
    connect(m_appList, &AppList::activated, this, [this](const AppInfo &app) {
        StartActions::launch(app);
        close();
    });
    connect(m_appList, &AppList::contextMenuRequested, this, &StartMenu::showAppMenu);
    panelLayout->addWidget(scrollFor(m_appList, panel));
    body->addWidget(panel, 1);
    v->addLayout(body, 1);
    return page;
}

QWidget *StartMenu::buildSearch()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *chips = new QHBoxLayout;
    chips->setSpacing(8);
    for (const QString &label : {tr("All"), tr("Apps"), tr("Commands"), tr("Files"), tr("Settings")}) {
        auto *chip = new ChipButton(label, page);
        connect(chip, &QAbstractButton::toggled, this, [this](bool checked) {
            if (checked)
                runSearch();
        });
        chips->addWidget(chip);
        m_chips.append(chip);
    }
    m_chips.first()->setChecked(true);
    chips->addStretch(1);
    v->addLayout(chips);
    v->addSpacing(14);

    auto *body = new QHBoxLayout;
    body->setSpacing(16);
    m_results = new ResultList(page);
    connect(m_results, &ResultList::activated, this, &StartMenu::activate);
    body->addWidget(scrollFor(m_results, page), 1);
    m_preview = new PreviewCard(m_index, page);
    connect(m_preview, &PreviewCard::done, this, &QWidget::close);
    connect(m_results, &ResultList::currentChanged, this, [this] {
        const SearchResult *current = m_results->current();
        m_preview->setResult(current);
        m_preview->setVisible(current != nullptr);
    });
    body->addWidget(m_preview);
    v->addLayout(body, 1);
    return page;
}

QWidget *StartMenu::buildFooter()
{
    auto *footer = new QWidget(this);
    footer->setFixedHeight(FooterHeight);
    auto *h = new QHBoxLayout(footer);
    h->setContentsMargins(26, 0, 22, 0);
    h->setSpacing(8);
    h->addWidget(new UserBadge(footer));
    h->addStretch(1);

    const Ui::Theme &t = Ui::Theme::current();
    auto button = [&](const QString &glyph, const QString &hint) {
        auto *b = new Ui::IconButton(glyph, footer, {40, 40}, 18);
        b->setGlyphColor(t.text);
        b->setFocusPolicy(Qt::NoFocus);
        b->setHint(hint);
        h->addWidget(b);
        return b;
    };
    auto *files = button(QStringLiteral("folder"), tr("Files"));
    connect(files, &QAbstractButton::clicked, this, [this] {
        StartActions::openFile(QDir::homePath());
        close();
    });
    auto *settings = button(QStringLiteral("settings"), tr("Settings"));
    settings->setEnabled(!Integrations::settingsProgram(false).isEmpty());
    connect(settings, &QAbstractButton::clicked, this, [this] {
        Integrations::openSettings();
        close();
    });
    auto *power = button(QStringLiteral("power"), tr("Power"));
    connect(power, &QAbstractButton::clicked, this, [this, power] { showPowerMenu(power); });
    return footer;
}

void StartMenu::refreshHome()
{
    m_pinned->setApps(m_index->pinnedApps());
    m_recent->setFiles(m_index->recentFiles(6));
    // Installed apps by category (empty ones left out), then App Center's
    // tools that are not installed, by category too.
    const int current = qMax(0, m_categories->currentIndex());
    m_categories->clear();
    m_categoryRows.clear();
    auto addCategories = [this](const QList<Category> &categories, bool installed) {
        for (const Category &category : categories) {
            const int row = m_categories->addItem(category.glyph, category.label);
            m_categories->setTrailing(row, QString::number(category.count));
            m_categoryRows.insert(row, {category.key, installed});
        }
    };
    addCategories(m_index->categories(true), true);
    const QList<Category> notInstalled = m_index->categories(false);
    if (!notInstalled.isEmpty()) {
        m_categories->addHeading(tr("Not installed"));
        addCategories(notInstalled, false);
    }
    m_categories->setCurrentIndex(m_categoryRows.contains(current) ? current : 0);
    showCategory(m_categories->currentIndex());
    // English has no numerus translation: the singular is its own string.
    const int apps = m_index->installedCount();
    const int tools = m_index->commandLineCount();
    m_allAppsCount->setText((apps == 1 ? tr("1 app") : tr("%n apps", nullptr, apps)) + QStringLiteral(" · ")
                            + (tools == 1 ? tr("1 command-line tool") : tr("%n command-line tools", nullptr, tools)));
}

void StartMenu::showCategory(int row)
{
    const auto category = m_categoryRows.constFind(row);
    if (category == m_categoryRows.constEnd())
        return;
    m_appList->setApps(m_index->appsIn(category->key, category->installed));
}

void StartMenu::showPage(Page page)
{
    m_page = page;
    m_pages->setCurrentIndex(int(page));
}

void StartMenu::runSearch()
{
    // Checking the first chip while the page is built calls this early.
    if (!m_results || !m_preview)
        return;
    int scope = 0;
    for (int i = 0; i < m_chips.size(); ++i) {
        if (m_chips.at(i)->isChecked())
            scope = i;
    }
    QList<AppInfo> settingsApps = m_index->settings();
    if (scope == 4) {
        // The main Settings app stays among Apps in All, but belongs in
        // the dedicated Settings scope together with individual modules.
        for (const AppInfo &app : m_index->apps()) {
            if (app.entry.categories.contains(QStringLiteral("Settings")))
                settingsApps.prepend(app);
        }
    }
    const QString query = m_search->lineEdit()->text().trimmed();
    if (query.isEmpty() && scope != 0) {
        QList<SearchResult> results;
        QString heading, emptyText;
        if (scope == 1 || scope == 4) {
            const auto &apps = scope == 1 ? m_index->apps() : settingsApps;
            for (const AppInfo &app : apps) {
                if (!app.installed)
                    continue;
                SearchResult result;
                result.kind = scope == 1 ? SearchResult::App : SearchResult::Setting;
                result.title = app.entry.name;
                result.app = app;
                result.trailing = AppIndex::labelFor(app.category);
                results.append(result);
            }
            heading = scope == 1 ? tr("Apps") : tr("Settings");
            emptyText = scope == 1 ? tr("No apps installed.") : tr("No settings available.");
        } else if (scope == 2) {
            QSet<QString> known;
            for (const AppInfo &tool : m_index->apps()) {
                if (!tool.isTool() || !tool.installed)
                    continue;
                SearchResult result;
                result.kind = SearchResult::Command;
                result.title = tool.entry.name;
                result.command = StartActions::toolCommandLine(tool);
                result.app = tool;
                result.trailing = tool.envs.value(0);
                results.append(result);
                known.insert(tool.entry.exec);
            }
            // Offer available commands even on a fresh install with no
            // catalog tools or command history. Typing narrows this list.
            for (const QString &name : m_index->commands(QString(), 40)) {
                if (known.contains(name))
                    continue;
                SearchResult result;
                result.kind = SearchResult::Command;
                result.title = name;
                result.command = name;
                result.trailing = QFileInfo(AppIndex::commandPath(name)).absolutePath();
                results.append(result);
            }
            heading = tr("Commands");
            emptyText = tr("No commands available.");
        } else if (scope == 3) {
            for (const FileHit &hit : m_index->recentFiles(40)) {
                SearchResult result;
                result.kind = SearchResult::File;
                result.file = hit;
                result.title = QFileInfo(hit.path).fileName();
                result.trailing = QFileInfo(QFileInfo(hit.path).absolutePath()).fileName();
                results.append(result);
            }
            heading = tr("Recent files");
            emptyText = tr("No recent files. Type a name to search.");
        }
        m_results->setResults({{QString(), {}}, {heading, results}}, QString(), emptyText);
        return;
    }
    if (query.isEmpty()) {
        // Before typing, as on Windows: the apps used lately, then recent
        // files. Without a history yet, the pinned apps stand in.
        QList<AppInfo> apps = m_index->recentApps(8);
        const bool used = !apps.isEmpty();
        if (!used)
            apps = m_index->pinnedApps().mid(0, 8);
        QList<SearchResult> recentApps, recentFiles;
        for (const AppInfo &app : std::as_const(apps)) {
            SearchResult result;
            result.kind = SearchResult::App;
            result.title = app.entry.name;
            result.app = app;
            result.trailing = AppIndex::labelFor(app.category);
            recentApps.append(result);
        }
        for (const FileHit &hit : m_index->recentFiles(4)) {
            SearchResult result;
            result.kind = SearchResult::File;
            result.file = hit;
            result.title = QFileInfo(hit.path).fileName();
            result.trailing = QFileInfo(QFileInfo(hit.path).absolutePath()).fileName();
            recentFiles.append(result);
        }
        m_results->setResults({{QString(), {}},
                               {used ? tr("Recent") : tr("Pinned"), recentApps},
                               {tr("Recent files"), recentFiles}},
                              QString());
        return;
    }
    const bool all = scope == 0;
    if (query != m_fileQuery && (all || scope == 3))
        m_index->searchFiles(query);

    int appScore = 0;
    int settingScore = 0;
    QList<SearchResult> apps, settings, commands, files, run;
    if (all || scope == 1)
        apps = matchApps(m_index->apps(), query, SearchResult::App, all ? 5 : 14, &appScore);
    if (all || scope == 4)
        settings = matchApps(settingsApps, query, SearchResult::Setting, all ? 3 : 14, &settingScore);
    int commandScore = 0;
    QSet<QString> toolCommands;
    if (all || scope == 2) {
        // Subcommands of matching tools first: "sam" offers samtools view,
        // sort and index, "samtools s" only sort.
        const int limit = all ? 4 : 14;
        for (const AppInfo &tool : m_index->apps()) {
            if (!tool.isTool() || !tool.installed || commands.size() >= limit)
                continue;
            const QString exec = tool.entry.exec;
            QString subPrefix;
            if (query.startsWith(exec + QLatin1Char(' '), Qt::CaseInsensitive))
                subPrefix = query.mid(exec.size() + 1).trimmed();
            else if (!exec.startsWith(query, Qt::CaseInsensitive)
                     && !tool.entry.name.startsWith(query, Qt::CaseInsensitive))
                continue;
            toolCommands.insert(exec);
            for (const QString &sub : tool.subcommands) {
                if (!sub.startsWith(subPrefix) || commands.size() >= limit)
                    continue;
                SearchResult result;
                result.kind = SearchResult::Command;
                result.title = exec + QLatin1Char(' ') + sub;
                result.command = StartActions::toolCommandLine(tool, sub);
                result.app = tool;
                result.trailing = tool.envs.value(0);
                commands.append(result);
                if (commandScore == 0)
                    commandScore = 60;
            }
        }
    }
    if (all || scope == 2) {
        const QString first = query.section(QLatin1Char(' '), 0, 0);
        if (query.contains(QLatin1Char(' ')) && !AppIndex::commandPath(first).isEmpty()) {
            SearchResult result;
            result.kind = SearchResult::Run;
            result.title = query;
            result.command = query;
            run.append(result);
        } else {
            for (const QString &name : m_index->commands(query, all ? 4 : 14)) {
                // A catalog tool is already among the apps.
                if (toolCommands.contains(name) || commands.size() >= (all ? 6 : 20))
                    continue;
                SearchResult result;
                result.kind = SearchResult::Command;
                result.title = name;
                result.command = name;
                result.trailing = QFileInfo(AppIndex::commandPath(name)).absolutePath();
                commands.append(result);
                if (commandScore == 0)
                    commandScore = name == query ? 90 : 70;
            }
        }
    }
    if ((all || scope == 3) && m_fileQuery == query) {
        for (const FileHit &hit : m_fileHits.mid(0, all ? 5 : 14)) {
            SearchResult result;
            result.kind = SearchResult::File;
            result.file = hit;
            result.title = QFileInfo(hit.path).fileName();
            result.trailing = QFileInfo(QFileInfo(hit.path).absolutePath()).fileName();
            files.append(result);
        }
    }

    // µ, °C, α by name, in All only.
    QList<SearchResult> symbols;
    bool exactSymbol = false;
    if (all) {
        for (const SymbolHit &hit : searchSymbols(query, 6)) {
            SearchResult result;
            result.kind = SearchResult::Symbol;
            result.title = hit.name;
            result.command = hit.text;
            result.trailing = symbolCodes(hit.text);
            symbols.append(result);
            exactSymbol = exactSymbol || (symbols.size() == 1 && hit.exact);
        }
    }

    // The best match leads; it leaves its own section.
    QList<SearchResult> best;
    auto take = [&best](QList<SearchResult> &from) {
        if (!from.isEmpty())
            best.append(from.takeFirst());
    };
    if (!run.isEmpty())
        take(run);
    else if (appScore > 0 && appScore >= commandScore && appScore >= settingScore)
        take(apps);
    else if (commandScore > 0 && commandScore >= settingScore)
        take(commands);
    else if (settingScore > 0)
        take(settings);
    else if (exactSymbol)
        take(symbols);
    else
        take(files);

    m_results->setResults({{tr("Best match"), best},
                           {tr("Apps"), apps},
                           {tr("Settings"), settings},
                           {tr("Commands"), commands},
                           {tr("Symbols"), symbols},
                           {tr("Files"), files}},
                          query);
}

void StartMenu::activate(const SearchResult &result)
{
    switch (result.kind) {
    case SearchResult::App:
    case SearchResult::Setting:
        StartActions::launch(result.app);
        break;
    case SearchResult::Command:
    case SearchResult::Run:
        StartActions::runInTerminal(result.command);
        break;
    case SearchResult::File:
        StartActions::openFile(result.file.path);
        break;
    case SearchResult::Symbol:
        StartActions::copyPath(result.command);
        break;
    }
    close();
}

void StartMenu::showAppMenu(const AppInfo &app, const QPoint &globalPos)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    menu->addAction(tr("Open"), this, [this, app] {
        StartActions::launch(app);
        close();
    });
    const bool pinned = m_index->isPinned(app.entry.id);
    menu->addAction(pinned ? tr("Unpin from Start") : tr("Pin to Start"), this,
                    [this, app, pinned] { m_index->setPinned(app.entry.id, !pinned); });
    // The taskbar holds desktop apps; catalog tools have no desktop entry.
    if (!app.isTool() && m_taskbarPins.isPinned && m_taskbarPins.setPinned) {
        const bool onTaskbar = m_taskbarPins.isPinned(app.entry.id);
        menu->addAction(onTaskbar ? tr("Unpin from taskbar") : tr("Pin to taskbar"), this,
                        [this, app, onTaskbar] { m_taskbarPins.setPinned(app.entry.id, !onTaskbar); });
    }
    menu->popup(globalPos);
}

void StartMenu::showPowerMenu(QWidget *anchor)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    auto run = [this](const QString &program, const QStringList &args) {
        QProcess::startDetached(program, args);
        close();
    };
    menu->addAction(tr("Lock"), this, [this] {
        close();
        SessionLocker::lock();
    });
    menu->addAction(tr("Sign out"), this, [run] { run(QStringLiteral("labwc"), {QStringLiteral("--exit")}); });
    menu->addAction(tr("Restart"), this, [run] { run(QStringLiteral("systemctl"), {QStringLiteral("reboot")}); });
    menu->addAction(tr("Shut down"), this,
                    [run] { run(QStringLiteral("systemctl"), {QStringLiteral("poweroff")}); });
    const QSize size = menu->sizeHint();
    menu->popup(anchor->mapToGlobal(QPoint(anchor->width() - size.width(), -size.height() - 6)));
}

QRect StartMenu::cardRect() const
{
    return rect().marginsRemoved(Ui::largeShadowMargins().toMargins());
}

void StartMenu::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = cardRect();
    paintCard(painter, card, CardRadius);
    // Footer strip: --k-pane under a 1px edge, inside the card's corners.
    QPainterPath clip;
    clip.addRoundedRect(card, CardRadius, CardRadius);
    painter.save();
    painter.setClipPath(clip);
    const QRectF footer(card.left(), card.bottom() - FooterHeight, card.width(), FooterHeight);
    painter.fillRect(footer, t.pane);
    painter.fillRect(QRectF(footer.left(), footer.top(), footer.width(), 1), t.edge);
    painter.restore();
}

void StartMenu::mousePressEvent(QMouseEvent *event)
{
    // The shadow belongs to the surface but reads as outside.
    if (!cardRect().contains(event->position().toPoint())) {
        close();
        return;
    }
    QWidget::mousePressEvent(event);
}

void StartMenu::keyPressEvent(QKeyEvent *event)
{
    // Typing anywhere goes to the search field.
    if (!event->text().isEmpty() && event->text().at(0).isPrint()) {
        QLineEdit *edit = m_search->lineEdit();
        edit->setFocus();
        edit->insert(event->text());
        return;
    }
    Flyout::keyPressEvent(event);
}

bool StartMenu::eventFilter(QObject *watched, QEvent *event)
{
    // A click in the field opens search before typing: the recent apps.
    if (m_search && watched == m_search->lineEdit() && event->type() == QEvent::MouseButtonPress
        && m_page != Page::Search && m_search->lineEdit()->text().trimmed().isEmpty()) {
        showPage(Page::Search);
        runSearch();
    }
    if (m_search && watched == m_search->lineEdit() && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Escape:
            if (!m_search->lineEdit()->text().isEmpty())
                m_search->lineEdit()->clear();
            else if (m_page == Page::Search && m_browsePage != Page::Search)
                showPage(m_browsePage);
            else
                close();
            return true;
        case Qt::Key_Down:
        case Qt::Key_Up:
            if (m_page == Page::Search)
                m_results->moveCurrent(key->key() == Qt::Key_Down ? 1 : -1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_page == Page::Search && m_results->current())
                activate(*m_results->current());
            return true;
        default:
            break;
        }
    }
    return Flyout::eventFilter(watched, event);
}
