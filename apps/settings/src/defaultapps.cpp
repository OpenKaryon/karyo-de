// SPDX-License-Identifier: GPL-3.0-or-later
#include "defaultapps.h"
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

bool DefaultApps::set(const QStringList &mimeTypes, const QString &desktopId, QString *error)
{
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (error) error->clear();
    static const QRegularExpression invalidId(QStringLiteral("[\\\\/;\\r\\n]"));
    static const QRegularExpression mime(QRegularExpression::anchoredPattern(QStringLiteral("[A-Za-z0-9!#$&^_.+-]+/[A-Za-z0-9!#$&^_.+-]+")));
    if (desktopId.isEmpty() || !desktopId.endsWith(QLatin1String(".desktop"))
        || desktopId.contains(QChar::Null) || invalidId.match(desktopId).hasMatch() || mimeTypes.isEmpty())
        return fail(QStringLiteral("Choose a valid application and MIME type."));
    QSet<QString> selected;
    for (const QString &type : mimeTypes) {
        if (!mime.match(type).hasMatch())
            return fail(QStringLiteral("Invalid MIME type."));
        selected.insert(type);
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/mimeapps.list");
    QStringList original;
    QFile input(path);
    if (input.open(QIODevice::ReadOnly)) {
        original = QString::fromUtf8(input.readAll()).split(QLatin1Char('\n'));
        if (input.error() != QFileDevice::NoError)
            return fail(input.errorString());
    } else if (input.exists()) {
        return fail(input.errorString());
    }
    QStringList lines;
    bool inDefaults = false;
    qsizetype insert = -1;
    for (const QString &line : original) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            inDefaults = trimmed == QLatin1String("[Default Applications]");
            lines.append(line);
            if (inDefaults && insert < 0) insert = lines.size();
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (inDefaults && equals >= 0 && selected.contains(line.left(equals).trimmed()))
            continue;
        lines.append(line);
    }
    if (insert < 0) {
        if (!lines.isEmpty() && !lines.last().isEmpty()) lines.append(QString());
        lines.append(QStringLiteral("[Default Applications]"));
        insert = lines.size();
    }
    QStringList seen;
    for (const QString &type : mimeTypes) {
        if (!seen.contains(type)) {
            lines.insert(insert++, type + QLatin1Char('=') + desktopId + QLatin1Char(';'));
            seen.append(type);
        }
    }
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(QStringLiteral("Could not create the application configuration directory."));
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
    const QByteArray data = lines.join(QLatin1Char('\n')).toUtf8() + '\n';
    if (output.write(data) != data.size() || !output.commit()) return fail(output.errorString());
    return true;
}
