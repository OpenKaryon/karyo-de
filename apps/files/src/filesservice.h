// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>

class FilesWindow;

class FilesService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.openkaryon.Files")
public:
    explicit FilesService(QObject *parent = nullptr) : QObject(parent) {}
    void showItems(const QStringList &paths, const QString &activationToken = {});
    void showFolders(const QStringList &paths, const QString &activationToken = {});

public Q_SLOTS:
    Q_SCRIPTABLE void Open(const QStringList &paths, bool reveal, bool newWindow, const QString &activationToken);

private:
    void present(FilesWindow *window, const QString &activationToken);
    FilesWindow *openFolder(const QString &folder, const QStringList &items = {}, bool newWindow = false, const QString &activationToken = {});
};

// Standard desktop API used by Electron's shell.showItemInFolder and browsers.
class FileManagerService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
    explicit FileManagerService(FilesService *files) : QObject(files), m_files(files) {}

public Q_SLOTS:
    Q_SCRIPTABLE void ShowItems(const QStringList &uris, const QString &startupId);
    Q_SCRIPTABLE void ShowFolders(const QStringList &uris, const QString &startupId);
    Q_SCRIPTABLE void ShowItemProperties(const QStringList &uris, const QString &startupId);

private:
    FilesService *m_files;
};
