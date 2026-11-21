// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QRegularExpression>
#include <QVector>
#include <optional>
#include <algorithm>
#include <QStandardPaths>
#include <QTextStream>

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

namespace Config {

namespace {

QString sessionFile(bool legacy = false)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + (legacy ? QStringLiteral("/openkaryon/session.env") : QStringLiteral("/karyo/session.env"));
}

QString readPath()
{
    const QString current = sessionFile();
    return QFileInfo::exists(current) ? current : sessionFile(true);
}

std::optional<QString> assignmentKey(const QString &line)
{
    static const QRegularExpression prefix(QStringLiteral("^\\s*(?:export\\s+)?([A-Za-z_][A-Za-z0-9_]*)\\s*="));
    const auto match = prefix.match(line);
    return match.hasMatch() ? std::optional<QString>(match.captured(1)) : std::nullopt;
}

// Read literal shell quoting without executing expansions or commands. New
// assignments are single-quoted; legacy plain/double-quoted values still work.
std::optional<QString> literalValue(QString text)
{
    text = text.trimmed();
    QString result; QChar quote; bool escaped = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (escaped) {
            result += c; escaped = false;
        } else if (quote == QLatin1Char('\'')) {
            if (c == quote) quote = {}; else result += c;
        } else if (c == QLatin1Char('\\')) {
            escaped = true;
        } else if (!quote.isNull()) {
            if (c == quote) quote = {}; else result += c;
        } else if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            quote = c;
        } else if (c.isSpace()) {
            const QString rest = text.mid(i).trimmed();
            if (!rest.isEmpty() && !rest.startsWith(QLatin1Char('#')))
                return std::nullopt;
            break;
        } else {
            result += c;
        }
    }
    return quote.isNull() && !escaped ? std::optional<QString>(result) : std::nullopt;
}

} // namespace

QSettings desktop()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("desktop"));
}

QSettings shell()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("shell"));
}

QString sessionValue(const QString &key)
{
    QFile file(readPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QString result;
    for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        if (assignmentKey(line) != std::optional<QString>(key))
            continue;
        if (const auto value = literalValue(line.mid(line.indexOf(QLatin1Char('=')) + 1)))
            result = *value; // Shell assignment semantics: the last occurrence wins.
    }
    return result;
}

bool setSessionValue(const QString &key, const QString &value, QString *error)
{
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (error) error->clear();
    static const QRegularExpression validKey(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (!validKey.match(key).hasMatch() || value.contains(QChar::Null)
        || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')))
        return fail(QStringLiteral("Invalid session setting."));
    QStringList lines;
    QFile input(readPath());
    if (input.open(QIODevice::ReadOnly)) {
        for (const QString &line : QString::fromUtf8(input.readAll()).split(QLatin1Char('\n'))) {
            if (assignmentKey(line) != std::optional<QString>(key))
                lines.append(line);
        }
        if (input.error() != QFileDevice::NoError)
            return fail(input.errorString());
    } else if (input.exists()) {
        return fail(input.errorString());
    } else {
        lines.append(QStringLiteral("# Read by karyo-session at sign-in; written by Settings."));
    }
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    if (!value.isEmpty()) {
        QString escaped = value;
        escaped.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
        lines.append(key + QStringLiteral("='") + escaped + QLatin1Char('\''));
    }
    const QString path = sessionFile();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(QStringLiteral("Could not create the session configuration directory."));
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
        return fail(output.errorString());
    const QByteArray bytes = lines.join(QLatin1Char('\n')).toUtf8() + '\n';
    if (output.write(bytes) != bytes.size() || !output.commit())
        return fail(output.errorString());
    return true;
}

QString userLogin()
{
    const passwd *pw = getpwuid(getuid());
    return pw ? QString::fromLocal8Bit(pw->pw_name) : qEnvironmentVariable("USER");
}

QString userName()
{
    const passwd *pw = getpwuid(getuid());
    const QString gecos = pw ? QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed()
                             : QString();
    return gecos.isEmpty() ? userLogin() : gecos;
}

QString initials(const QString &name)
{
    QString out;
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &word : words.mid(0, 2))
        out += word.at(0).toUpper();
    return out;
}

bool isAdministrator()
{
    if (getuid() == 0)
        return true;
    const group *sudo = getgrnam("sudo");
    if (!sudo)
        return false;
    const gid_t adminGroup = sudo->gr_gid;
    if (getgid() == adminGroup)
        return true;
    const int count = getgroups(0, nullptr);
    if (count < 0)
        return false;
    QVector<gid_t> groups(count);
    const int loaded = getgroups(count, groups.data());
    if (loaded < 0)
        return false;
    return std::find(groups.cbegin(), groups.cbegin() + loaded, adminGroup) != groups.cbegin() + loaded;
}

} // namespace Config
