// SPDX-License-Identifier: GPL-3.0-or-later
#include "filemanager.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

namespace {
void request(const QString &path, bool folder, QObject *context)
{
    if (path.isEmpty())
        return;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"),
        folder ? QStringLiteral("ShowFolders") : QStringLiteral("ShowItems"));
    message << QStringList{QUrl::fromLocalFile(absolute).toString()} << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message),
                                               context ? context : QCoreApplication::instance());
    const QString fallback = folder ? absolute : QFileInfo(absolute).absolutePath();
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [watcher, fallback] {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError())
            QDesktopServices::openUrl(QUrl::fromLocalFile(fallback));
        watcher->deleteLater();
    });
}

} // namespace
void Karyo::Apps::showInFiles(const QString &path, QObject *context) { request(path, false, context); }
void Karyo::Apps::openFolder(const QString &path, QObject *context) { request(path, true, context); }
