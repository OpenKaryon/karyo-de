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

void Karyo::Apps::showInFiles(const QString &path, QObject *context)
{
    if (path.isEmpty())
        return;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    message << QStringList{QUrl::fromLocalFile(absolute).toString()} << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message),
                                               context ? context : QCoreApplication::instance());
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [watcher, absolute] {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(absolute).absolutePath()));
        watcher->deleteLater();
    });
}
