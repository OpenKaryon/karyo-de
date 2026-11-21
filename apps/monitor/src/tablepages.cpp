// SPDX-License-Identifier: GPL-3.0-or-later
#include "tablepages.h"

#include "processmodel.h"
#include "sampler.h"

#include <karyo/ui/desktopentry.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProcess>
#include <QMessageBox>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <memory>
#include <pwd.h>

namespace Ui = Karyo::Ui;

namespace {

constexpr int SortRole = Qt::UserRole + 10;

QString userName(uid_t uid)
{
    static QHash<uid_t, QString> cache;
    if (auto it = cache.constFind(uid); it != cache.constEnd())
        return *it;
    const passwd *pw = getpwuid(uid);
    const QString name = pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(uid);
    cache.insert(uid, name);
    return name;
}

QString stateText(char state)
{
    switch (state) {
    case 'R': return QObject::tr("Running");
    case 'S': return QObject::tr("Sleeping");
    case 'D': return QObject::tr("Waiting");
    case 'Z': return QObject::tr("Zombie");
    case 'T':
    case 't': return QObject::tr("Stopped");
    case 'I': return QObject::tr("Idle");
    default: return QString(QChar::fromLatin1(state));
    }
}

} // namespace

TablePage::TablePage(const QString &title, const QStringList &columns, QWidget *parent)
    : QWidget(parent)
    , m_actions(new QHBoxLayout)
    , m_model(new QStandardItemModel(0, int(columns.size()), this))
    , m_proxy(new QSortFilterProxyModel(this))
    , m_view(new QTreeView)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 16, 12);
    layout->setSpacing(10);
    auto *top = new QHBoxLayout;
    auto *heading = new QLabel(title);
    heading->setFont(Ui::headingFont(26));
    top->addWidget(heading);
    top->addStretch();
    m_actions->setSpacing(4);
    top->addLayout(m_actions);
    layout->addLayout(top);

    m_model->setHorizontalHeaderLabels(columns);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(SortRole);
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_view->setModel(m_proxy);
    m_view->setRootIsDecorated(false);
    m_view->setSortingEnabled(true);
    m_view->setUniformRowHeights(true);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->header()->setStretchLastSection(true);
    m_view->setAttribute(Qt::WA_TranslucentBackground);
    QPalette palette = m_view->palette();
    palette.setColor(QPalette::Base, Qt::transparent);
    m_view->setPalette(palette);
    layout->addWidget(m_view, 1);
}

void TablePage::setFilter(const QString &text) { m_proxy->setFilterFixedString(text); }

void TablePage::addAction(QWidget *button) { m_actions->addWidget(button); }

QString TablePage::selectedValue(int column) const
{
    const QModelIndex current = m_view->currentIndex();
    return current.isValid() ? current.sibling(current.row(), column).data().toString() : QString();
}

void TablePage::setRows(const QList<QStringList> &rows, const QList<QVariantList> &sortValues)
{
    // Keep the selection and scroll position across refreshes.
    const QString selected = selectedValue(0) + QLatin1Char('\x1f') + selectedValue(1);
    const int scroll = m_view->verticalScrollBar()->value();
    m_model->setRowCount(0);
    for (qsizetype r = 0; r < rows.size(); ++r) {
        QList<QStandardItem *> items;
        for (qsizetype c = 0; c < rows.at(r).size(); ++c) {
            auto *item = new QStandardItem(rows.at(r).at(c));
            item->setEditable(false);
            const QVariant sort = r < sortValues.size() && c < sortValues.at(r).size() && sortValues.at(r).at(c).isValid()
                                      ? sortValues.at(r).at(c)
                                      : QVariant(rows.at(r).at(c).toLower());
            item->setData(sort, SortRole);
            if (sort.typeId() != QMetaType::QString)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            items.append(item);
        }
        m_model->appendRow(items);
    }
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        const QModelIndex index = m_proxy->index(row, 0);
        if (index.data().toString() + QLatin1Char('\x1f') + index.sibling(row, 1).data().toString() == selected) {
            m_view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            break;
        }
    }
    m_view->verticalScrollBar()->setValue(scroll);
}

DetailsPage::DetailsPage(Sampler *sampler, QWidget *parent)
    : TablePage(tr("Details"),
                {tr("Name"), tr("PID"), tr("Status"), tr("User"), tr("CPU"), tr("Memory"), tr("Threads"), tr("Command line")},
                parent)
    , m_sampler(sampler)
{
    view()->sortByColumn(5, Qt::DescendingOrder);
    view()->header()->resizeSection(0, 200);
    connect(sampler, &Sampler::updated, this, &DetailsPage::refresh);
    refresh();
}

void DetailsPage::refresh()
{
    if (!isVisible() && view()->model()->rowCount() > 0)
        return;
    QList<QStringList> rows;
    QList<QVariantList> sort;
    for (const ProcessInfo &p : m_sampler->processes()) {
        if (p.pid == 2 || p.ppid == 2)
            continue;
        rows.append({p.name, QString::number(p.pid), stateText(p.state), userName(p.uid),
                     QStringLiteral("%1%").arg(p.cpuPercent, 0, 'f', 1), formatBytes(double(p.rssBytes)),
                     QString::number(p.threads), p.command});
        sort.append({QVariant(), int(p.pid), QVariant(), QVariant(), p.cpuPercent, double(p.rssBytes), p.threads, QVariant()});
    }
    setRows(rows, sort);
}

UsersPage::UsersPage(Sampler *sampler, QWidget *parent)
    : TablePage(tr("Users"), {tr("User"), tr("Processes"), tr("CPU"), tr("Memory")}, parent)
    , m_sampler(sampler)
{
    connect(sampler, &Sampler::updated, this, &UsersPage::refresh);
    refresh();
}

void UsersPage::refresh()
{
    struct Totals { int count = 0; double cpu = 0; qint64 memory = 0; };
    QMap<uid_t, Totals> byUser;
    for (const ProcessInfo &p : m_sampler->processes()) {
        if (p.pid == 2 || p.ppid == 2)
            continue;
        Totals &t = byUser[p.uid];
        ++t.count;
        t.cpu += p.cpuPercent;
        t.memory += p.rssBytes;
    }
    QList<QStringList> rows;
    QList<QVariantList> sort;
    for (auto it = byUser.cbegin(); it != byUser.cend(); ++it) {
        rows.append({userName(it.key()), QString::number(it->count), QStringLiteral("%1%").arg(it->cpu, 0, 'f', 1),
                     formatBytes(double(it->memory))});
        sort.append({QVariant(), it->count, it->cpu, double(it->memory)});
    }
    setRows(rows, sort);
}

ServicesPage::ServicesPage(QWidget *parent)
    : TablePage(tr("Services"), {tr("Name"), tr("Scope"), tr("Status"), tr("Description")}, parent)
{
    for (const auto &[glyph, label, verb] : {std::tuple{"play", QT_TR_NOOP("Start"), "start"},
                                             std::tuple{"square", QT_TR_NOOP("Stop"), "stop"},
                                             std::tuple{"rotate-cw", QT_TR_NOOP("Restart"), "restart"}}) {
        auto *button = new Ui::PillButton(QString::fromLatin1(glyph), tr(label));
        const QString v = QString::fromLatin1(verb);
        connect(button, &QAbstractButton::clicked, this, [this, v] { control(v); });
        addAction(button);
    }
    view()->header()->resizeSection(0, 260);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (isVisible())
            refresh();
    });
    timer->start(5000);
}

void ServicesPage::showEvent(QShowEvent *event)
{
    TablePage::showEvent(event);
    refresh();
}

void ServicesPage::refresh()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    auto rows = std::make_shared<QList<QStringList>>();
    auto pending = std::make_shared<int>(2);
    for (const bool user : {false, true}) {
        auto *process = new QProcess(this);
        auto done = std::make_shared<bool>(false);
        auto finish = [this, process, rows, pending, done, user] {
            if (*done)
                return;
            *done = true;
            const auto lines = QString::fromLocal8Bit(process->readAllStandardOutput())
                                   .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const auto &line : lines) {
                const auto fields = line.simplified().split(QLatin1Char(' '));
                if (fields.size() < 4 || !fields.at(0).endsWith(QLatin1String(".service")))
                    continue;
                QString name = fields.at(0);
                name.chop(8);
                const QString status = fields.at(2) == QLatin1String("active")
                    ? tr("Running (%1)").arg(fields.at(3))
                    : fields.at(2) == QLatin1String("failed") ? tr("Failed") : tr("Stopped");
                rows->append({name, user ? tr("User") : tr("System"), status,
                              fields.mid(4).join(QLatin1Char(' '))});
            }
            process->deleteLater();
            if (--*pending == 0) {
                m_refreshing = false;
                setRows(*rows);
            }
        };
        connect(process, &QProcess::finished, this, finish);
        connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                finish();
        });
        QTimer::singleShot(3000, process, [process] {
            if (process->state() != QProcess::NotRunning)
                process->kill();
        });
        QStringList args{QStringLiteral("list-units"), QStringLiteral("--type=service"),
                         QStringLiteral("--all"), QStringLiteral("--no-legend"),
                         QStringLiteral("--plain"), QStringLiteral("--no-pager")};
        if (user)
            args.prepend(QStringLiteral("--user"));
        process->start(QStringLiteral("systemctl"), args);
    }
}

void ServicesPage::control(const QString &verb)
{
    const QString name = selectedValue(0);
    if (name.isEmpty())
        return;
    QStringList args = {verb, QStringLiteral("--"), name + QStringLiteral(".service")};
    // System units ask for authorization through the session's polkit agent.
    if (selectedValue(1) == tr("User"))
        args.prepend(QStringLiteral("--user"));
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (code != 0 || status != QProcess::NormalExit)
            QMessageBox::warning(this, tr("Services"), QString::fromLocal8Bit(process->readAllStandardError()));
        process->deleteLater();
        refresh();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            QMessageBox::warning(this, tr("Services"), process->errorString());
            process->deleteLater();
        }
    });
    process->start(QStringLiteral("systemctl"), args);
}

StartupPage::StartupPage(QWidget *parent)
    : TablePage(tr("Startup apps"), {tr("Name"), tr("Status"), tr("Description")}, parent)
{
    auto *enable = new Ui::PillButton(QStringLiteral("circle-check"), tr("Enable"));
    auto *disable = new Ui::PillButton(QStringLiteral("circle-x"), tr("Disable"));
    connect(enable, &QAbstractButton::clicked, this, [this] { setEnabled(true); });
    connect(disable, &QAbstractButton::clicked, this, [this] { setEnabled(false); });
    addAction(enable);
    addAction(disable);
    view()->header()->resizeSection(0, 240);
    refresh();
}

void StartupPage::refresh()
{
    // XDG autostart: the user's directory overrides system entries of the same name.
    QMap<QString, QString> files;
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
    std::reverse(dirs.begin(), dirs.end());
    for (const QString &dir : std::as_const(dirs)) {
        const QDir autostart(dir + QStringLiteral("/autostart"));
        for (const QString &name : autostart.entryList({QStringLiteral("*.desktop")}, QDir::Files))
            files.insert(name, autostart.filePath(name));
    }
    QList<QStringList> rows;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        QSettings entry(it.value(), QSettings::IniFormat);
        entry.beginGroup(QStringLiteral("Desktop Entry"));
        const bool hidden = entry.value(QStringLiteral("Hidden")).toString() == QLatin1String("true")
                            || entry.value(QStringLiteral("X-GNOME-Autostart-enabled")).toString() == QLatin1String("false");
        const auto parsed = DesktopEntry::load(it.value(), it.key());
        const QString name = parsed ? parsed->name : entry.value(QStringLiteral("Name")).toString();
        if (name.isEmpty())
            continue;
        rows.append({name, hidden ? tr("Disabled") : tr("Enabled"),
                     parsed ? parsed->comment : QString(), it.key()});
    }
    setRows(rows);
    view()->setColumnHidden(3, true);
}

void StartupPage::setEnabled(bool enabled)
{
    const QModelIndex current = view()->currentIndex();
    if (!current.isValid())
        return;
    const QString file = current.sibling(current.row(), 3).data().toString();
    if (file.isEmpty())
        return;
    // Write the user's override; system entries stay untouched.
    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart");
    QDir().mkpath(userDir);
    const QString target = userDir + QLatin1Char('/') + file;
    QString source = target;
    if (!QFile::exists(source)) {
        for (const auto &dir : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation)) {
            const QString candidate = dir + QStringLiteral("/autostart/") + file;
            if (QFile::exists(candidate)) {
                source = candidate;
                break;
            }
        }
    }
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Startup apps"), input.errorString());
        return;
    }
    QStringList lines = QString::fromUtf8(input.readAll()).split(QLatin1Char('\n'));
    input.close();
    const qsizetype group = lines.indexOf(QStringLiteral("[Desktop Entry]"));
    if (group < 0)
        return;
    qsizetype end = group + 1;
    bool hidden = false;
    for (; end < lines.size() && !lines.at(end).trimmed().startsWith(QLatin1Char('[')); ++end) {
        if (lines.at(end).startsWith(QLatin1String("Hidden="))) {
            lines[end] = enabled ? QStringLiteral("Hidden=false") : QStringLiteral("Hidden=true");
            hidden = true;
        } else if (enabled && lines.at(end).startsWith(QLatin1String("X-GNOME-Autostart-enabled="))) {
            lines[end] = QStringLiteral("X-GNOME-Autostart-enabled=true");
        }
    }
    if (!hidden)
        lines.insert(group + 1, enabled ? QStringLiteral("Hidden=false") : QStringLiteral("Hidden=true"));
    const QByteArray data = lines.join(QLatin1Char('\n')).toUtf8();
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) {
        QMessageBox::warning(this, tr("Startup apps"), output.errorString());
        return;
    }
    refresh();
}
