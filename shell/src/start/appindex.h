// SPDX-License-Identifier: GPL-3.0-or-later
// What Start shows and searches: installed apps by category, the command-line
// tools of App Center's catalog (installed or not), the pinned apps, settings
// pages, commands on $PATH, recent files and files in the home folder.
#pragma once

#include <karyo/ui/appstyle.h>
#include <karyo/ui/desktopentry.h>

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QStringList>

struct AppInfo
{
    DesktopEntry entry;
    AppStyle style;
    QString category;  // key of one of AppIndex::categories()
    // Command-line tools of App Center's catalog (entry.terminal is set,
    // entry.exec is the command).
    QString toolId;           // catalog id; empty for desktop apps
    QString source;           // apt, bioconda...
    QStringList envs;         // conda environments that have the command
    QString condaRoot;        // where those environments live
    bool installed = true;    // on $PATH or in an environment
    QStringList subcommands;  // "view", "sort"... for the Commands results

    bool isTool() const { return !toolId.isEmpty(); }
};

struct Category
{
    QString key;
    QString glyph;
    QString label;
    int count = 0;
};

struct FileHit
{
    QString path;
    QDateTime modified;
    qint64 size = 0;
    bool directory = false;
};

class AppIndex : public QObject
{
    Q_OBJECT
public:
    explicit AppIndex(QObject *parent = nullptr);

    // Rereads the desktop entries when they are older than a few seconds.
    void refresh();

    const QList<AppInfo> &apps() const { return m_apps; }
    // Settings pages (lxqt-config modules and the like), searched separately.
    const QList<AppInfo> &settings() const { return m_settings; }
    // The categories that hold installed apps, "all" first; or those that
    // hold catalog tools which are not installed.
    QList<Category> categories(bool installed) const;
    // "all" lists every installed app.
    QList<AppInfo> appsIn(const QString &category, bool installed) const;
    // Installed apps among the pinned ids, in pinned order.
    QList<AppInfo> pinnedApps() const;
    bool isPinned(const QString &id) const;
    void setPinned(const QString &id, bool pinned);
    // Start's pinned grid after a drag: these ids first, in this order.
    void setPinnedOrder(const QStringList &ids);
    // Apps whose windows opened lately, newest first: the taskbar notes each
    // app as its window appears; Start's search lists them before typing.
    void noteUsed(const QString &id);
    QList<AppInfo> recentApps(int limit) const;
    // Installed apps whose Terminal key is true count as command-line tools.
    int commandLineCount() const;
    int installedCount() const;

    // Executables on $PATH whose name starts with prefix, shortest first.
    QStringList commands(const QString &prefix, int limit) const;
    // Full path of a command on $PATH, empty if there is none.
    static QString commandPath(const QString &name);
    // Files from recently-used.xbel, newest first, that still exist.
    QList<FileHit> recentFiles(int limit) const;

    // Searches names in the home folder in a worker thread; filesFound()
    // reports the hits for the latest query only.
    void searchFiles(const QString &query);

    static QString labelFor(const QString &categoryKey);

Q_SIGNALS:
    void changed();
    void filesFound(const QString &query, const QList<FileHit> &hits);

private:
    QStringList pinnedIds() const;
    void loadTools();

    QList<AppInfo> m_apps;
    QList<AppInfo> m_settings;
    qint64 m_loadedAt = 0;
    mutable QStringList m_commandCache;
    mutable qint64 m_commandsAt = 0;
    quint64 m_fileQuery = 0;
};
