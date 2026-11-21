// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearance.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
bool validTheme(const QString &value) { return QStringList { "light", "dark", "auto" }.contains(value); }
bool validAccent(const QString &value)
{
    return QRegularExpression(QStringLiteral("\\A[a-z0-9-]{1,24}\\z")).match(value).hasMatch();
}
}
GreeterAppearance readGreeterAppearance(
    const QString &login, quint32 uid, const QString &directory, const QString &legacyDirectory)
{
    GreeterAppearance result;
    const QRegularExpression account(QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_.-]{0,127}\\$?\\z"));
    if (!account.match(login).hasMatch())
        return result;
    const auto name = login + QStringLiteral(".conf");
    const auto current = QDir(directory).filePath(name);
    const QFileInfo selected(current);
    const auto path
        = selected.exists() || selected.isSymLink() ? current : QDir(legacyDirectory).filePath(name);
    const int fd
        = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return result;
    struct stat info { };
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != uid || info.st_size < 0
        || info.st_size > 4096)
    {
        ::close(fd);
        return result;
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle))
    {
        ::close(fd);
        return result;
    }
    const auto bytes = file.read(4097);
    if (file.error() != QFileDevice::NoError || bytes.size() > 4096)
        return result;
    // A tiny allowlisted INI subset, not QSettings QVariant deserialization.
    // Validate the exact descriptor we read; never stat then re-open a path.
    bool appearance = false;
    for (const auto &line : QString::fromUtf8(bytes).split(QLatin1Char('\n')))
    {
        const auto text = line.trimmed();
        if (text.startsWith(QLatin1Char('[')))
        {
            appearance = text == QLatin1String("[Appearance]");
            continue;
        }
        if (!appearance || text.startsWith(QLatin1Char('#')) || text.startsWith(QLatin1Char(';')))
            continue;
        const auto equals = text.indexOf(QLatin1Char('='));
        if (equals < 1)
            continue;
        const auto key = text.left(equals).trimmed();
        auto value = text.mid(equals + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        if (key == QLatin1String("theme"))
            result.theme = validTheme(value) ? value : QString();
        else if (key == QLatin1String("accent"))
            result.accent = validAccent(value) ? value : QString();
    }
    result.accepted = true;
    return result;
}
bool applyGreeterAppearance(const GreeterAppearance &appearance, const QString &settingsPath)
{
    // The shared theme preserves its historical preference namespace. These are
    // the greeter account's own settings, not the selected user's home files.
    QSettings own = settingsPath.isEmpty() ? QSettings(QSettings::NativeFormat, QSettings::UserScope,
                                                 QStringLiteral("openkaryon"), QStringLiteral("desktop"))
                                           : QSettings(settingsPath, QSettings::IniFormat);
    if (appearance.accepted && validTheme(appearance.theme))
        own.setValue(QStringLiteral("Appearance/theme"), appearance.theme);
    else
        own.remove(QStringLiteral("Appearance/theme"));
    if (appearance.accepted && validAccent(appearance.accent))
        own.setValue(QStringLiteral("Appearance/accent"), appearance.accent);
    else
        own.remove(QStringLiteral("Appearance/accent"));
    own.sync();
    return own.status() == QSettings::NoError;
}
