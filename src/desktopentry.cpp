// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/desktopentry.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <utility>
#ifdef Q_OS_LINUX
#include <sys/stat.h>
#endif

namespace {

// Keep the caller alive as the direct parent: authorization brokers validate it.
// The observer owns itself until process exit. It is deliberately not parented
// to qApp, so shutting down the launcher does not kill ordinary applications.
bool startApplication(const QString &program, const QStringList &args, qint64 *pid = nullptr)
{
    if (pid)
        *pid = 0;
    if (!QCoreApplication::instance())
        return QProcess::startDetached(program, args, QDir::homePath(), pid);
    auto *process = new QProcess;
    process->setProgram(program);
    process->setArguments(args);
    process->setWorkingDirectory(QDir::homePath());
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
    QObject::connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            process->deleteLater();
    });
    qint64 launchedPid = 0;
    const auto started = QObject::connect(process, &QProcess::started, process,
                                         [&] { launchedPid = process->processId(); });
    process->start();
    const bool running = process->waitForStarted();
    QObject::disconnect(started);
    if (!running) {
        process->deleteLater();
        return false;
    }
    if (pid)
        *pid = launchedPid;
    return true;
}

QString unescape(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (int i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != QLatin1Char('\\') || i + 1 == value.size()) {
            out.append(c);
            continue;
        }
        const QChar n = value.at(++i);
        switch (n.unicode()) {
        case 's': out.append(QLatin1Char(' ')); break;
        case 'n': out.append(QLatin1Char('\n')); break;
        case 't': out.append(QLatin1Char('\t')); break;
        case 'r': out.append(QLatin1Char('\r')); break;
        default: out.append(n); break;
        }
    }
    return out;
}

QStringList splitList(const QString &value)
{
    QStringList items = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (QString &item : items)
        item = item.trimmed();
    return items;
}

// Locale keys in lookup order, e.g. ru_RU, ru.
QStringList localeSuffixes()
{
    const QString name = QLocale().name();
    QStringList suffixes{name};
    const qsizetype underscore = name.indexOf(QLatin1Char('_'));
    if (underscore > 0)
        suffixes.append(name.left(underscore));
    return suffixes;
}

QString normalized(QString value)
{
    value = value.toLower();
    if (value.endsWith(QLatin1String(".desktop")))
        value.chop(8);
    return value;
}

// Chromium/Electron can append the user-data directory to their window
// class: "app (/absolute/profile/path)". Only remove that specific suffix;
// parenthesized application names and profile labels keep their identity.
QString profileBaseAppId(const QString &appId)
{
    const qsizetype start = appId.indexOf(QLatin1String(" (/"));
    if (start <= 0 || !appId.endsWith(QLatin1Char(')')))
        return appId;
    const QString path = appId.mid(start + 2, appId.size() - start - 3);
    return QDir::isAbsolutePath(path) ? appId.left(start) : appId;
}

bool matchesPlainAppId(const DesktopEntry &entry, const QString &appId)
{
    const QString app = normalized(appId);
    const QString own = normalized(entry.id);
    if (app == own)
        return true;
    if (!entry.startupWmClass.isEmpty() && app == entry.startupWmClass.toLower())
        return true;
    // org.kde.dolphin <-> dolphin, and firefox-esr <-> firefox-esr.desktop.
    const QString ownTail = own.section(QLatin1Char('.'), -1);
    const QString appTail = app.section(QLatin1Char('.'), -1);
    if (own.contains(QLatin1Char('.')) && ownTail == app)
        return true;
    if (app.contains(QLatin1Char('.')) && appTail == own)
        return true;
    // Exec basename, e.g. "featherpad %U" -> featherpad.
    const QStringList cmd = entry.command();
    return !cmd.isEmpty() && QFileInfo(cmd.first()).fileName().toLower() == app;
}

QByteArray applicationCatalogStamp()
{
    QByteArray result;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        const QByteArray path = QFile::encodeName(dir);
        result += path + '\0';
#ifdef Q_OS_LINUX
        struct stat info {};
        if (::stat(path.constData(), &info) == 0) {
            result += QByteArray::number(quint64(info.st_dev)) + ':';
            result += QByteArray::number(quint64(info.st_ino)) + ':';
            result += QByteArray::number(qint64(info.st_mtim.tv_sec)) + ':';
            result += QByteArray::number(qint64(info.st_mtim.tv_nsec));
        }
#else
        result += QByteArray::number(QFileInfo(dir).lastModified().toMSecsSinceEpoch());
#endif
        result += '\0';
    }
    return result;
}

} // namespace

QStringList DesktopEntry::applicationDirs()
{
    QStringList dirs;
    for (const QString &dir :
         QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        if (!dirs.contains(dir))
            dirs.append(dir);
    }
    return dirs;
}

std::optional<DesktopEntry> DesktopEntry::load(const QString &path, const QString &id)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;

    QHash<QString, QString> values;
    bool inMainGroup = false;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            inMainGroup = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inMainGroup)
            continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        values.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    if (values.value(QStringLiteral("Type")) != QLatin1String("Application"))
        return std::nullopt;
    if (values.value(QStringLiteral("Hidden")) == QLatin1String("true"))
        return std::nullopt;

    auto localized = [&](const QString &key) {
        for (const QString &suffix : localeSuffixes()) {
            const QString value = values.value(QStringLiteral("%1[%2]").arg(key, suffix));
            if (!value.isEmpty())
                return unescape(value);
        }
        return unescape(values.value(key));
    };

    // Desktops listed in OnlyShowIn/NotShowIn are honoured for our own name.
    const QStringList desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP")
                                     .split(QLatin1Char(':'), Qt::SkipEmptyParts);
    const QStringList onlyShowIn = splitList(values.value(QStringLiteral("OnlyShowIn")));
    const QStringList notShowIn = splitList(values.value(QStringLiteral("NotShowIn")));
    bool shown = onlyShowIn.isEmpty();
    for (const QString &desktop : desktops) {
        if (onlyShowIn.contains(desktop))
            shown = true;
        if (notShowIn.contains(desktop))
            shown = false;
    }

    DesktopEntry entry;
    entry.id = id;
    entry.path = path;
    entry.name = localized(QStringLiteral("Name"));
    entry.genericName = localized(QStringLiteral("GenericName"));
    entry.comment = localized(QStringLiteral("Comment"));
    entry.icon = values.value(QStringLiteral("Icon"));
    entry.exec = values.value(QStringLiteral("Exec"));
    entry.startupWmClass = values.value(QStringLiteral("StartupWMClass"));
    entry.categories = splitList(values.value(QStringLiteral("Categories")));
    entry.keywords = splitList(localized(QStringLiteral("Keywords")));
    entry.mimeTypes = splitList(values.value(QStringLiteral("MimeType")));
    entry.noDisplay = !shown || values.value(QStringLiteral("NoDisplay")) == QLatin1String("true");
    entry.terminal = values.value(QStringLiteral("Terminal")) == QLatin1String("true");
    entry.tryExec = unescape(values.value(QStringLiteral("TryExec")));
    entry.desktopNames = splitList(values.value(QStringLiteral("DesktopNames")));
    if (entry.name.isEmpty() || entry.exec.isEmpty())
        return std::nullopt;
    return entry;
}

std::optional<DesktopEntry> DesktopEntry::find(const QString &id)
{
    // "foo-bar.desktop" may live at applications/foo/bar.desktop.
    QStringList relative{id};
    QString nested = id;
    for (qsizetype dash = nested.indexOf(QLatin1Char('-')); dash > 0;
         dash = nested.indexOf(QLatin1Char('-'), dash + 1)) {
        QString candidate = nested;
        candidate[dash] = QLatin1Char('/');
        relative.append(candidate);
    }
    for (const QString &dir : applicationDirs()) {
        for (const QString &rel : relative) {
            const QString path = QDir(dir).filePath(rel);
            if (QFileInfo::exists(path))
                return load(path, id);
        }
    }
    return std::nullopt;
}

QList<DesktopEntry> DesktopEntry::all()
{
    QList<DesktopEntry> entries;
    QSet<QString> seen;
    for (const QString &dir : applicationDirs()) {
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QString id = QDir(dir).relativeFilePath(path);
            id.replace(QLatin1Char('/'), QLatin1Char('-'));
            if (seen.contains(id))
                continue;
            seen.insert(id);
            if (auto entry = load(path, id))
                entries.append(*entry);
        }
    }
    return entries;
}

bool DesktopEntry::matchesAppId(const QString &appId) const
{
    if (appId.isEmpty())
        return false;
    if (matchesPlainAppId(*this, appId))
        return true;
    const QString base = profileBaseAppId(appId);
    return base != appId && matchesPlainAppId(*this, base);
}

std::optional<DesktopEntry> DesktopEntry::forAppId(const QString &appId)
{
    if (appId.isEmpty())
        return std::nullopt;
    QStringList candidates{appId};
    const QString base = profileBaseAppId(appId);
    if (base != appId)
        candidates.append(base);
    static QList<DesktopEntry> cached;
    static qint64 cachedAt = 0;
    static QByteArray cachedCatalogStamp;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QByteArray catalogStamp = applicationCatalogStamp();
    std::optional<DesktopEntry> suffixMatch;
    bool ambiguousSuffix = false;
    for (const QString &candidate : std::as_const(candidates)) {
        if (!candidate.contains(QLatin1Char('/'))) {
            if (auto direct = find(candidate + QStringLiteral(".desktop")))
                return direct;
            if (auto lower = find(candidate.toLower() + QStringLiteral(".desktop")))
                return lower;
        }
        if (cached.isEmpty() || now - cachedAt > 30 || cachedCatalogStamp != catalogStamp) {
            cached = all();
            cachedAt = now;
            cachedCatalogStamp = catalogStamp;
        }
        // Exact desktop IDs / StartupWMClass take precedence over stripping
        // a profile suffix, including entries intentionally naming a profile.
        const QString app = normalized(candidate);
        const QString fullApp = candidate.toLower();
        const QString suffix = QLatin1Char('.') + app;
        const QString fullSuffix = QLatin1Char('.') + fullApp;
        const bool hasNamespace = app.contains(QLatin1Char('.'))
            && !app.startsWith(QLatin1Char('.')) && !app.endsWith(QLatin1Char('.'));
        for (const DesktopEntry &entry : std::as_const(cached)) {
            if (matchesPlainAppId(entry, candidate))
                return entry;
            // A window can omit the vendor prefix from a multi-part desktop ID.
            // Its last component can itself be "Desktop", as in Mattermost.Desktop.
            const QString id = normalized(entry.id);
            if ((hasNamespace && id.endsWith(suffix))
                || (fullApp != app && id.endsWith(fullSuffix))) {
                if (!suffixMatch)
                    suffixMatch = entry;
                else if (suffixMatch->id != entry.id)
                    ambiguousSuffix = true;
            }
        }
    }
    if (ambiguousSuffix)
        return std::nullopt;
    return suffixMatch;
}

QStringList DesktopEntry::command() const
{
    QStringList args = QProcess::splitCommand(exec);
    QStringList out;
    for (QString arg : args) {
        if (arg.size() == 2 && arg.startsWith(QLatin1Char('%'))) {
            const QChar code = arg.at(1);
            if (code == QLatin1Char('%'))
                out.append(QStringLiteral("%"));
            else if (code == QLatin1Char('c'))
                out.append(name);
            else if (code == QLatin1Char('k'))
                out.append(path);
            // %f %F %u %U %i and deprecated codes expand to nothing without files.
            continue;
        }
        arg.replace(QLatin1String("%%"), QLatin1String("%"));
        out.append(arg);
    }
    return out;
}

bool DesktopEntry::launch() const
{
    QStringList cmd = command();
    if (cmd.isEmpty())
        return false;
    if (terminal) {
        const QString term = qEnvironmentVariable("TERMINAL", QStringLiteral("x-terminal-emulator"));
        cmd.prepend(QStringLiteral("-e"));
        cmd.prepend(term);
    }
    const QString program = cmd.takeFirst();
    return startApplication(program, cmd);
}

bool DesktopEntry::launchWith(const QStringList &paths, qint64 *pid) const
{
    QStringList args = QProcess::splitCommand(exec);
    QStringList out;
    bool placed = false;
    for (const QString &arg : args) {
        if (arg == QLatin1String("%f") || arg == QLatin1String("%u")) {
            if (!paths.isEmpty())
                out.append(paths.first());
            placed = true;
        } else if (arg == QLatin1String("%F") || arg == QLatin1String("%U")) {
            out.append(paths);
            placed = true;
        } else if (arg.size() == 2 && arg.startsWith(QLatin1Char('%'))) {
            if (arg.at(1) == QLatin1Char('c'))
                out.append(name);
        } else {
            out.append(arg);
        }
    }
    if (!placed)
        out.append(paths);
    if (out.isEmpty())
        return false;
    if (terminal)
        out = QStringList{qEnvironmentVariable("TERMINAL", QStringLiteral("x-terminal-emulator")),
                          QStringLiteral("-e")} + out;
    const QString program = out.takeFirst();
    return startApplication(program, out, pid);
}

namespace {

// Default applications from mimeapps.list, user file first.
QStringList defaultIds(const QString &mimeType)
{
    QStringList files;
    files.append(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                 + QStringLiteral("/mimeapps.list"));
    const QStringList desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower()
                                     .split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation)) {
        for (const QString &desktop : desktops) {
            if (!desktop.contains(QLatin1Char('/')) && !desktop.contains(QLatin1Char('\\')))
                files.append(dir + QStringLiteral("/%1-mimeapps.list").arg(desktop));
        }
        files.append(dir + QStringLiteral("/mimeapps.list"));
    }
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        for (const QString &desktop : desktops) {
            if (!desktop.contains(QLatin1Char('/')) && !desktop.contains(QLatin1Char('\\')))
                files.append(dir + QStringLiteral("/%1-mimeapps.list").arg(desktop));
        }
        files.append(dir + QStringLiteral("/mimeapps.list"));
    }
    QStringList ids;
    for (const QString &path : std::as_const(files)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        bool inDefaults = false;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.startsWith(QLatin1Char('['))) {
                inDefaults = line == QLatin1String("[Default Applications]");
                continue;
            }
            if (inDefaults && line.startsWith(mimeType + QLatin1Char('='))) {
                for (const QString &id : line.mid(mimeType.size() + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                    if (!ids.contains(id))
                        ids.append(id);
                }
            }
        }
    }
    return ids;
}

} // namespace

std::optional<DesktopEntry> DesktopEntry::defaultFor(const QString &mimeType)
{
    for (const QString &id : defaultIds(mimeType)) {
        if (auto entry = find(id))
            return entry;
    }
    const QList<DesktopEntry> candidates = forMimeType(mimeType);
    if (!candidates.isEmpty())
        return candidates.first();
    return std::nullopt;
}

QList<DesktopEntry> DesktopEntry::forMimeType(const QString &mimeType)
{
    QList<DesktopEntry> result;
    const QStringList defaults = defaultIds(mimeType);
    for (const QString &id : defaults) {
        if (auto entry = find(id))
            result.append(*entry);
    }
    static QList<DesktopEntry> cached;
    static qint64 cachedAt = 0;
    static QByteArray cachedCatalogStamp;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QByteArray catalogStamp = applicationCatalogStamp();
    if (cached.isEmpty() || now - cachedAt > 30 || cachedCatalogStamp != catalogStamp) {
        cached = all();
        cachedAt = now;
        cachedCatalogStamp = catalogStamp;
    }
    for (const DesktopEntry &entry : std::as_const(cached)) {
        if (entry.noDisplay || !entry.mimeTypes.contains(mimeType))
            continue;
        bool known = false;
        for (const DesktopEntry &r : std::as_const(result))
            known = known || r.id == entry.id;
        if (!known)
            result.append(entry);
    }
    return result;
}
