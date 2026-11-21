// SPDX-License-Identifier: GPL-3.0-or-later
#include "pages.h"

#include "filemanager.h"
#include "widgets.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <functional>

namespace Ui = Karyo::Ui;

namespace
{

class Text
{
    Q_DECLARE_TR_FUNCTIONS(AppCenter)
};

QString terminalProgram()
{
    const QString own = QStandardPaths::findExecutable(QStringLiteral("karyo-terminal"));
    return own.isEmpty() ? QStringLiteral("x-terminal-emulator") : own;
}

void terminal(const QString &commandLine)
{
    QProcess::startDetached(terminalProgram(),
        { QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-c"),
            commandLine + QStringLiteral("; exec bash") },
        QDir::homePath());
}

// A rounded card painted in a color, holding child widgets. The color comes
// from the current theme at every paint, so the card follows it live.
class Card : public QWidget
{
public:
    Card(std::function<QColor()> fill, qreal radius, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_fill(std::move(fill))
        , m_radius(radius)
    {
    }
    void setDecorated(bool decorated) { m_decorated = decorated; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), m_radius, m_radius);
        p.setClipPath(clip);
        p.fillRect(rect(), m_fill());
        if (m_decorated)
        {
            // Theme-colored decorative circles.
            const qreal h = height();
            p.setPen(Qt::NoPen);
            p.setBrush(Ui::alpha(Ui::sage(300), t.scheme == Ui::ColorScheme::Light ? 70 : 30));
            p.drawEllipse(QRectF(width() - h * 1.05, -h * 0.4, h * 1.6, h * 1.6));
            p.setBrush(t.accent);
            p.drawEllipse(QRectF(width() - h * 0.88, h * 0.4, h * 0.44, h * 0.44));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Ui::alpha(Ui::accent(200), 85), h * 0.08));
            p.drawEllipse(QRectF(width() - h * 0.66, h * 0.66, h * 0.5, h * 0.5));
        }
    }

private:
    std::function<QColor()> m_fill;
    qreal m_radius;
    bool m_decorated = false;
};

// Accent text with a chevron: "See all ›".
class TextLink : public QAbstractButton
{
public:
    TextLink(const QString &text, QWidget *parent)
        : QAbstractButton(parent)
    {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(QFontMetrics(Ui::bodyFont(13.5, QFont::DemiBold)).horizontalAdvance(text) + 24, 28);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setFont(Ui::bodyFont(13.5, QFont::DemiBold));
        p.setPen(t.ink1);
        p.drawText(rect().adjusted(0, 0, -18, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
        p.drawPixmap(width() - 16, (height() - 14) / 2,
            Ui::icon(QStringLiteral("chevron-right"), 14, t.ink1, devicePixelRatioF()));
    }
};

// A list row: mono name, a line of text and an action at the right.
class Row : public QWidget
{
public:
    Row(const QString &name, const QString &detail, QWidget *parent)
        : QWidget(parent)
        , m_name(name)
        , m_detail(detail)
        , m_right(new QHBoxLayout)
    {
        setFixedHeight(58);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 16, 0);
        h->addStretch(1);
        h->addLayout(m_right);
    }
    QHBoxLayout *right() const { return m_right; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, rect(), 8, t.card);
        const int w = width() - 220;
        p.setFont(Ui::monoFont(13.5, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(QRect(18, 9, w, 20), Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(p.font()).elidedText(m_name, Qt::ElideRight, w));
        p.setFont(Ui::bodyFont(12.5));
        p.setPen(t.muted);
        p.drawText(QRect(18, 30, w, 18), Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(p.font()).elidedText(m_detail, Qt::ElideRight, w));
    }

private:
    QString m_name;
    QString m_detail;
    QHBoxLayout *m_right;
};

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0))
    {
        delete item->widget();
        if (item->layout())
            clearLayout(item->layout());
        delete item;
    }
}

QHBoxLayout *sectionHeader(
    const QString &title, QWidget *parent, TextLink **link = nullptr, const QString &linkText = { })
{
    auto *h = new QHBoxLayout;
    h->addWidget(pageTitle(title, parent, 22));
    h->addStretch(1);
    if (link)
    {
        *link = new TextLink(linkText, parent);
        h->addWidget(*link);
    }
    return h;
}

// Install/Open/Remove for an app in a details page or a row; follows jobs.
Pill *actionFor(PackageManager *packages, const CatalogApp &app, QWidget *parent)
{
    auto *pill = new Pill(Text::tr("Install"), true, parent, QStringLiteral("download"));
    auto sync = [pill, packages, app]
    {
        pill->setEnabled(!packages->isBusy(app));
        if (packages->isBusy(app))
        {
            pill->setProgress(packages->progress(app));
            pill->setFixedWidth(110);
            return;
        }
        pill->setProgress(-1);
        const bool installed = packages->isInstalled(app);
        pill->setText(installed ? Text::tr("Open") : Text::tr("Install"));
        pill->setPrimary(!installed);
        pill->setEnabled(installed || packages->isAvailable(app));
    };
    QObject::connect(pill, &QAbstractButton::clicked, pill,
        [packages, app]
        {
            if (packages->isInstalled(app))
                packages->open(app);
            else
                packages->install(app);
        });
    QObject::connect(packages, &PackageManager::changed, pill, sync);
    QObject::connect(packages, &PackageManager::jobChanged, pill,
        [sync, app](const CatalogApp &changed)
        {
            if (changed.package == app.package && changed.source == app.source)
                sync();
        });
    sync();
    return pill;
}

// ---------------------------------------------------------------- Discover

class DiscoverPage : public Page
{
public:
    explicit DiscoverPage(PackageManager *packages)
        : m_packages(packages)
    {
        const Ui::Theme &t = Ui::Theme::current();
        auto *top = new QHBoxLayout;
        top->setSpacing(18);
        m_hero = new Card(
            []
            {
                const Ui::Theme &t = Ui::Theme::current();
                return Ui::mix(Ui::sage(200), t.win, t.scheme == Ui::ColorScheme::Light ? 55 : 22);
            },
            28, this);
        m_hero->setDecorated(true);
        m_hero->setFixedHeight(272);
        m_heroLayout = new QVBoxLayout(m_hero);
        m_heroLayout->setContentsMargins(38, 34, 38, 34);
        m_heroLayout->setSpacing(10);
        top->addWidget(m_hero, 1);

        auto *updates = new Card([] { return Ui::Theme::current().card; }, 28, this);
        updates->setFixedSize(320, 272);
        auto *u = new QVBoxLayout(updates);
        u->setContentsMargins(26, 26, 26, 26);
        u->setSpacing(10);
        auto *glyph = new QLabel(updates);
        auto paintGlyph = [this, glyph]
        {
            glyph->setPixmap(
                Ui::icon(QStringLiteral("download"), 18, Ui::Theme::current().ink1, devicePixelRatioF()));
        };
        paintGlyph();
        connect(Ui::ThemeNotifier::instance(), &Ui::ThemeNotifier::changed, glyph, paintGlyph);
        glyph->setFixedSize(44, 44);
        glyph->setAlignment(Qt::AlignCenter);
        glyph->setStyleSheet(
            QStringLiteral("background:%1;border-radius:22px;").arg(t.tint1.name(QColor::HexArgb)));
        u->addWidget(glyph);
        m_updatesTitle = pageTitle(Text::tr("Checking for updates…"), updates, 22);
        m_updatesTitle->setWordWrap(true);
        u->addWidget(m_updatesTitle);
        m_updatesText = mutedText(QString(), updates, 13);
        u->addWidget(m_updatesText);
        u->addStretch(1);
        m_updateAll = new Pill(Text::tr("Update all"), true, updates);
        m_updateAll->setMinimumWidth(268);
        m_updateAll->setMaximumWidth(268);
        connect(m_updateAll, &QAbstractButton::clicked, this,
            [this] { Q_EMIT navigate(QStringLiteral("updates")); });
        u->addWidget(m_updateAll);
        top->addWidget(updates);
        m_column->addLayout(top);

        TextLink *seeAll = nullptr;
        m_column->addSpacing(8);
        m_column->addLayout(
            sectionHeader(Text::tr("Popular in Bioinformatics"), this, &seeAll, Text::tr("See all")));
        connect(seeAll, &QAbstractButton::clicked, this, [this] { Q_EMIT navigate(QStringLiteral("bio")); });
        m_bio = new CardGrid(packages, this);
        connect(m_bio, &CardGrid::detailsRequested, this, &Page::detailsRequested);
        m_column->addWidget(m_bio);

        TextLink *seeApps = nullptr;
        m_column->addSpacing(8);
        m_column->addLayout(
            sectionHeader(Text::tr("Apps for your work"), this, &seeApps, Text::tr("See all")));
        connect(
            seeApps, &QAbstractButton::clicked, this, [this] { Q_EMIT navigate(QStringLiteral("apps")); });
        m_apps = new CardGrid(packages, this);
        connect(m_apps, &CardGrid::detailsRequested, this, &Page::detailsRequested);
        m_column->addWidget(m_apps);

        connect(packages, &PackageManager::updatesChanged, this, &DiscoverPage::showUpdates);
        connect(packages, &PackageManager::changed, this, &DiscoverPage::showHero);
        showHero();
        showUpdates();
        QList<CatalogApp> bio = packages->inCategory(QStringLiteral("bio"));
        bio.removeIf([featured = packages->featured()](const CatalogApp &a) { return a.id == featured.id; });
        m_bio->setApps(bio.mid(0, 8));
        m_apps->setApps(packages->inCategory(QStringLiteral("apps")).mid(0, 4));
    }

private:
    void showHero()
    {
        const CatalogApp app = m_packages->featured();
        // Versions arrive after the first paint: the title shows them then.
        const QString key = app.id + QLatin1Char('@') + m_packages->version(app);
        if (key == m_heroId)
            return;
        m_heroId = key;
        clearLayout(m_heroLayout);
        if (app.id.isEmpty())
        {
            m_heroLayout->addWidget(pageTitle(Text::tr("Find apps and tools"), m_hero, 32));
            m_heroLayout->addWidget(mutedText(
                Text::tr("Search your system's packages or set up a project environment."), m_hero));
            m_heroLayout->addStretch(1);
            return;
        }
        auto *kicker = mutedText(Text::tr("FEATURED IN BIOINFORMATICS"), m_hero, 12);
        QFont font = kicker->font();
        font.setWeight(QFont::DemiBold);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        kicker->setFont(font);
        m_heroLayout->addWidget(kicker);
        const QString version = m_packages->version(app);
        m_heroLayout->addWidget(
            pageTitle(app.name + (version.isEmpty() ? QString() : QLatin1Char(' ') + version), m_hero, 42));
        auto *summary = new QLabel(app.summary, m_hero);
        summary->setTextFormat(Qt::PlainText);
        summary->setFont(Ui::bodyFont(15));
        summary->setWordWrap(true);
        summary->setMaximumWidth(460);
        m_heroLayout->addWidget(summary);
        m_heroLayout->addStretch(1);
        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(10);
        buttons->addWidget(actionFor(m_packages, app, m_hero));
        auto *details = new Pill(Text::tr("Details"), false, m_hero);
        connect(details, &QAbstractButton::clicked, this, [this, app] { Q_EMIT detailsRequested(app); });
        buttons->addWidget(details);
        buttons->addStretch(1);
        m_heroLayout->addLayout(buttons);
    }

    void showUpdates()
    {
        const int count = int(m_packages->updates().size());
        if (m_packages->checkingUpdates() || !m_packages->updatesKnown()
            || !m_packages->updatesError().isEmpty())
        {
            m_updatesTitle->setText(m_packages->checkingUpdates() ? Text::tr("Checking for updates…")
                    : !m_packages->updatesError().isEmpty() ? Text::tr("Could not check for updates")
                                                            : Text::tr("Updates have not been checked"));
            m_updatesText->setText(m_packages->updatesError());
            m_updateAll->hide();
            return;
        }
        m_updatesTitle->setText(
            count == 0 ? Text::tr("You're up to date") : Text::tr("%n updates ready", nullptr, count));
        m_updatesText->setText(count == 0
                ? Text::tr("System packages are current. App Center checks again when you open it.")
                : Text::tr("System packages, ready to install together."));
        m_updateAll->setVisible(count > 0);
    }

    PackageManager *m_packages;
    Card *m_hero;
    QVBoxLayout *m_heroLayout;
    QString m_heroId;
    QLabel *m_updatesTitle;
    QLabel *m_updatesText;
    Pill *m_updateAll;
    CardGrid *m_bio;
    CardGrid *m_apps;
};

class CategoryPage : public Page
{
public:
    CategoryPage(
        PackageManager *packages, const QString &category, const QString &title, const QString &description)
        : m_packages(packages)
        , m_category(category)
    {
        m_column->addWidget(pageTitle(title, this));
        m_count = mutedText(description, this);
        m_column->addWidget(m_count);
        m_grid = new CardGrid(packages, this);
        connect(m_grid, &CardGrid::detailsRequested, this, &Page::detailsRequested);
        m_column->addWidget(m_grid);
        refresh();
    }

    void refresh() override { m_grid->setApps(m_packages->inCategory(m_category)); }

private:
    PackageManager *m_packages;
    QString m_category;
    QLabel *m_count;
    CardGrid *m_grid;
};

class UpdatesPage : public Page
{
public:
    explicit UpdatesPage(PackageManager *packages)
        : m_packages(packages)
    {
        auto *header = new QHBoxLayout;
        header->addWidget(pageTitle(Text::tr("Updates"), this));
        header->addStretch(1);
        auto *check = new Pill(Text::tr("Check for updates"), false, this, QStringLiteral("refresh-cw"));
        connect(check, &QAbstractButton::clicked, packages, &PackageManager::refreshSources);
        header->addWidget(check);
        CatalogApp system;
        system.source = QStringLiteral("apt");
        system.package = QStringLiteral("*");
        m_all = new Pill(Text::tr("Update all"), true, this, QStringLiteral("download"));
        connect(m_all, &QAbstractButton::clicked, packages, &PackageManager::updateAll);
        auto sync = [this, packages, system, check]
        {
            CatalogApp lists;
            lists.source = QStringLiteral("apt");
            lists.package = QStringLiteral("lists");
            const bool busy = packages->isBusy(system) || packages->isBusy(lists);
            check->setEnabled(!busy && !packages->checkingUpdates());
            m_all->setEnabled(!busy && !packages->checkingUpdates() && packages->updatesError().isEmpty());
            m_all->setProgress(packages->isBusy(system) ? packages->progress(system) : -1);
        };
        connect(packages, &PackageManager::jobChanged, this, [sync] { sync(); });
        connect(packages, &PackageManager::updatesChanged, this, sync);
        sync();
        header->addWidget(m_all);
        m_column->addLayout(header);
        m_status = mutedText(QString(), this);
        m_column->addWidget(m_status);
        m_list = new QVBoxLayout;
        m_list->setSpacing(8);
        m_column->addLayout(m_list);
        connect(packages, &PackageManager::updatesChanged, this, &UpdatesPage::refresh);
        refresh();
    }

    void refresh() override
    {
        clearLayout(m_list);
        const QList<Upgrade> &updates = m_packages->updates();
        m_all->setVisible(!updates.isEmpty());
        if (m_packages->checkingUpdates())
        {
            m_status->setText(Text::tr("Checking for updates…"));
            return;
        }
        if (!m_packages->updatesError().isEmpty() || !m_packages->updatesKnown())
        {
            m_status->setText(m_packages->updatesError().isEmpty()
                    ? Text::tr("Updates have not been checked")
                    : Text::tr("Could not check for updates: %1").arg(m_packages->updatesError()));
            m_all->hide();
            return;
        }
        m_status->setText(updates.isEmpty()
                ? Text::tr("Everything is up to date.")
                : Text::tr("%n packages can be updated. Polkit asks for an administrator's password.",
                      nullptr, int(updates.size())));
        for (const Upgrade &u : updates)
            m_list->addWidget(new Row(u.package,
                u.from.isEmpty() ? Text::tr("New: %1").arg(u.to) : u.from + QStringLiteral("  →  ") + u.to,
                this));
    }

private:
    PackageManager *m_packages;
    Pill *m_all;
    QLabel *m_status;
    QVBoxLayout *m_list;
};

class InstalledPage : public Page
{
public:
    explicit InstalledPage(PackageManager *packages)
        : m_packages(packages)
    {
        m_column->addWidget(pageTitle(Text::tr("Installed"), this));
        m_count = mutedText(QString(), this);
        m_column->addWidget(m_count);
        m_grid = new CardGrid(packages, this);
        connect(m_grid, &CardGrid::detailsRequested, this, &Page::detailsRequested);
        m_column->addWidget(m_grid);
        connect(packages, &PackageManager::changed, this, &InstalledPage::refresh);
        refresh();
    }

    void refresh() override
    {
        QList<CatalogApp> installed;
        for (const CatalogApp &app : m_packages->catalog())
        {
            if (m_packages->isInstalled(app))
                installed.append(app);
        }
        QStringList identities;
        for (const auto &app : installed)
            identities.append(app.source + QLatin1Char(':') + app.id);
        if (identities == m_shown && m_initialized)
            return;
        m_initialized = true;
        m_shown = identities;
        m_count->setText(
            Text::tr("%n apps and tools from App Center's catalog", nullptr, int(installed.size())));
        m_grid->setApps(installed);
    }

private:
    PackageManager *m_packages;
    QLabel *m_count;
    CardGrid *m_grid;
    QStringList m_shown;
    bool m_initialized = false;
};

// ---------------------------------------------------------------- Environments

// The list of environments at the left: cube, name, versions, size.
class EnvList : public QWidget
{
public:
    explicit EnvList(QWidget *parent)
        : QWidget(parent)
    {
        setFixedWidth(330);
        setMouseTracking(true);
    }

    std::function<void(int)> onSelect;

    void setEnvs(const QList<CondaEnv> &envs, int current)
    {
        m_envs = envs;
        m_current = current;
        setFixedHeight(qMax(1, int(envs.size())) * 76);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QLocale locale;
        for (int i = 0; i < m_envs.size(); ++i)
        {
            const CondaEnv &env = m_envs.at(i);
            const QRect row(0, i * 76, width(), 70);
            if (i == m_current)
                Ui::fillRounded(p, row, 8, t.sel);
            else if (i == m_hover)
                Ui::fillRounded(p, row, 8, t.hover);
            const QRectF cube(16, row.center().y() - 21, 42, 42);
            Ui::fillPill(p, cube, i == m_current ? Ui::sage(600) : t.card);
            p.drawPixmap(QPointF(cube.x() + 11, cube.y() + 11),
                Ui::icon(QStringLiteral("box"), 20, i == m_current ? Ui::neutral(100) : t.ink2,
                    devicePixelRatioF()));
            p.setFont(Ui::bodyFont(14, QFont::DemiBold));
            p.setPen(t.text);
            p.drawText(
                QRect(72, row.y() + 14, width() - 72 - 80, 20), Qt::AlignVCenter | Qt::AlignLeft, env.name);
            p.setFont(Ui::bodyFont(12.5));
            p.setPen(t.muted);
            const QString detail = (env.python.isEmpty() ? QString() : env.python + QStringLiteral(" · "))
                + Text::tr("%n packages", nullptr, env.packages);
            p.drawText(QRect(72, row.y() + 36, width() - 72 - 80, 18), Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(p.font()).elidedText(detail, Qt::ElideRight, width() - 72 - 80));
            if (env.size >= 0)
            {
                p.drawText(QRect(width() - 90, row.y(), 76, row.height()), Qt::AlignVCenter | Qt::AlignRight,
                    locale.formattedDataSize(env.size, 1));
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int hover = int(event->position().y()) / 76;
        if (hover != m_hover)
        {
            m_hover = hover < m_envs.size() ? hover : -1;
            update();
        }
    }

    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const int row = int(event->position().y()) / 76;
        if (row >= 0 && row < m_envs.size() && onSelect)
            onSelect(row);
    }

private:
    QList<CondaEnv> m_envs;
    int m_current = 0;
    int m_hover = -1;
};

// The package table of an environment.
class PackageTable : public QWidget
{
public:
    explicit PackageTable(QWidget *parent)
        : QWidget(parent)
    {
    }

    void setPackages(const QList<CondaPackage> &packages)
    {
        m_packages = packages;
        setFixedHeight(36 + int(packages.size()) * 41);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int w = width();
        const int cVersion = int(w * 0.28);
        const int cChannel = int(w * 0.56);
        QFont head = Ui::bodyFont(11.5, QFont::DemiBold);
        head.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        p.setFont(head);
        p.setPen(t.muted);
        p.drawText(QRect(10, 0, 200, 34), Qt::AlignVCenter, Text::tr("PACKAGE"));
        p.drawText(QRect(cVersion, 0, 200, 34), Qt::AlignVCenter, Text::tr("VERSION"));
        p.drawText(QRect(cChannel, 0, 200, 34), Qt::AlignVCenter, Text::tr("CHANNEL"));
        p.drawText(QRect(w - 210, 0, 200, 34), Qt::AlignVCenter | Qt::AlignRight, Text::tr("SIZE"));
        p.fillRect(QRect(0, 35, w, 1), t.edge);
        const QLocale locale;
        for (int i = 0; i < m_packages.size(); ++i)
        {
            const CondaPackage &pkg = m_packages.at(i);
            const int y = 36 + i * 41;
            p.setFont(Ui::monoFont(13.5));
            p.setPen(t.text);
            p.drawText(QRect(10, y, cVersion - 20, 40), Qt::AlignVCenter, pkg.name);
            p.drawText(QRect(cVersion, y, cChannel - cVersion - 10, 40), Qt::AlignVCenter, pkg.version);
            const QFont tagFont = Ui::monoFont(11.5);
            const int tagW = QFontMetrics(tagFont).horizontalAdvance(pkg.channel) + 20;
            const QRectF tag(cChannel, y + 9, tagW, 22);
            const bool conda
                = pkg.channel == QLatin1String("bioconda") || pkg.channel == QLatin1String("conda-forge");
            Ui::fillPill(p, tag, conda ? t.tint2 : Ui::alpha(t.text, 8));
            p.setFont(tagFont);
            p.setPen(conda ? t.ink2 : t.muted);
            p.drawText(tag, Qt::AlignCenter, pkg.channel);
            p.setFont(Ui::bodyFont(13));
            p.setPen(t.muted);
            p.drawText(QRect(w - 210, y, 200, 40), Qt::AlignVCenter | Qt::AlignRight,
                pkg.size > 0 ? locale.formattedDataSize(pkg.size, 1) : QString());
            p.fillRect(QRect(0, y + 40, w, 1), t.edge);
        }
    }

private:
    QList<CondaPackage> m_packages;
};

class EnvironmentsPage : public Page
{
public:
    explicit EnvironmentsPage(PackageManager *packages)
        : m_packages(packages)
    {
        auto *header = new QHBoxLayout;
        auto *titles = new QVBoxLayout;
        titles->setSpacing(2);
        titles->addWidget(pageTitle(Text::tr("Environments"), this));
        m_summary = mutedText(QString(), this);
        titles->addWidget(m_summary);
        header->addLayout(titles, 1);
        auto *import = new Pill(Text::tr("Import environment.yml"), false, this, QStringLiteral("upload"));
        connect(import, &QAbstractButton::clicked, this,
            [this]
            {
                const QString file = QFileDialog::getOpenFileName(this, Text::tr("Import an environment"),
                    QDir::homePath(), Text::tr("Environment files (*.yml *.yaml)"));
                if (!file.isEmpty())
                    m_packages->importEnvironment(file);
            });
        header->addWidget(import, 0, Qt::AlignTop);
        auto *create = new Pill(Text::tr("New environment"), true, this, QStringLiteral("plus"));
        connect(create, &QAbstractButton::clicked, this,
            [this]
            {
                bool ok = false;
                const QString name = QInputDialog::getText(this, Text::tr("New environment"),
                    Text::tr("Name (letters, digits, - and _)"), QLineEdit::Normal, QString(), &ok)
                                         .trimmed();
                if (ok && !name.isEmpty())
                    m_packages->createEnvironment(name);
            });
        header->addWidget(create, 0, Qt::AlignTop);
        m_column->addLayout(header);
        m_import = import;
        m_create = create;

        auto *body = new QHBoxLayout;
        body->setSpacing(18);
        m_list = new EnvList(this);
        m_list->onSelect = [this](int row)
        {
            m_current = row;
            showCurrent();
        };
        body->addWidget(m_list, 0, Qt::AlignTop);
        m_detail = new Card([] { return Ui::Theme::current().card; }, 28, this);
        m_detailLayout = new QVBoxLayout(m_detail);
        m_detailLayout->setContentsMargins(28, 26, 28, 26);
        m_detailLayout->setSpacing(12);
        body->addWidget(m_detail, 1, Qt::AlignTop);
        m_column->addLayout(body);
        connect(packages, &PackageManager::envSizeKnown, this, [this] { showList(); });
    }

    void refresh() override
    {
        m_envs = m_packages->condaEnvs();
        const bool conda = !m_packages->condaRoot().isEmpty();
        m_import->setVisible(conda);
        m_create->setVisible(conda);
        if (!conda)
        {
            showSetup();
            return;
        }
        const QString tool = QFileInfo(m_packages->condaTool()).fileName();
        m_summary->setText(Text::tr("%n conda environments", nullptr, int(m_envs.size()))
            + QStringLiteral(" · ") + QFileInfo(m_packages->condaRoot()).fileName() + QStringLiteral(" · ")
            + tool);
        m_current = m_envs.isEmpty() ? -1 : qBound(0, m_current, int(m_envs.size()) - 1);
        for (const CondaEnv &env : std::as_const(m_envs))
        {
            if (env.size < 0)
                m_packages->measureEnv(env.path);
        }
        showList();
        showCurrent();
    }

private:
    void showList()
    {
        m_envs = m_packages->condaEnvs();
        m_list->setEnvs(m_envs, m_current);
    }

    void showSetup()
    {
        m_list->hide();
        m_summary->setText(Text::tr("conda is not set up"));
        clearLayout(m_detailLayout);
        m_detailLayout->addWidget(pageTitle(Text::tr("Set up conda environments"), m_detail, 26));
        m_detailLayout->addWidget(mutedText(
            Text::tr(
                "Environments keep each project's tools and versions apart. App Center installs Miniforge in "
                "your home folder (~/miniforge3) with the bioconda and conda-forge channels; no "
                "administrator "
                "password is needed. The download runs in Terminal so you can follow it."),
            m_detail, 13.5));
        auto *setup = new Pill(Text::tr("Set up Miniforge"), true, m_detail, QStringLiteral("download"));
        connect(setup, &QAbstractButton::clicked, this,
            [this] { terminal(m_packages->installMiniforgeCommand()); });
        auto *row = new QHBoxLayout;
        row->addWidget(setup);
        row->addStretch(1);
        m_detailLayout->addLayout(row);
    }

    void showCurrent()
    {
        m_list->show();
        m_list->setEnvs(m_envs, m_current);
        clearLayout(m_detailLayout);
        if (m_current < 0 || m_current >= m_envs.size())
            return;
        const CondaEnv env = m_envs.at(m_current);
        auto *head = new QHBoxLayout;
        auto *names = new QVBoxLayout;
        names->setSpacing(2);
        names->addWidget(pageTitle(env.name, m_detail, 28));
        QString path = env.path;
        if (path.startsWith(QDir::homePath()))
            path.replace(0, QDir::homePath().size(), QStringLiteral("~"));
        auto *pathLabel = mutedText(path, m_detail, 12.5);
        pathLabel->setFont(Ui::monoFont(12.5));
        names->addWidget(pathLabel);
        head->addLayout(names, 1);
        auto *open
            = new Pill(Text::tr("Open in Terminal"), true, m_detail, QStringLiteral("square-terminal"));
        connect(
            open, &QAbstractButton::clicked, this, [this, env] { m_packages->openEnvTerminal(env.path); });
        head->addWidget(open, 0, Qt::AlignTop);
        auto *exportButton = new Pill(Text::tr("Export"), false, m_detail, QStringLiteral("file-down"));
        connect(exportButton, &QAbstractButton::clicked, this,
            [this, env]
            {
                const QString file = QFileDialog::getSaveFileName(this, Text::tr("Export environment"),
                    QDir::homePath() + QLatin1Char('/') + env.name + QStringLiteral("-environment.yml"),
                    Text::tr("Environment files (*.yml)"));
                if (file.isEmpty())
                    return;
                m_packages->exportEnvironment(env.path, file);
            });
        head->addWidget(exportButton, 0, Qt::AlignTop);
        auto *more = new Pill(QString(), false, m_detail, QStringLiteral("ellipsis"));
        more->setFixedWidth(40);
        connect(more, &QAbstractButton::clicked, this,
            [this, env, more]
            {
                auto *menu = new QMenu(this);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                Ui::styleMenu(menu);
                menu->addAction(Text::tr("Show in Files"), this,
                    [this, env] { Karyo::Apps::openFolder(env.path, this); });
                if (env.name != QLatin1String("base"))
                {
                    menu->addAction(Text::tr("Remove environment…"), this,
                        [this, env]
                        {
                            QMessageBox confirm(QMessageBox::Question, Text::tr("Remove %1?").arg(env.name),
                                Text::tr("The environment and its packages are deleted from %1.")
                                    .arg(env.path),
                                QMessageBox::Yes | QMessageBox::No, this);
                            confirm.setTextFormat(Qt::PlainText);
                            confirm.setDefaultButton(QMessageBox::No);
                            if (confirm.exec() == QMessageBox::Yes)
                                m_packages->removeEnvironment(env.path);
                        });
                }
                menu->popup(more->mapToGlobal(QPoint(0, more->height() + 4)));
            });
        head->addWidget(more, 0, Qt::AlignTop);
        m_detailLayout->addLayout(head);

        const QList<CondaPackage> all = m_packages->condaPackages(env.path);
        QList<CondaPackage> requested;
        for (const CondaPackage &p : all)
        {
            if (p.requested)
                requested.append(p);
        }
        auto *facts = new QHBoxLayout;
        facts->setSpacing(8);
        QStringList factTexts;
        if (!env.python.isEmpty())
            factTexts.append(env.python);
        factTexts.append(Text::tr("%n packages", nullptr, int(all.size())));
        if (env.size >= 0)
            factTexts.append(QLocale().formattedDataSize(env.size, 1));
        const QFileInfo history(env.path + QStringLiteral("/conda-meta/history"));
        if (history.exists())
            factTexts.append(Text::tr("Updated %1")
                    .arg(QLocale().toString(history.lastModified().date(), QLocale::ShortFormat)));
        for (const QString &fact : factTexts)
        {
            auto *tag = new QLabel(fact, m_detail);
            tag->setTextFormat(Qt::PlainText);
            tag->setFont(Ui::bodyFont(12, QFont::DemiBold));
            tag->setContentsMargins(12, 5, 12, 5);
            tag->setStyleSheet(QStringLiteral("background:%1;border-radius:13px;")
                    .arg(Ui::Theme::current().win.name(QColor::HexArgb)));
            facts->addWidget(tag);
        }
        facts->addStretch(1);
        m_detailLayout->addLayout(facts);

        auto *which = new QHBoxLayout;
        auto *explicitButton
            = new Pill(Text::tr("Explicitly installed · %1").arg(requested.size()), !m_showAll, m_detail);
        auto *allButton = new Pill(Text::tr("All · %1").arg(all.size()), m_showAll, m_detail);
        connect(explicitButton, &QAbstractButton::clicked, this,
            [this]
            {
                m_showAll = false;
                showCurrent();
            });
        connect(allButton, &QAbstractButton::clicked, this,
            [this]
            {
                m_showAll = true;
                showCurrent();
            });
        which->addWidget(explicitButton);
        which->addWidget(allButton);
        which->addStretch(1);
        m_detailLayout->addLayout(which);
        auto *table = new PackageTable(m_detail);
        table->setPackages(m_showAll || requested.isEmpty() ? all : requested);
        m_detailLayout->addWidget(table);
    }

    PackageManager *m_packages;
    QLabel *m_summary;
    Pill *m_import;
    Pill *m_create;
    EnvList *m_list;
    Card *m_detail;
    QVBoxLayout *m_detailLayout;
    QList<CondaEnv> m_envs;
    int m_current = 0;
    bool m_showAll = false;
};

} // namespace

Page::Page(QWidget *parent)
    : QWidget(parent)
    , m_column(new QVBoxLayout(this))
{
    m_column->setContentsMargins(30, 26, 30, 30);
    m_column->setSpacing(16);
}

Page *createDiscoverPage(PackageManager *packages)
{
    auto *page = new DiscoverPage(packages);
    page->column()->addStretch(1);
    return page;
}

Page *createCategoryPage(
    PackageManager *packages, const QString &category, const QString &title, const QString &description)
{
    auto *page = new CategoryPage(packages, category, title, description);
    page->column()->addStretch(1);
    return page;
}

Page *createUpdatesPage(PackageManager *packages)
{
    auto *page = new UpdatesPage(packages);
    page->column()->addStretch(1);
    return page;
}

Page *createInstalledPage(PackageManager *packages)
{
    auto *page = new InstalledPage(packages);
    page->column()->addStretch(1);
    return page;
}

Page *createEnvironmentsPage(PackageManager *packages)
{
    auto *page = new EnvironmentsPage(packages);
    page->column()->addStretch(1);
    return page;
}

// ---------------------------------------------------------------- SearchPage

SearchPage::SearchPage(PackageManager *packages, QWidget *parent)
    : Page(parent)
    , m_packages(packages)
    , m_title(pageTitle(QString(), this))
    , m_catalog(new CardGrid(packages, this))
    , m_debianHeading(pageTitle(Text::tr("System packages"), this, 22))
    , m_debian(new QVBoxLayout)
    , m_flatpakHeading(pageTitle(QStringLiteral("Flathub"), this, 22))
    , m_flatpak(new CardGrid(packages, this))
{
    m_column->addWidget(m_title);
    m_column->addWidget(m_catalog);
    m_column->addWidget(m_debianHeading);
    m_debian->setSpacing(8);
    m_column->addLayout(m_debian);
    m_column->addWidget(m_flatpakHeading);
    m_column->addWidget(m_flatpak);
    m_column->addStretch(1);
    connect(m_catalog, &CardGrid::detailsRequested, this, &Page::detailsRequested);
    connect(m_flatpak, &CardGrid::detailsRequested, this, &Page::detailsRequested);
    connect(packages, &PackageManager::aptResults, this,
        [this](const QString &query, const QList<CatalogApp> &apps)
        {
            if (query != m_query)
                return;
            // Catalog entries already show as cards.
            QList<CatalogApp> rest;
            for (const CatalogApp &app : apps)
            {
                bool known = false;
                for (const CatalogApp &c : m_packages->catalog())
                    known = known || (c.source == QLatin1String("apt") && c.package == app.package);
                if (!known)
                    rest.append(app);
            }
            m_debianHeading->setVisible(!rest.isEmpty());
            showRows(m_debian, rest);
        });
    connect(packages, &PackageManager::flatpakResults, this,
        [this](const QString &query, const QList<CatalogApp> &apps)
        {
            if (query != m_query)
                return;
            m_flatpakHeading->setVisible(!apps.isEmpty());
            m_flatpak->setVisible(!apps.isEmpty());
            m_flatpak->setApps(apps);
        });
}

void SearchPage::search(const QString &query)
{
    m_query = query;
    m_title->setText(Text::tr("Results for “%1”").arg(query));
    const QList<CatalogApp> catalog = m_packages->searchCatalog(query);
    m_catalog->setApps(catalog);
    m_catalog->setVisible(!catalog.isEmpty());
    m_debianHeading->hide();
    clearLayout(m_debian);
    m_flatpakHeading->hide();
    m_flatpak->hide();
    m_packages->searchApt(query);
    m_packages->searchFlatpak(query);
}

void SearchPage::showRows(QVBoxLayout *layout, const QList<CatalogApp> &apps)
{
    clearLayout(layout);
    for (const CatalogApp &app : apps)
    {
        auto *row = new Row(app.package, app.summary, this);
        row->right()->addWidget(actionFor(m_packages, app, row));
        layout->addWidget(row);
    }
}

// ---------------------------------------------------------------- DetailsPage

DetailsPage::DetailsPage(PackageManager *packages, const CatalogApp &app, QWidget *parent)
    : Page(parent)
{
    auto *head = new QHBoxLayout;
    head->setSpacing(22);
    auto *tile = new QLabel(this);
    tile->setFixedSize(96, 96);
    QPixmap pixmap(QSize(96, 96) * devicePixelRatioF());
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    pixmap.fill(Qt::transparent);
    {
        QPainter p(&pixmap);
        paintAppTile(p, QRectF(0, 0, 96, 96), app);
    }
    tile->setPixmap(pixmap);
    head->addWidget(tile, 0, Qt::AlignTop);
    auto *names = new QVBoxLayout;
    names->setSpacing(6);
    names->addWidget(pageTitle(app.name, this, 34));
    const QString version = packages->version(app);
    auto *meta = new QHBoxLayout;
    meta->setSpacing(10);
    if (!version.isEmpty())
        meta->addWidget(mutedText(version, this, 13.5));
    meta->addWidget(new SourceTag(app.source, this));
    meta->addStretch(1);
    names->addLayout(meta);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addWidget(actionFor(packages, app, this));
    auto *remove = new Pill(Text::tr("Remove"), false, this, QStringLiteral("trash-2"));
    connect(remove, &QAbstractButton::clicked, packages, [packages, app] { packages->remove(app); });
    const auto syncRemove = [packages, app, remove]
    {
        remove->setVisible(packages->isInstalled(app));
        remove->setEnabled(!packages->isBusy(app));
    };
    connect(packages, &PackageManager::changed, remove, syncRemove);
    connect(packages, &PackageManager::jobChanged, remove, [syncRemove] { syncRemove(); });
    syncRemove();
    buttons->addWidget(remove);
    buttons->addStretch(1);
    names->addSpacing(6);
    names->addLayout(buttons);
    head->addLayout(names, 1);
    m_column->addLayout(head);

    auto *summary = new QLabel(app.summary, this);
    summary->setTextFormat(Qt::PlainText);
    summary->setFont(Ui::bodyFont(15));
    summary->setWordWrap(true);
    m_column->addWidget(summary);

    QStringList facts;
    facts << Text::tr("Package: %1").arg(app.package);
    if (app.source == QLatin1String("apt"))
        facts << Text::tr("From your system repositories, installed for everyone on this computer");
    else if (app.source == QLatin1String("flatpak"))
        facts << Text::tr("From Flathub, installed for you");
    else
        facts << Text::tr("From %1, installed into a conda environment").arg(app.source);
    if (app.kind == QLatin1String("cli"))
        facts << Text::tr("Runs in Terminal as %1").arg(app.command.isEmpty() ? app.package : app.command);
    m_column->addWidget(mutedText(facts.join(QLatin1Char('\n')), this, 13.5));
    m_column->addStretch(1);
}
