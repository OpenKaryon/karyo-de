// SPDX-License-Identifier: GPL-3.0-or-later
#include "packages.h"
#include "command.h"
#include "softwarejob.h"

#include <karyo/ui/desktopentry.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>

namespace
{

QString shellQuote(QString text)
{
    text.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + text + QLatin1Char('\'');
}
bool validPackage(const CatalogApp &app)
{
    QString pattern;
    if (app.source == QLatin1String("apt"))
        pattern = QStringLiteral("[a-z0-9][a-z0-9+.-]{0,254}(?::[a-z0-9][a-z0-9-]{0,31})?");
    else if (app.source == QLatin1String("flatpak"))
        pattern = QStringLiteral("[A-Za-z0-9][A-Za-z0-9._-]{0,254}");
    else if (app.source == QLatin1String("bioconda") || app.source == QLatin1String("conda-forge"))
        pattern = QStringLiteral("[a-z0-9][a-z0-9_.-]{0,127}");
    else
        return false;
    return QRegularExpression(QRegularExpression::anchoredPattern(pattern)).match(app.package).hasMatch();
}

QString terminalProgram()
{
    const QString own = QStandardPaths::findExecutable(QStringLiteral("karyo-terminal"));
    return own.isEmpty() ? QStringLiteral("x-terminal-emulator") : own;
}

void terminal(const QString &commandLine)
{
    QProcess::startDetached(terminalProgram(),
        { QStringLiteral("-e"), QStringLiteral("sh"), QStringLiteral("-c"),
            commandLine + QStringLiteral("; exec \"${SHELL:-/bin/bash}\"") },
        QDir::homePath());
}

// "1:1.10.2+ds1-1+b5" -> "1.10.2"
QString upstreamVersion(const QString &debian)
{
    const auto match = QRegularExpression(QStringLiteral("^(?:\\d+:)?([0-9][^+~-]*)")).match(debian);
    return match.hasMatch() ? match.captured(1) : debian;
}

// "https://conda.anaconda.org/bioconda/linux-64" -> "bioconda"
QString channelName(const QString &channel)
{
    QString c = channel;
    if (c.contains(QLatin1String("://")))
        c = c.section(QLatin1Char('/'), 3, 3);
    return c.isEmpty() ? channel : c;
}

const QString Flathub = QStringLiteral("https://dl.flathub.org/repo/flathub.flatpakrepo");

} // namespace

PackageManager::PackageManager(QObject *parent)
    : QObject(parent)
{
    loadCatalog();
}

void PackageManager::loadCatalog()
{
    const auto data = readCatalog();
    m_catalog = data.apps;
    m_featured = data.featured;
    m_catalogError = data.error;
}
void PackageManager::runQuery(
    const QString &program, const QStringList &args, const std::function<void(const QString &)> &done)
{
    Karyo::Apps::runCommand(
        this, program, args, done, [this](const QString &error) { Q_EMIT queryFailed(error); }, 60000);
}

QList<CatalogApp> PackageManager::inCategory(const QString &category) const
{
    QList<CatalogApp> out;
    for (const CatalogApp &app : m_catalog)
    {
        if (app.category == category && isAvailable(app))
            out.append(app);
    }
    return out;
}

CatalogApp PackageManager::featured() const
{
    for (const QString &id : m_featured)
    {
        for (const CatalogApp &app : m_catalog)
        {
            if (app.id == id && isAvailable(app)
                && (app.source == QLatin1String("apt") || !condaRoot().isEmpty()))
                return app;
        }
    }
    return m_catalog.value(0);
}

QList<CatalogApp> PackageManager::searchCatalog(const QString &query) const
{
    QList<CatalogApp> out;
    for (const CatalogApp &app : m_catalog)
    {
        if (!isAvailable(app))
            continue;
        if (app.name.contains(query, Qt::CaseInsensitive) || app.package.contains(query, Qt::CaseInsensitive)
            || app.summary.contains(query, Qt::CaseInsensitive)
            || app.command.contains(query, Qt::CaseInsensitive))
            out.append(app);
    }
    return out;
}

void PackageManager::refresh()
{
    const auto generation = ++m_refreshGeneration;
    runQuery(QStringLiteral("dpkg-query"),
        { QStringLiteral("-W"),
            QStringLiteral("-f=${binary:Package}\\t${db:Status-Abbrev}\\t${Version}\\n") },
        [this, generation](const QString &out)
        {
            if (generation != m_refreshGeneration)
                return;
            m_installed.clear();
            for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            {
                const QStringList f = line.split(QLatin1Char('\t'));
                if (f.size() >= 3 && f.at(1).startsWith(QLatin1String("ii")))
                {
                    m_installed.insert(f.at(0), f.at(2));
                    const auto base = f.at(0).section(QLatin1Char(':'), 0, 0);
                    if (!m_installed.contains(base))
                        m_installed.insert(base, f.at(2));
                }
            }
            Q_EMIT changed();
        });

    QStringList packages;
    for (const CatalogApp &app : std::as_const(m_catalog))
    {
        if (app.source == QLatin1String("apt"))
            packages.append(app.package);
    }
    if (!packages.isEmpty())
        runQuery(QStringLiteral("apt-cache"), QStringList { QStringLiteral("policy") } + packages,
            [this, generation](const QString &out)
            {
                if (generation != m_refreshGeneration)
                    return;
                m_candidates.clear();
                m_candidatesLoaded = true;
                QString current;
                for (const QString &line : out.split(QLatin1Char('\n')))
                {
                    if (!line.startsWith(QLatin1Char(' ')) && line.endsWith(QLatin1Char(':')))
                    {
                        current = line.chopped(1);
                    }
                    else if (line.trimmed().startsWith(QLatin1String("Candidate:")) && !current.isEmpty())
                    {
                        const QString candidate
                            = line.trimmed().mid(QStringLiteral("Candidate:").size()).trimmed();
                        if (candidate != QLatin1String("(none)"))
                            m_candidates.insert(current, candidate);
                    }
                }
                Q_EMIT changed();
            });

    if (hasFlatpak())
    {
        runQuery(QStringLiteral("flatpak"),
            { QStringLiteral("list"), QStringLiteral("--user"), QStringLiteral("--app"),
                QStringLiteral("--columns=application") },
            [this, generation](const QString &out)
            {
                if (generation != m_refreshGeneration)
                    return;
                m_flatpaks.clear();
                for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                    m_flatpaks.insert(line.trimmed());
                Q_EMIT changed();
            });
    }
}

bool PackageManager::isInstalled(const CatalogApp &app) const
{
    if (app.source == QLatin1String("apt"))
        return m_installed.contains(app.package);
    if (app.source == QLatin1String("flatpak"))
        return m_flatpaks.contains(app.package);
    for (const CondaEnv &env : condaEnvs())
    {
        if (!QDir(env.path + QStringLiteral("/conda-meta"))
                .entryList({ app.package + QStringLiteral("-[0-9]*.json") }, QDir::Files)
                .isEmpty())
            return true;
    }
    return false;
}

bool PackageManager::isAvailable(const CatalogApp &app) const
{
    if (app.source == QLatin1String("apt"))
        return !app.fromCatalog || m_candidates.contains(app.package) || m_installed.contains(app.package)
            || !m_candidatesLoaded;
    if (app.source == QLatin1String("flatpak"))
        return hasFlatpak();
    // conda channels: App Center can set up Miniforge first.
    return true;
}

QString PackageManager::version(const CatalogApp &app) const
{
    if (app.source == QLatin1String("apt"))
    {
        const QString v = m_installed.value(app.package, m_candidates.value(app.package));
        return upstreamVersion(v);
    }
    if (app.source != QLatin1String("flatpak"))
    {
        for (const CondaEnv &env : condaEnvs())
        {
            for (const CondaPackage &p : condaPackages(env.path))
            {
                if (p.name == app.package)
                    return p.version;
            }
        }
    }
    return { };
}

bool PackageManager::canStart(const CatalogApp &app)
{
    if (m_jobs.contains(jobKey(app)))
        return false;
    if (app.source == QLatin1String("apt"))
        for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it)
            if (it.key().startsWith(QLatin1String("apt:")))
            {
                Q_EMIT jobFailed(app, tr("Another package operation is still running."));
                return false;
            }
    return true;
}
void PackageManager::runJob(const CatalogApp &app, const QString &program, const QStringList &args)
{
    if (canStart(app))
        trackJob(app, Karyo::Apps::SoftwareJob::start(program, args));
}
void PackageManager::runAptJob(const CatalogApp &app, const QString &operation)
{
    if (!canStart(app))
        return;
    const QStringList packages
        = (operation == QLatin1String("install") || operation == QLatin1String("remove"))
        ? QStringList { app.package }
        : QStringList { };
    trackJob(app, Karyo::Apps::SoftwareJob::startApt(operation, packages));
}
void PackageManager::trackJob(const CatalogApp &app, Karyo::Apps::SoftwareJob *job)
{
    const QString key = jobKey(app);
    m_jobs.insert(key, 0);
    Q_EMIT jobChanged(app);
    connect(job, &Karyo::Apps::SoftwareJob::progress, this,
        [this, app, key](int percent)
        {
            m_jobs[key] = percent;
            Q_EMIT jobChanged(app);
        });
    connect(job, &Karyo::Apps::SoftwareJob::finished, this,
        [this, app, key](bool success, const QString &error)
        {
            m_jobs.remove(key);
            if (!success)
                Q_EMIT jobFailed(app, error);
            Q_EMIT jobChanged(app);
            refresh();
            checkUpdates();
        });
}

void PackageManager::install(const CatalogApp &app, const QString &condaEnvPath)
{
    if (!validPackage(app))
    {
        Q_EMIT jobFailed(app, tr("Invalid software source or package name."));
        return;
    }
    if (app.source == QLatin1String("apt"))
    {
        runAptJob(app, QStringLiteral("install"));
    }
    else if (app.source == QLatin1String("flatpak"))
    {
        // Per-user: no administrator needed. Flathub is added on first use.
        runJob(app, QStringLiteral("sh"),
            { QStringLiteral("-c"),
                QStringLiteral("flatpak remote-add --user --if-not-exists flathub %1 && "
                               "flatpak install --user -y --noninteractive flathub \"$0\"")
                    .arg(Flathub),
                app.package });
    }
    else
    {
        if (condaRoot().isEmpty())
        {
            Q_EMIT jobFailed(app, tr("Set up conda first: Environments > Set up Miniforge."));
            return;
        }
        QStringList args { QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("-p"),
            condaEnvPath.isEmpty() ? condaRoot() : condaEnvPath };
        if (app.source == QLatin1String("bioconda"))
            args << QStringLiteral("-c") << QStringLiteral("bioconda");
        args << QStringLiteral("-c") << QStringLiteral("conda-forge") << app.package;
        runJob(app, condaTool(), args);
    }
}

void PackageManager::remove(const CatalogApp &app)
{
    if (!validPackage(app))
    {
        Q_EMIT jobFailed(app, tr("Invalid software source or package name."));
        return;
    }
    if (app.source == QLatin1String("apt"))
    {
        runAptJob(app, QStringLiteral("remove"));
    }
    else if (app.source == QLatin1String("flatpak"))
    {
        runJob(app, QStringLiteral("flatpak"),
            { QStringLiteral("uninstall"), QStringLiteral("--user"), QStringLiteral("-y"),
                QStringLiteral("--noninteractive"), app.package });
    }
    else
    {
        for (const CondaEnv &env : condaEnvs())
        {
            if (!QDir(env.path + QStringLiteral("/conda-meta"))
                    .entryList({ app.package + QStringLiteral("-[0-9]*.json") }, QDir::Files)
                    .isEmpty())
            {
                runJob(app, condaTool(),
                    { QStringLiteral("remove"), QStringLiteral("-y"), QStringLiteral("-p"), env.path,
                        app.package });
                return;
            }
        }
    }
}

void PackageManager::open(const CatalogApp &app)
{
    if (!validPackage(app))
    {
        Q_EMIT jobFailed(app, tr("Invalid software source or package name."));
        return;
    }
    if (app.source == QLatin1String("flatpak"))
    {
        QProcess::startDetached(QStringLiteral("flatpak"), { QStringLiteral("run"), app.package });
        return;
    }
    if (app.kind == QLatin1String("gui") && !app.desktop.isEmpty())
    {
        if (const auto entry = DesktopEntry::find(app.desktop))
        {
            entry->launch();
            return;
        }
    }
    const QString command = app.command.isEmpty() ? app.package : app.command;
    if (!QRegularExpression(
            QRegularExpression::anchoredPattern(QStringLiteral("[A-Za-z0-9][A-Za-z0-9_.+-]{0,127}")))
            .match(command)
            .hasMatch())
    {
        Q_EMIT queryFailed(tr("The application command is not an executable name."));
        return;
    }
    if (app.source == QLatin1String("apt") || app.source.isEmpty())
    {
        terminal(shellQuote(command) + QStringLiteral(" --help"));
        return;
    }
    for (const CondaEnv &env : condaEnvs())
    {
        if (!QDir(env.path + QStringLiteral("/conda-meta"))
                .entryList({ app.package + QStringLiteral("-[0-9]*.json") }, QDir::Files)
                .isEmpty())
        {
            openEnvTerminal(env.path, shellQuote(command) + QStringLiteral(" --help"));
            return;
        }
    }
}

void PackageManager::searchApt(const QString &query)
{
    runQuery(QStringLiteral("apt-cache"), { QStringLiteral("search"), QStringLiteral("--"), query },
        [this, query](const QString &out)
        {
            QList<CatalogApp> apps;
            for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            {
                const QString package = line.section(QStringLiteral(" - "), 0, 0).trimmed();
                CatalogApp app;
                app.id = package;
                app.name = package;
                app.tile = package.left(3);
                app.source = QStringLiteral("apt");
                app.package = package;
                app.kind = QStringLiteral("cli");
                app.command = package;
                app.summary = line.section(QStringLiteral(" - "), 1).trimmed();
                app.fromCatalog = false;
                apps.append(app);
                if (apps.size() >= 60)
                    break;
            }
            // Names that start with the query first.
            std::stable_sort(apps.begin(), apps.end(),
                [&query](const CatalogApp &a, const CatalogApp &b)
                {
                    return a.package.startsWith(query, Qt::CaseInsensitive)
                        && !b.package.startsWith(query, Qt::CaseInsensitive);
                });
            Q_EMIT aptResults(query, apps);
        });
}

bool PackageManager::hasFlatpak() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty();
}

void PackageManager::searchFlatpak(const QString &query)
{
    if (!hasFlatpak())
        return;
    runQuery(QStringLiteral("flatpak"),
        { QStringLiteral("search"), QStringLiteral("--columns=application,name,description,version"),
            QStringLiteral("--"), query },
        [this, query](const QString &out)
        {
            QList<CatalogApp> apps;
            for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            {
                const QStringList f = line.split(QLatin1Char('\t'));
                if (f.size() < 3 || !f.at(0).contains(QLatin1Char('.')))
                    continue;
                CatalogApp app;
                app.id = f.at(0);
                app.package = f.at(0);
                app.name = f.at(1);
                app.tile = f.at(1).left(2);
                app.summary = f.at(2);
                app.source = QStringLiteral("flatpak");
                app.kind = QStringLiteral("gui");
                app.fromCatalog = false;
                apps.append(app);
                if (apps.size() >= 24)
                    break;
            }
            Q_EMIT flatpakResults(query, apps);
        });
}

void PackageManager::checkUpdates()
{
    // A simulation needs no root and no lock.
    runQuery(QStringLiteral("apt-get"),
        { QStringLiteral("-s"), QStringLiteral("-o"), QStringLiteral("Debug::NoLocking=1"),
            QStringLiteral("dist-upgrade") },
        [this](const QString &out)
        {
            m_updates.clear();
            const QRegularExpression inst(QStringLiteral("^Inst (\\S+) (?:\\[([^\\]]+)\\] )?\\((\\S+)"));
            for (const QString &line : out.split(QLatin1Char('\n')))
            {
                const auto match = inst.match(line);
                if (match.hasMatch())
                    m_updates.append({ match.captured(1), upstreamVersion(match.captured(2)),
                        upstreamVersion(match.captured(3)) });
            }
            Q_EMIT updatesChanged();
        });
}

void PackageManager::updateAll()
{
    CatalogApp system;
    system.id = QStringLiteral("system");
    system.source = QStringLiteral("apt");
    system.package = QStringLiteral("*");
    runAptJob(system, QStringLiteral("upgrade"));
}

void PackageManager::refreshSources()
{
    CatalogApp lists;
    lists.id = QStringLiteral("lists");
    lists.source = QStringLiteral("apt");
    lists.package = QStringLiteral("lists");
    runAptJob(lists, QStringLiteral("update"));
}

QString PackageManager::condaRoot() const
{
    const QString home = QDir::homePath();
    for (const QString &candidate :
        { home + QStringLiteral("/miniforge3"), home + QStringLiteral("/mambaforge"),
            home + QStringLiteral("/miniconda3"), home + QStringLiteral("/anaconda3"),
            QStringLiteral("/opt/conda"), QStringLiteral("/opt/miniforge3") })
    {
        if (QFileInfo::exists(candidate + QStringLiteral("/bin/conda")))
            return candidate;
    }
    return { };
}

QString PackageManager::condaTool() const
{
    const QString root = condaRoot();
    const QString mamba = root + QStringLiteral("/bin/mamba");
    return QFileInfo::exists(mamba) ? mamba : root + QStringLiteral("/bin/conda");
}

QList<CondaEnv> PackageManager::condaEnvs() const
{
    const QString root = condaRoot();
    if (root.isEmpty())
        return { };
    QStringList paths { root };
    for (const QString &dir :
        QDir(root + QStringLiteral("/envs")).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        paths.append(root + QStringLiteral("/envs/") + dir);
    QFile listed(QDir::homePath() + QStringLiteral("/.conda/environments.txt"));
    if (listed.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        for (const QString &line :
            QString::fromUtf8(listed.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        {
            const QString path = QDir::cleanPath(line.trimmed());
            if (!paths.contains(path) && QFileInfo::exists(path + QStringLiteral("/conda-meta")))
                paths.append(path);
        }
    }
    QList<CondaEnv> envs;
    for (const QString &path : std::as_const(paths))
    {
        CondaEnv env;
        env.path = path;
        env.name = path == root ? QStringLiteral("base") : QFileInfo(path).fileName();
        const QStringList metas
            = QDir(path + QStringLiteral("/conda-meta")).entryList({ QStringLiteral("*.json") }, QDir::Files);
        env.packages = int(metas.size());
        for (const QString &meta : metas)
        {
            const auto python
                = QRegularExpression(QStringLiteral("^python-(\\d+\\.\\d+)[.\\d]*-")).match(meta);
            if (python.hasMatch())
                env.python = QStringLiteral("Python ") + python.captured(1);
            const auto r = QRegularExpression(QStringLiteral("^r-base-(\\d+\\.\\d+)[.\\d]*-")).match(meta);
            if (r.hasMatch())
                env.python = QStringLiteral("R ") + r.captured(1)
                    + (env.python.isEmpty() ? QString() : QStringLiteral(" · ") + env.python);
        }
        env.size = m_envSizes.value(path, -1);
        envs.append(env);
    }
    return envs;
}

QList<CondaPackage> PackageManager::condaPackages(const QString &envPath) const
{
    QList<CondaPackage> packages;
    const QDir meta(envPath + QStringLiteral("/conda-meta"));
    for (const QString &name : meta.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name))
    {
        QFile file(meta.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        CondaPackage p;
        p.name = o.value(QStringLiteral("name")).toString();
        p.version = o.value(QStringLiteral("version")).toString();
        p.channel = channelName(o.value(QStringLiteral("channel")).toString());
        p.size = qint64(o.value(QStringLiteral("size")).toDouble());
        p.requested = !o.value(QStringLiteral("requested_spec")).toString().isEmpty();
        packages.append(p);
    }
    return packages;
}

void PackageManager::measureEnv(const QString &envPath)
{
    runQuery(QStringLiteral("du"), { QStringLiteral("-sb"), QStringLiteral("--"), envPath },
        [this, envPath](const QString &out)
        {
            bool valid = false;
            const qint64 size = out.section(QLatin1Char('\t'), 0, 0).trimmed().toLongLong(&valid);
            if (!valid || size < 0)
            {
                Q_EMIT queryFailed(tr("Could not measure the environment size."));
                return;
            }
            m_envSizes.insert(envPath, size);
            Q_EMIT envSizeKnown(envPath, size);
        });
}

void PackageManager::openEnvTerminal(const QString &envPath, const QString &command)
{
    // An rcfile keeps the user's own ~/.bashrc and adds the activation.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString rc = dir + QStringLiteral("/env-")
        + QString::fromLatin1(
            QCryptographicHash::hash(envPath.toUtf8(), QCryptographicHash::Md5).toHex().left(12))
        + QStringLiteral(".bashrc");
    QSaveFile file(rc);
    QString script = QStringLiteral("[ -f \"$HOME/.bashrc\" ] && . \"$HOME/.bashrc\"\n. ")
        + shellQuote(condaRoot() + QStringLiteral("/etc/profile.d/conda.sh")) + QLatin1Char('\n')
        + QStringLiteral("conda activate ") + shellQuote(envPath) + QLatin1Char('\n');
    if (!command.isEmpty())
        script += command + QLatin1Char('\n');
    const auto bytes = script.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit())
    {
        Q_EMIT queryFailed(tr("Could not prepare the environment terminal: %1").arg(file.errorString()));
        return;
    }
    if (!QProcess::startDetached(terminalProgram(),
            { QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("--rcfile"), rc },
            QDir::homePath()))
        Q_EMIT queryFailed(tr("Could not start a terminal."));
}

QString PackageManager::installMiniforgeCommand() const
{
    // Explicit user-requested bootstrap, never run by discovery or with root.
    // HTTPS release artifacts stay in a private temporary directory; verify the
    // published SHA256 before executing the installer as the current user.
    return QString::fromUtf8(R"KARYO(set -eu
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
trap 'exit 1' HUP INT TERM
cd "$work"
arch=$(uname -m)
case "$arch" in x86_64|aarch64|ppc64le) ;; *) echo 'Unsupported Miniforge architecture' >&2; exit 2 ;; esac
release=$(curl --proto '=https' --tlsv1.2 -fLs -o /dev/null -w '%{url_effective}' https://github.com/conda-forge/miniforge/releases/latest)
case "$release" in https://github.com/conda-forge/miniforge/releases/tag/*) ;; *) echo 'Unexpected release URL' >&2; exit 2 ;; esac
tag=${release##*/}
case "$tag" in ''|*[!a-zA-Z0-9._-]*) echo 'Invalid release tag' >&2; exit 2 ;; esac
url="https://github.com/conda-forge/miniforge/releases/download/$tag/Miniforge3-$tag-Linux-$arch.sh"
curl --proto '=https' --tlsv1.2 -fL -o installer.sh "$url"
curl --proto '=https' --tlsv1.2 -fL -o installer.sha256 "$url.sha256"
expected=$(awk '{print $1; exit}' installer.sha256)
case "$expected" in ''|*[!a-fA-F0-9]*) echo 'Invalid installer checksum' >&2; exit 2 ;; esac
[ "${#expected}" -eq 64 ] || exit 2
printf '%s  installer.sh\n' "$expected" | sha256sum -c -
bash installer.sh -b -p "$HOME/miniforge3"
"$HOME/miniforge3/bin/conda" config --add channels bioconda
"$HOME/miniforge3/bin/conda" config --add channels conda-forge
"$HOME/miniforge3/bin/conda" config --set channel_priority strict
"$HOME/miniforge3/bin/conda" init bash
printf '\nMiniforge is ready. Reopen App Center to see the environments.\n'
)KARYO");
}

QStringList PackageManager::sources() const
{
    QStringList list { QSysInfo::prettyProductName() };
    if (hasFlatpak())
        list.append(QStringLiteral("Flathub"));
    if (!condaRoot().isEmpty())
        list << QStringLiteral("bioconda") << QStringLiteral("conda-forge");
    return list;
}
