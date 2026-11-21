// SPDX-License-Identifier: GPL-3.0-or-later
#include "inventory.h"
#include "signin.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <karyo/ui/desktopentry.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
namespace
{
QString tr(const char *text) { return QCoreApplication::translate("GreeterInventory", text); }
QString selectionPath(const QString &root)
{
    return (root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) : root)
        + QStringLiteral("/karyo/greeter.json");
}
bool validReference(const QString &text)
{
    return !text.isEmpty() && text.size() <= 256 && !text.contains(QChar::Null)
        && !text.contains(QLatin1Char('\n')) && !text.contains(QLatin1Char('\r'));
}
QByteArray smallFile(const QString &path, QString *error)
{
    const int fd
        = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
    {
        *error = QString::fromLocal8Bit(std::strerror(errno));
        return { };
    }
    struct stat info { };
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > 4096)
    {
        ::close(fd);
        *error = tr("The greeter preferences are not a small regular file.");
        return { };
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle))
    {
        ::close(fd);
        *error = file.errorString();
        return { };
    }
    const auto data = file.read(4097);
    if (file.error() != QFileDevice::NoError || data.size() > 4096)
    {
        *error = tr("Could not read the greeter preferences.");
        return { };
    }
    return data;
}
}
QStringList GreeterSession::environment() const
{
    QStringList result { QStringLiteral("XDG_SESSION_TYPE=wayland"),
        QStringLiteral("XDG_SESSION_DESKTOP=") + id };
    if (!desktopNames.isEmpty())
        result.append(QStringLiteral("XDG_CURRENT_DESKTOP=") + desktopNames);
    return result;
}
QList<GreeterUser> visibleGreeterUsers(
    const QList<GreeterUser> &users, const Karyo::Accounts::DesktopAccounts &accounts, QString *error)
{
    if (error)
        error->clear();
    if (!accounts.valid())
    {
        if (error)
            *error = tr("The desktop account configuration is invalid.");
        return { };
    }
    QList<GreeterUser> result;
    QStringList seen;
    for (const auto &user : users)
    {
        const auto shell = QFileInfo(user.shell).fileName();
        if (user.uid < 1000 || user.uid >= 60000 || user.login.isEmpty() || shell == QLatin1String("nologin")
            || shell == QLatin1String("false") || accounts.isChild(user.login) || seen.contains(user.login))
            continue;
        result.append(user);
        seen.append(user.login);
    }
    return result;
}
QList<GreeterUser> systemGreeterUsers(const Karyo::Accounts::DesktopAccounts &accounts, QString *error)
{
    QList<GreeterUser> users;
    setpwent();
    while (const passwd *entry = getpwent())
        users.append({ QString::fromLocal8Bit(entry->pw_name), SignIn::fullName(entry),
            QString::fromLocal8Bit(entry->pw_shell), quint32(entry->pw_uid) });
    endpwent();
    return visibleGreeterUsers(users, accounts, error);
}
QList<GreeterSession> greeterSessions(const QString &directory)
{
    QList<GreeterSession> result;
    const QDir dir(directory);
    for (const auto &name : dir.entryList({ QStringLiteral("*.desktop") }, QDir::Files, QDir::Name))
    {
        const auto path = dir.filePath(name);
        const auto entry = DesktopEntry::load(path, name);
        if (!entry || entry->noDisplay)
            continue;
        auto command = entry->command();
        if (command.isEmpty())
            continue;
        const auto executable = QStandardPaths::findExecutable(command.first());
        if (executable.isEmpty())
            continue;
        command[0] = executable;
        if (!entry->tryExec.isEmpty() && QStandardPaths::findExecutable(entry->tryExec).isEmpty())
            continue;
        const auto &desktops = entry->desktopNames;
        const QRegularExpression valid(QStringLiteral("\\A[A-Za-z0-9_.-]{1,64}\\z"));
        bool namesValid = true;
        for (const auto &desktop : desktops)
            namesValid = namesValid && valid.match(desktop).hasMatch();
        if (!namesValid)
            continue;
        result.append({ name.chopped(8), entry->name, command, desktops.join(QLatin1Char(':')) });
    }
    std::stable_sort(result.begin(), result.end(),
        [](const GreeterSession &a, const GreeterSession &b)
        {
            const auto rank = [](const QString &id)
            {
                return id == QLatin1String("karyo") ? 0 : id == QLatin1String("openkaryon") ? 1 : 2;
            };
            return rank(a.id) < rank(b.id);
        });
    return result;
}
GreeterSelection readGreeterSelection(const QString &configRoot, const QString &homeRoot)
{
    GreeterSelection result;
    const auto path = selectionPath(configRoot);
    const QFileInfo current(path);
    const bool primary = current.exists() || current.isSymLink();
    const auto legacy
        = (homeRoot.isEmpty() ? QDir::homePath() : homeRoot) + QStringLiteral("/openkaryon-greeter.conf");
    if (!primary && !QFileInfo::exists(legacy))
        return result;
    const auto data = smallFile(primary ? path : legacy, &result.error);
    if (!result.error.isEmpty())
        return result;
    if (primary)
    {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(data, &error);
        const auto object = document.object();
        if (error.error != QJsonParseError::NoError || !document.isObject()
            || object.value(QStringLiteral("schema")).toDouble() != 1
            || !object.value(QStringLiteral("lastUser")).isString()
            || !object.value(QStringLiteral("lastSession")).isString())
        {
            result.error = tr("The greeter preferences are invalid.");
            return result;
        }
        result.user = object.value(QStringLiteral("lastUser")).toString();
        result.session = object.value(QStringLiteral("lastSession")).toString();
    }
    else
    {
        // Parse a bounded snapshot, never re-open the mutable original path.
        QTemporaryFile copy;
        if (!copy.open() || copy.write(data) != data.size() || !copy.flush())
        {
            result.error = tr("Could not read the greeter preferences.");
            return result;
        }
        QSettings old(copy.fileName(), QSettings::IniFormat);
        result.user = old.value(QStringLiteral("lastUser")).toString();
        result.session = old.value(QStringLiteral("lastSession")).toString();
    }
    if (!validReference(result.user) || !validReference(result.session))
    {
        result.user.clear();
        result.session.clear();
        result.error = tr("The greeter preferences are invalid.");
    }
    return result;
}
bool saveGreeterSelection(const QString &user, const QString &session, QString *error, const QString &root)
{
    if (error)
        error->clear();
    const auto path = selectionPath(root);
    if (!validReference(user) || !validReference(session))
    {
        if (error)
            *error = tr("Choose a valid user and session.");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        if (error)
            *error = tr("Could not create the greeter preferences directory.");
        return false;
    }
    const auto data
        = QJsonDocument(QJsonObject { { QStringLiteral("schema"), 1 }, { QStringLiteral("lastUser"), user },
                            { QStringLiteral("lastSession"), session } })
              .toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(data) != data.size() || !file.commit())
    {
        if (error)
            *error = tr("Could not save the greeter preferences: %1").arg(file.errorString());
        return false;
    }
    return true;
}
