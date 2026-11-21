// SPDX-License-Identifier: GPL-3.0-or-later
#include "processmodel.h"

#include <karyo/ui/desktopentry.h>

#include <QDateTime>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <functional>
#include <unistd.h>

namespace {

// Programs that host other programs; they never identify an app by themselves.
const QSet<QString> &wrappers()
{
    static const QSet<QString> set = {
        QStringLiteral("sh"),      QStringLiteral("bash"),    QStringLiteral("dash"),
        QStringLiteral("env"),     QStringLiteral("python3"), QStringLiteral("python"),
        QStringLiteral("perl"),    QStringLiteral("flatpak"), QStringLiteral("snap"),
        QStringLiteral("sudo"),    QStringLiteral("pkexec"),  QStringLiteral("java"),
        QStringLiteral("openkaryon-shell"), QStringLiteral("karyo-shell"), QStringLiteral("labwc"),
    };
    return set;
}

struct AppIndex
{
    QHash<QString, DesktopEntry> byExe;
    qint64 builtAt = 0;
};

const QHash<QString, DesktopEntry> &appsByExe()
{
    static AppIndex index;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (index.builtAt && now - index.builtAt < 60)
        return index.byExe;
    index.byExe.clear();
    index.builtAt = now;
    const QList<DesktopEntry> entries = DesktopEntry::all();
    for (const DesktopEntry &entry : entries) {
        if (entry.noDisplay || entry.terminal)
            continue;
        const QStringList cmd = entry.command();
        if (cmd.isEmpty())
            continue;
        const QString exe = QFileInfo(cmd.first()).fileName();
        if (!wrappers().contains(exe) && !index.byExe.contains(exe))
            index.byExe.insert(exe, entry);
        if (!entry.startupWmClass.isEmpty() && !index.byExe.contains(entry.startupWmClass.toLower()))
            index.byExe.insert(entry.startupWmClass.toLower(), entry);
    }
    return index.byExe;
}

// "salmon quant": the executable and its first non-option argument.
QString workloadName(const ProcessInfo &p)
{
    const QStringList args = p.command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString name = p.exe.isEmpty() ? p.name : p.exe;
    for (int i = 1; i < args.size(); ++i) {
        if (!args.at(i).startsWith(QLatin1Char('-'))) {
            name += QLatin1Char(' ') + QFileInfo(args.at(i)).fileName();
            break;
        }
    }
    return name.left(48);
}

} // namespace

QString formatBytes(double bytes)
{
    if (bytes >= 1024.0 * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 1);
    if (bytes >= 1024.0 * 1024)
        return QStringLiteral("%1 MB").arg(qRound(bytes / (1024.0 * 1024)));
    return QStringLiteral("%1 KB").arg(qRound(bytes / 1024.0));
}

QString formatRate(double bytesPerSec)
{
    if (bytesPerSec >= 1024.0 * 1024)
        return QStringLiteral("%1 MB/s").arg(bytesPerSec / (1024.0 * 1024), 0, 'f', bytesPerSec >= 10.0 * 1024 * 1024 ? 0 : 1);
    if (bytesPerSec >= 1024.0)
        return QStringLiteral("%1 KB/s").arg(qRound(bytesPerSec / 1024.0));
    return QStringLiteral("0 MB/s");
}

ProcessModel::ProcessModel(Sampler *sampler, QObject *parent)
    : QAbstractItemModel(parent)
    , m_sampler(sampler)
{
    connect(sampler, &Sampler::updated, this, &ProcessModel::rebuild);
    rebuild();
}

void ProcessModel::setFilter(const QString &text)
{
    m_filter = text.trimmed();
    rebuild();
}

void ProcessModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    rebuild();
}

void ProcessModel::sortNodes(QList<std::shared_ptr<Node>> &nodes) const
{
    const int column = m_sortColumn;
    const bool ascending = m_sortOrder == Qt::AscendingOrder;
    std::stable_sort(nodes.begin(), nodes.end(), [column, ascending](const auto &a, const auto &b) {
        auto less = [column](const Node &x, const Node &y) {
            switch (column) {
            case Cpu: return x.cpu < y.cpu;
            case Memory: return x.memory < y.memory;
            case Disk: return x.disk < y.disk;
            case Environment: return x.environment.compare(y.environment, Qt::CaseInsensitive) < 0;
            default: return x.name.compare(y.name, Qt::CaseInsensitive) < 0;
            }
        };
        return ascending ? less(*a, *b) : less(*b, *a);
    });
    for (auto &node : nodes)
        sortNodes(node->children);
}

void ProcessModel::rebuild()
{
    Q_EMIT aboutToRebuild();
    beginResetModel();
    m_groups.clear();

    const auto &procs = m_sampler->processes();
    const uid_t self = getuid();
    QHash<pid_t, QList<pid_t>> childrenOf;
    for (auto it = procs.cbegin(); it != procs.cend(); ++it) {
        if (it->pid == 2 || it->ppid == 2)
            continue;  // kernel threads
        childrenOf[it->ppid].append(it->pid);
    }
    const auto &apps = appsByExe();

    auto makeLeaf = [](const ProcessInfo &p, const QString &name) {
        auto node = std::make_shared<Node>();
        node->key = QStringLiteral("pid:%1:%2").arg(p.pid).arg(p.startTicks);
        node->name = name;
        node->environment = p.environment;
        node->cpu = p.cpuPercent;
        node->memory = p.rssBytes;
        node->disk = p.diskBytesPerSec;
        node->pids = {p.pid};
        return node;
    };

    QSet<pid_t> claimed;
    // Collects the subtree of pid into node; a child with its own conda env
    // becomes a separate workload instead.
    std::function<void(pid_t, Node *, QList<pid_t> &)> collect = [&](pid_t pid, Node *node, QList<pid_t> &workloads) {
        for (pid_t child : childrenOf.value(pid)) {
            if (claimed.contains(child))
                continue;
            const ProcessInfo &p = procs[child];
            if (!p.environment.isEmpty() && p.environment != procs[pid].environment) {
                workloads.append(child);
                continue;
            }
            claimed.insert(child);
            auto leaf = makeLeaf(p, p.name);
            leaf->parent = node;
            node->cpu += p.cpuPercent;
            node->memory += p.rssBytes;
            node->disk += p.diskBytesPerSec;
            node->pids.append(child);
            node->children.append(leaf);
            collect(child, node, workloads);
        }
    };

    auto appsGroup = std::make_shared<Node>();
    appsGroup->group = true;
    appsGroup->key = QStringLiteral("group:apps");
    auto backgroundGroup = std::make_shared<Node>();
    backgroundGroup->group = true;
    backgroundGroup->key = QStringLiteral("group:background");

    // Apps: user processes started from a desktop entry, plus conda workloads.
    QList<pid_t> pending;
    for (auto it = procs.cbegin(); it != procs.cend(); ++it) {
        const ProcessInfo &p = *it;
        if (p.uid != self || p.ppid == 2)
            continue;
        const auto app = apps.constFind(p.exe);
        const bool isApp = app != apps.constEnd() && !wrappers().contains(p.exe);
        const auto parentIt = procs.constFind(p.ppid);
        const ProcessInfo *parent = parentIt != procs.constEnd() ? &*parentIt : nullptr;
        bool parentSameApp = false;
        if (isApp && parent) {
            const auto parentApp = apps.constFind(parent->exe);
            parentSameApp = parentApp != apps.constEnd() && parentApp->id == app->id;
        }
        if (isApp && !parentSameApp)
            pending.append(p.pid);
        else if (!p.environment.isEmpty() && (!parent || parent->environment != p.environment))
            pending.append(p.pid);
    }
    while (!pending.isEmpty()) {
        const pid_t pid = pending.takeFirst();
        if (claimed.contains(pid))
            continue;
        claimed.insert(pid);
        const ProcessInfo &p = procs[pid];
        const auto app = apps.constFind(p.exe);
        auto node = std::make_shared<Node>();
        node->key = QStringLiteral("app:%1:%2").arg(p.pid).arg(p.startTicks);
        node->parent = appsGroup.get();
        if (app != apps.constEnd() && !wrappers().contains(p.exe)) {
            node->name = app->name;
            node->style = AppStyle::forEntry(*app);
        } else {
            node->name = workloadName(p);
            node->style = AppStyle::fallback(p.exe);
        }
        node->hasStyle = true;
        node->environment = p.environment;
        node->cpu = p.cpuPercent;
        node->memory = p.rssBytes;
        node->disk = p.diskBytesPerSec;
        node->pids = {p.pid};
        QList<pid_t> workloads;
        collect(pid, node.get(), workloads);
        pending.append(workloads);
        appsGroup->children.append(node);
    }

    // Background: every other process; children with the same executable
    // (worker pools, kernels) nest under their parent.
    for (auto it = procs.cbegin(); it != procs.cend(); ++it) {
        const ProcessInfo &p = *it;
        if (claimed.contains(p.pid) || p.pid == 2 || p.ppid == 2)
            continue;
        const auto parentIt = procs.constFind(p.ppid);
        const ProcessInfo *parent = parentIt != procs.constEnd() ? &*parentIt : nullptr;
        if (parent && !claimed.contains(parent->pid) && parent->exe == p.exe && !p.exe.isEmpty())
            continue;  // shown under its parent
        auto node = makeLeaf(p, p.name);
        node->parent = backgroundGroup.get();
        node->hasStyle = true;
        node->style = AppStyle::fallback(p.name);
        std::function<void(pid_t)> nest = [&](pid_t pid) {
            for (pid_t child : childrenOf.value(pid)) {
                const ProcessInfo &c = procs[child];
                if (claimed.contains(child) || c.exe != p.exe)
                    continue;
                auto leaf = makeLeaf(c, c.name);
                leaf->parent = node.get();
                node->cpu += c.cpuPercent;
                node->memory += c.rssBytes;
                node->disk += c.diskBytesPerSec;
                node->pids.append(child);
                node->children.append(leaf);
                nest(child);
            }
        };
        nest(p.pid);
        backgroundGroup->children.append(node);
    }

    // Filter by name, PID or environment.
    if (!m_filter.isEmpty()) {
        auto matches = [this](const Node &n) {
            if (n.name.contains(m_filter, Qt::CaseInsensitive)
                || n.environment.contains(m_filter, Qt::CaseInsensitive))
                return true;
            for (pid_t pid : n.pids) {
                if (QString::number(pid) == m_filter)
                    return true;
            }
            return false;
        };
        for (auto *group : {appsGroup.get(), backgroundGroup.get()}) {
            QList<std::shared_ptr<Node>> kept;
            for (const auto &node : std::as_const(group->children)) {
                bool keep = matches(*node);
                for (const auto &child : std::as_const(node->children))
                    keep = keep || matches(*child);
                if (keep)
                    kept.append(node);
            }
            group->children = kept;
        }
    }

    for (auto *group : {appsGroup.get(), backgroundGroup.get()})
        sortNodes(group->children);
    appsGroup->name = tr("Apps (%1)").arg(appsGroup->children.size());
    backgroundGroup->name = tr("Background processes (%1)").arg(backgroundGroup->children.size());
    m_groups = {appsGroup, backgroundGroup};
    endResetModel();
    Q_EMIT rebuilt();
}

int ProcessModel::appCount() const { return m_groups.isEmpty() ? 0 : int(m_groups.first()->children.size()); }
int ProcessModel::backgroundCount() const { return m_groups.size() < 2 ? 0 : int(m_groups.at(1)->children.size()); }

ProcessModel::Node *ProcessModel::nodeFor(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<Node *>(index.internalPointer()) : nullptr;
}

QModelIndex ProcessModel::indexFor(Node *node, int column) const
{
    if (!node)
        return {};
    const QList<std::shared_ptr<Node>> &siblings = node->parent ? node->parent->children : m_groups;
    for (int row = 0; row < siblings.size(); ++row) {
        if (siblings.at(row).get() == node)
            return createIndex(row, column, node);
    }
    return {};
}

QModelIndex ProcessModel::index(int row, int column, const QModelIndex &parent) const
{
    const QList<std::shared_ptr<Node>> &nodes = parent.isValid() ? nodeFor(parent)->children : m_groups;
    if (row < 0 || row >= nodes.size() || column < 0 || column >= ColumnCount)
        return {};
    return createIndex(row, column, nodes.at(row).get());
}

QModelIndex ProcessModel::parent(const QModelIndex &child) const
{
    Node *node = nodeFor(child);
    return node ? indexFor(node->parent, 0) : QModelIndex();
}

int ProcessModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    return parent.isValid() ? int(nodeFor(parent)->children.size()) : int(m_groups.size());
}

int ProcessModel::columnCount(const QModelIndex &) const { return ColumnCount; }

Qt::ItemFlags ProcessModel::flags(const QModelIndex &index) const
{
    Node *node = nodeFor(index);
    if (!node)
        return Qt::NoItemFlags;
    return node->group ? Qt::ItemIsEnabled : (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
}

QModelIndex ProcessModel::indexForKey(const QString &key) const
{
    std::function<Node *(const QList<std::shared_ptr<Node>> &)> find = [&](const auto &nodes) -> Node * {
        for (const auto &node : nodes) {
            if (node->key == key)
                return node.get();
            if (Node *found = find(node->children))
                return found;
        }
        return nullptr;
    };
    return indexFor(find(m_groups), 0);
}

QVariant ProcessModel::data(const QModelIndex &index, int role) const
{
    Node *node = nodeFor(index);
    if (!node)
        return {};
    const int column = index.column();
    switch (role) {
    case Qt::DisplayRole:
        if (node->group)
            return column == Name ? node->name : QVariant();
        switch (column) {
        case Name: return node->name;
        case Environment: return node->environment;
        case Cpu: return QStringLiteral("%1%").arg(node->cpu, 0, 'f', 1);
        case Memory: return formatBytes(double(node->memory));
        case Disk: return formatRate(node->disk);
        case Network: return QString();
        }
        break;
    case ValueRole:
        switch (column) {
        case Cpu: return node->cpu;
        case Memory: return double(node->memory);
        case Disk: return node->disk;
        }
        break;
    case Qt::TextAlignmentRole:
        return column >= Cpu ? int(Qt::AlignRight | Qt::AlignVCenter) : int(Qt::AlignLeft | Qt::AlignVCenter);
    case GroupRole:
        return node->group;
    case KeyRole:
        return node->key;
    case PidsRole: {
        QVariantList pids;
        for (pid_t pid : node->pids)
            pids.append(int(pid));
        return pids;
    }
    case StyleRole:
        if (node->hasStyle && column == Name) {
            QVariantMap style;
            style.insert(QStringLiteral("glyph"), node->style.glyph);
            style.insert(QStringLiteral("monogram"), node->style.monogram);
            style.insert(QStringLiteral("tile"), node->style.tile);
            style.insert(QStringLiteral("ink"), node->style.ink);
            style.insert(QStringLiteral("icon"), node->style.themeIcon);
            return style;
        }
        break;
    }
    return {};
}

QVariant ProcessModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Name: return tr("Name");
    case Environment: return tr("Environment");
    case Cpu: return tr("CPU");
    case Memory: return tr("Memory");
    case Disk: return tr("Disk");
    case Network: return tr("Network");
    }
    return {};
}
