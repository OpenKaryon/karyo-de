// SPDX-License-Identifier: GPL-3.0-or-later
#include "appindex.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <atomic>
#include <memory>

namespace {

// Start's pinned grid in KaryonStart.dc.html; apps that are not installed are skipped.
const QStringList DefaultPinned = {
    QStringLiteral("openkaryon-files.desktop"),     QStringLiteral("chromium.desktop"),
    QStringLiteral("karyo-terminal.desktop"),
    QStringLiteral("openkaryon-terminal.desktop"),  QStringLiteral("karyo-editor.desktop"), QStringLiteral("openkaryon-editor.desktop"),
    QStringLiteral("openkaryon-appcenter.desktop"), QStringLiteral("openkaryon-monitor.desktop"),
    QStringLiteral("openkaryon-settings.desktop"),  QStringLiteral("rstudio.desktop"),
    QStringLiteral("jupyterlab.desktop"),           QStringLiteral("igv.desktop"),
    QStringLiteral("libreoffice-writer.desktop"),   QStringLiteral("libreoffice-calc.desktop"),
    QStringLiteral("karyo-photos.desktop"), QStringLiteral("openkaryon-photos.desktop"),    QStringLiteral("openkaryon-player.desktop"),
    QStringLiteral("openkaryon-reader.desktop"),
    QStringLiteral("karyo-calculator.desktop"), QStringLiteral("openkaryon-calculator.desktop"), QStringLiteral("karyo-clock.desktop"), QStringLiteral("openkaryon-clock.desktop"),
    QStringLiteral("zotero.desktop"),
    QStringLiteral("org.inkscape.Inkscape.desktop"), QStringLiteral("thunderbird.desktop"),
    QStringLiteral("openkaryon-archive.desktop"),
};

// LXQt's session actions; Start's power button does their job.
const QStringList HiddenApps = {
    QStringLiteral("lxqt-hibernate.desktop"), QStringLiteral("lxqt-leave.desktop"),
    QStringLiteral("lxqt-lockscreen.desktop"), QStringLiteral("lxqt-logout.desktop"),
    QStringLiteral("lxqt-reboot.desktop"),     QStringLiteral("lxqt-shutdown.desktop"),
    QStringLiteral("lxqt-suspend.desktop"),
};

// The settings apps themselves are apps; other Settings entries are pages.
const QStringList SettingsApps = {
    QStringLiteral("lxqt-config.desktop"),
    QStringLiteral("openkaryon-settings.desktop"),
};

struct Rule
{
    const char *key;
    const char *glyph;
    QStringList categories;
};

// Freedesktop categories folded into Start's groups; the first match wins.
// The first groups hold the catalog's command-line tools ("section" in
// catalog.json), as in KaryonStart.dc.html's categories.
const QList<Rule> &rules()
{
    static const QList<Rule> table = {
        {"qc", "dna", {}},
        {"alignment", "git-merge", {}},
        {"variants", "flask-conical", {}},
        {"expression", "chart-column", {}},
        {"assembly", "layers", {}},
        {"workflows", "git-branch", {}},
        {"stats", "sigma", {}},
        {"science", "flask-conical",
         {QStringLiteral("Science"), QStringLiteral("Biology"), QStringLiteral("Chemistry"),
          QStringLiteral("Math"), QStringLiteral("DataVisualization"), QStringLiteral("Education")}},
        {"office", "file-text",
         {QStringLiteral("Office"), QStringLiteral("TextEditor"), QStringLiteral("WordProcessor"),
          QStringLiteral("Spreadsheet")}},
        {"development", "file-code", {QStringLiteral("Development")}},
        {"internet", "globe", {QStringLiteral("Network"), QStringLiteral("WebBrowser")}},
        {"graphics", "image", {QStringLiteral("Graphics")}},
        {"multimedia", "play",
         {QStringLiteral("AudioVideo"), QStringLiteral("Audio"), QStringLiteral("Video")}},
        {"system", "monitor",
         {QStringLiteral("System"), QStringLiteral("Settings"), QStringLiteral("FileManager"),
          QStringLiteral("TerminalEmulator"), QStringLiteral("PackageManager")}},
        {"utilities", "boxes", {QStringLiteral("Utility")}},
    };
    return table;
}

QString categoryOf(const DesktopEntry &entry)
{
    for (const Rule &rule : rules()) {
        for (const QString &category : rule.categories) {
            if (entry.categories.contains(category))
                return QString::fromLatin1(rule.key);
        }
    }
    return QStringLiteral("other");
}

QString glyphOf(const QString &key)
{
    for (const Rule &rule : rules()) {
        if (key == QLatin1String(rule.key))
            return QString::fromLatin1(rule.glyph);
    }
    return QStringLiteral("layout-grid");
}

QSettings shellSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("shell"));
}

bool byName(const AppInfo &a, const AppInfo &b)
{
    return QString::localeAwareCompare(a.entry.name, b.entry.name) < 0;
}

// The user's conda installation, as App Center finds it.
QString condaRoot()
{
    const QString home = QDir::homePath();
    for (const QString &candidate : {home + QStringLiteral("/miniforge3"), home + QStringLiteral("/mambaforge"),
                                     home + QStringLiteral("/miniconda3"), home + QStringLiteral("/anaconda3"),
                                     QStringLiteral("/opt/conda"), QStringLiteral("/opt/miniforge3")}) {
        if (QFileInfo::exists(candidate + QStringLiteral("/bin/conda")))
            return candidate;
    }
    return {};
}

// Environment name and its bin folder: "base" for the root, then envs/*.
QList<QPair<QString, QString>> condaBins(const QString &root)
{
    if (root.isEmpty())
        return {};
    QList<QPair<QString, QString>> bins{{QStringLiteral("base"), root + QStringLiteral("/bin")}};
    const QString envs = root + QStringLiteral("/envs");
    for (const QString &name : QDir(envs).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        bins.append({name, envs + QLatin1Char('/') + name + QStringLiteral("/bin")});
    return bins;
}

// Installed Debian packages: dpkg keeps a file list for each, named
// "name.list" or, for Multi-Arch: same packages, "name:arch.list".
QSet<QString> debianPackages()
{
    QSet<QString> names;
    const QStringList lists = QDir(QStringLiteral("/var/lib/dpkg/info")).entryList({QStringLiteral("*.list")}, QDir::Files);
    for (const QString &list : lists)
        names.insert(list.chopped(5).section(QLatin1Char(':'), 0, 0));
    return names;
}

// A CLI tile shows the catalog's short mono name ("fqc") on a monogram tile.
AppStyle toolStyle(const QString &name, const QString &tile)
{
    AppStyle style = AppStyle::fallback(name);
    if (!tile.isEmpty())
        style.monogram = tile;
    return style;
}

} // namespace

AppIndex::AppIndex(QObject *parent)
    : QObject(parent)
{
}

void AppIndex::refresh()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_loadedAt && now - m_loadedAt < 5000)
        return;
    m_loadedAt = now;
    m_apps.clear();
    m_settings.clear();
    const QList<DesktopEntry> entries = DesktopEntry::all();
    for (const DesktopEntry &entry : entries) {
        if (entry.noDisplay || entry.exec.isEmpty() || HiddenApps.contains(entry.id))
            continue;
        AppInfo info;
        info.entry = entry;
        info.style = AppStyle::forEntry(entry);
        info.category = categoryOf(entry);
        const bool page = entry.categories.contains(QStringLiteral("Settings"))
                          && !SettingsApps.contains(entry.id);
        (page ? m_settings : m_apps).append(info);
    }
    loadTools();
    std::sort(m_apps.begin(), m_apps.end(), byName);
    std::sort(m_settings.begin(), m_settings.end(), byName);
    Q_EMIT changed();
}

void AppIndex::loadTools()
{
    QFile file(QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                     QStringLiteral("karyo/catalog.json")));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray catalog = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("apps")).toArray();
    const bool russian = QLocale().language() == QLocale::Russian;
    const QString root = condaRoot();
    const auto bins = condaBins(root);
    const QSet<QString> debian = debianPackages();
    QSet<QString> names;
    for (const AppInfo &app : std::as_const(m_apps))
        names.insert(app.entry.name.toLower());
    for (const QJsonValue &value : catalog) {
        const QJsonObject o = value.toObject();
        if (o.value(QStringLiteral("kind")).toString() != QLatin1String("cli"))
            continue;
        AppInfo tool;
        tool.toolId = o.value(QStringLiteral("id")).toString();
        tool.entry.id = QStringLiteral("tool:") + tool.toolId;
        tool.entry.name = o.value(QStringLiteral("name")).toString();
        // An app with a desktop entry (R, Jupyter) is listed once, as the app.
        if (names.contains(tool.entry.name.toLower()))
            continue;
        tool.entry.exec = o.value(QStringLiteral("command")).toString();
        tool.entry.terminal = true;
        tool.entry.comment =
            o.value(russian && o.contains(QStringLiteral("summary_ru")) ? QStringLiteral("summary_ru")
                                                                         : QStringLiteral("summary"))
                .toString();
        tool.style = toolStyle(tool.entry.name, o.value(QStringLiteral("tile")).toString());
        tool.category = o.value(QStringLiteral("section")).toString(QStringLiteral("utilities"));
        tool.source = o.value(QStringLiteral("source")).toString();
        for (const QJsonValue &sub : o.value(QStringLiteral("subcommands")).toArray())
            tool.subcommands.append(sub.toString());
        for (const auto &bin : bins) {
            if (QFileInfo(bin.second + QLatin1Char('/') + tool.entry.exec).isExecutable())
                tool.envs.append(bin.first);
        }
        tool.condaRoot = root;
        // A Debian tool counts when its package is installed: the command
        // alone can mislead (Biopython's is python3).
        const QString package = o.value(QStringLiteral("package")).toString();
        const bool system = tool.source == QLatin1String("apt") ? debian.contains(package)
                                                                 : !commandPath(tool.entry.exec).isEmpty();
        tool.installed = system || !tool.envs.isEmpty();
        m_apps.append(tool);
    }
}

QString AppIndex::labelFor(const QString &key)
{
    static const QHash<QString, const char *> labels = {
        {QStringLiteral("all"), QT_TRANSLATE_NOOP("AppIndex", "All apps")},
        {QStringLiteral("qc"), QT_TRANSLATE_NOOP("AppIndex", "Sequencing & QC")},
        {QStringLiteral("alignment"), QT_TRANSLATE_NOOP("AppIndex", "Alignment")},
        {QStringLiteral("variants"), QT_TRANSLATE_NOOP("AppIndex", "Variant calling")},
        {QStringLiteral("expression"), QT_TRANSLATE_NOOP("AppIndex", "Expression")},
        {QStringLiteral("assembly"), QT_TRANSLATE_NOOP("AppIndex", "Genomes")},
        {QStringLiteral("workflows"), QT_TRANSLATE_NOOP("AppIndex", "Workflows")},
        {QStringLiteral("stats"), QT_TRANSLATE_NOOP("AppIndex", "Statistics & R")},
        {QStringLiteral("science"), QT_TRANSLATE_NOOP("AppIndex", "Science")},
        {QStringLiteral("office"), QT_TRANSLATE_NOOP("AppIndex", "Office & writing")},
        {QStringLiteral("development"), QT_TRANSLATE_NOOP("AppIndex", "Development")},
        {QStringLiteral("internet"), QT_TRANSLATE_NOOP("AppIndex", "Internet")},
        {QStringLiteral("graphics"), QT_TRANSLATE_NOOP("AppIndex", "Graphics")},
        {QStringLiteral("multimedia"), QT_TRANSLATE_NOOP("AppIndex", "Multimedia")},
        {QStringLiteral("system"), QT_TRANSLATE_NOOP("AppIndex", "System")},
        {QStringLiteral("utilities"), QT_TRANSLATE_NOOP("AppIndex", "Utilities")},
        {QStringLiteral("other"), QT_TRANSLATE_NOOP("AppIndex", "Other")},
    };
    return QCoreApplication::translate("AppIndex", labels.value(key, "Other"));
}

QList<Category> AppIndex::categories(bool installed) const
{
    QList<Category> out;
    if (installed)
        out.append({QStringLiteral("all"), QStringLiteral("layout-grid"), labelFor(QStringLiteral("all")),
                    installedCount()});
    QStringList keys;
    for (const Rule &rule : rules())
        keys.append(QString::fromLatin1(rule.key));
    keys.append(QStringLiteral("other"));
    for (const QString &key : std::as_const(keys)) {
        const int count = int(std::count_if(m_apps.cbegin(), m_apps.cend(), [&](const AppInfo &app) {
            return app.category == key && app.installed == installed;
        }));
        if (count > 0)
            out.append({key, glyphOf(key), labelFor(key), count});
    }
    return out;
}

QList<AppInfo> AppIndex::appsIn(const QString &category, bool installed) const
{
    QList<AppInfo> out;
    for (const AppInfo &app : m_apps) {
        if (app.installed == installed && (category == QLatin1String("all") || app.category == category))
            out.append(app);
    }
    return out;
}

int AppIndex::installedCount() const
{
    return int(std::count_if(m_apps.cbegin(), m_apps.cend(), [](const AppInfo &app) { return app.installed; }));
}

QStringList AppIndex::pinnedIds() const
{
    return shellSettings().value(QStringLiteral("Start/pinned"), DefaultPinned).toStringList();
}

QList<AppInfo> AppIndex::pinnedApps() const
{
    QList<AppInfo> out;
    for (const QString &id : pinnedIds()) {
        for (const AppInfo &app : m_apps) {
            if (app.entry.id == id) {
                out.append(app);
                break;
            }
        }
    }
    return out;
}

bool AppIndex::isPinned(const QString &id) const { return pinnedIds().contains(id); }

void AppIndex::setPinned(const QString &id, bool pinned)
{
    QStringList ids = pinnedIds();
    if (pinned == ids.contains(id))
        return;
    if (pinned)
        ids.append(id);
    else
        ids.removeAll(id);
    shellSettings().setValue(QStringLiteral("Start/pinned"), ids);
    Q_EMIT changed();
}

void AppIndex::setPinnedOrder(const QStringList &ids)
{
    // Pinned ids that Start does not show (apps not installed) keep their place after them.
    QStringList order = ids;
    for (const QString &id : pinnedIds()) {
        if (!order.contains(id))
            order.append(id);
    }
    shellSettings().setValue(QStringLiteral("Start/pinned"), order);
    Q_EMIT changed();
}

void AppIndex::noteUsed(const QString &id)
{
    QSettings settings = shellSettings();
    QStringList ids = settings.value(QStringLiteral("Start/recentApps")).toStringList();
    if (!ids.isEmpty() && ids.first() == id)
        return;
    ids.removeAll(id);
    ids.prepend(id);
    settings.setValue(QStringLiteral("Start/recentApps"), ids.mid(0, 24));
}

QList<AppInfo> AppIndex::recentApps(int limit) const
{
    QList<AppInfo> out;
    const QStringList ids = shellSettings().value(QStringLiteral("Start/recentApps")).toStringList();
    for (const QString &id : ids) {
        for (const AppInfo &app : m_apps) {
            if (app.installed && app.entry.id == id) {
                out.append(app);
                break;
            }
        }
        if (out.size() == limit)
            break;
    }
    return out;
}

int AppIndex::commandLineCount() const
{
    return int(std::count_if(m_apps.cbegin(), m_apps.cend(),
                             [](const AppInfo &app) { return app.installed && app.entry.terminal; }));
}

QStringList AppIndex::commands(const QString &prefix, int limit) const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_commandCache.isEmpty() || now - m_commandsAt > 60000) {
        QSet<QString> names;
        const QStringList dirs = qEnvironmentVariable("PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
        for (const QString &dir : dirs) {
            const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files | QDir::Executable);
            for (const QFileInfo &file : files)
                names.insert(file.fileName());
        }
        m_commandCache = QStringList(names.cbegin(), names.cend());
        m_commandsAt = now;
    }
    QStringList out;
    for (const QString &name : std::as_const(m_commandCache)) {
        if (name.startsWith(prefix))
            out.append(name);
    }
    std::sort(out.begin(), out.end(), [&prefix](const QString &a, const QString &b) {
        if (prefix.isEmpty())
            return a < b;
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    return out.mid(0, limit);
}

QString AppIndex::commandPath(const QString &name)
{
    return QStandardPaths::findExecutable(name);
}

QList<FileHit> AppIndex::recentFiles(int limit) const
{
    // Settings > Privacy & security.
    if (!shellSettings().value(QStringLiteral("Start/showRecent"), true).toBool())
        return {};
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
               + QStringLiteral("/recently-used.xbel"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QList<FileHit> hits;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("bookmark")) {
            const auto attrs = xml.attributes();
            const QUrl url(attrs.value(QLatin1String("href")).toString());
            QDateTime when = QDateTime::fromString(attrs.value(QLatin1String("visited")).toString(),
                                                   Qt::ISODate);
            const QDateTime modified = QDateTime::fromString(
                attrs.value(QLatin1String("modified")).toString(), Qt::ISODate);
            if (!when.isValid() || (modified.isValid() && modified > when))
                when = modified;
            if (url.isLocalFile()) {
                const QFileInfo info(url.toLocalFile());
                if (info.exists())
                    hits.append({info.absoluteFilePath(), when.toLocalTime(), info.size(), info.isDir()});
            }
        }
        if (xml.hasError())
            break;
    }
    std::sort(hits.begin(), hits.end(),
              [](const FileHit &a, const FileHit &b) { return a.modified > b.modified; });
    return hits.mid(0, limit);
}

void AppIndex::searchFiles(const QString &query)
{
    static auto latest = std::make_shared<std::atomic<quint64>>(0);
    const quint64 generation = ++m_fileQuery;
    latest->store(generation);
    if (query.trimmed().size() < 2 || !shellSettings().value(QStringLiteral("Start/searchFiles"), true).toBool()) {
        Q_EMIT filesFound(query, {});
        return;
    }
    QPointer<AppIndex> self(this);
    QThreadPool::globalInstance()->start([self, query, generation, current = latest] {
        constexpr int MaxVisited = 20000;
        constexpr int MaxHits = 40;
        const QString needle = query.trimmed();
        QList<FileHit> hits;
        int visited = 0;
        // Hidden folders (caches, conda pkgs, .git) stay out of the walk.
        QDirIterator it(QDir::homePath(), QDir::AllEntries | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext() && visited < MaxVisited && hits.size() < MaxHits) {
            if (current->load() != generation)
                return;
            it.next();
            ++visited;
            const QFileInfo info = it.fileInfo();
            if (info.fileName().contains(needle, Qt::CaseInsensitive))
                hits.append({info.absoluteFilePath(), info.lastModified(), info.size(), info.isDir()});
        }
        std::sort(hits.begin(), hits.end(), [&needle](const FileHit &a, const FileHit &b) {
            const bool aPrefix = QFileInfo(a.path).fileName().startsWith(needle, Qt::CaseInsensitive);
            const bool bPrefix = QFileInfo(b.path).fileName().startsWith(needle, Qt::CaseInsensitive);
            if (aPrefix != bPrefix)
                return aPrefix;
            return a.modified > b.modified;
        });
        QMetaObject::invokeMethod(
            qApp,
            [self, query, generation, current, hits] {
                if (self && current->load() == generation)
                    Q_EMIT self->filesFound(query, hits);
            },
            Qt::QueuedConnection);
    });
}
