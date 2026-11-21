// SPDX-License-Identifier: GPL-3.0-or-later
#include "filesservice.h"
#include "fileswindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QUrl>
#include <QWindow>
#include <algorithm>
#include <utility>

namespace {

QStringList localPaths(const QStringList &uris)
{
    QStringList paths;
    for (const QString &uri : uris) {
        QUrl url(uri, QUrl::StrictMode);
        if (!url.isValid() || !url.isLocalFile()
            || (!url.host().isEmpty() && url.host() != QLatin1String("localhost")))
            continue;
        url.setHost(QString());
        const QString path = url.toLocalFile();
        if (QDir::isAbsolutePath(path) && !path.contains(QChar::Null))
            paths.append(QDir::cleanPath(path));
    }
    paths.removeDuplicates();
    return paths;
}

} // namespace

void FilesService::present(FilesWindow *window, const QString &activationToken)
{
    // OpenDirectory forwards the caller's Wayland activation token as the
    // FileManager1 startup id. A secondary CLI start carries it in its env.
    // Qt consumes it synchronously in activateWindow/requestActivate.
    const bool waylandToken = !activationToken.isEmpty()
        && QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
    const bool hadToken = qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN");
    const QByteArray previous = qgetenv("XDG_ACTIVATION_TOKEN");
    if (waylandToken)
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    window->setWindowState(window->windowState() & ~Qt::WindowMinimized);
    window->show();
    window->raise();
    window->activateWindow();
    if (waylandToken) {
        if (hadToken && previous != activationToken.toUtf8())
            qputenv("XDG_ACTIVATION_TOKEN", previous);
        else
            qunsetenv("XDG_ACTIVATION_TOKEN");
    }
}

FilesWindow *FilesService::openFolder(const QString &folder, const QStringList &items, bool newWindow, const QString &activationToken)
{
    if (!QFileInfo(folder).isDir())
        return nullptr;
    if (!newWindow) {
        auto windows = QApplication::topLevelWidgets();
        std::stable_sort(windows.begin(), windows.end(), [](QWidget *a, QWidget *b) {
            return a->isActiveWindow() && !b->isActiveWindow();
        });
        for (QWidget *widget : std::as_const(windows)) {
            auto *window = qobject_cast<FilesWindow *>(widget);
            if (window && window->isVisible() && window->revealFolder(folder, items)) {
                present(window, activationToken);
                return window;
            }
        }
    }
    auto *window = new FilesWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->openTab(folder);
    window->revealFolder(folder, items);
    present(window, activationToken);
    return window;
}

void FilesService::showItems(const QStringList &paths, const QString &activationToken)
{
    // Several items in one directory share one tab and one selection.
    QMap<QString, QStringList> folders;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QString parent = info.absolutePath();
        const QString canonical = QFileInfo(parent).canonicalFilePath();
        folders[canonical.isEmpty() ? parent : canonical].append(path);
    }
    for (auto it = folders.cbegin(); it != folders.cend(); ++it)
        openFolder(it.key(), it.value(), false, activationToken);
}

void FilesService::showFolders(const QStringList &paths, const QString &activationToken)
{
    for (const QString &path : paths)
        openFolder(path, {}, false, activationToken);
}

void FilesService::Open(const QStringList &paths, bool reveal, bool newWindow, const QString &activationToken)
{
    if (reveal && !newWindow) {
        showItems(paths, activationToken);
        return;
    }
    FilesWindow *created = nullptr;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        const bool select = reveal || !info.isDir();
        const QString folder = select ? info.absolutePath() : info.absoluteFilePath();
        const QStringList items = select ? QStringList{path} : QStringList{};
        if (newWindow && created) {
            created->openTab(folder);
            created->revealFolder(folder, items);
        } else {
            auto *window = openFolder(folder, items, newWindow, activationToken);
            if (newWindow)
                created = window;
        }
    }
}

void FileManagerService::ShowItems(const QStringList &uris, const QString &startupId)
{
    m_files->showItems(localPaths(uris), startupId);
}

void FileManagerService::ShowFolders(const QStringList &uris, const QString &startupId)
{
    m_files->showFolders(localPaths(uris), startupId);
}

void FileManagerService::ShowItemProperties(const QStringList &uris, const QString &startupId)
{
    // The selected files' properties are shown in Karyo's existing details pane.
    ShowItems(uris, startupId);
}
