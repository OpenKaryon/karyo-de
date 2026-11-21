// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal freedesktop Desktop Entry reader: enough to name, draw and launch apps.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class DesktopEntry
{
public:
    QString id;       // e.g. "pcmanfm-qt.desktop"
    QString path;
    QString name;     // localized
    QString genericName;
    QString comment;
    QString icon;
    QString exec;
    QString startupWmClass;
    QStringList categories;
    QStringList keywords;
    QStringList mimeTypes;
    bool noDisplay = false;
    bool terminal = false;
    // Session discovery also needs these standard Desktop Entry fields.
    QString tryExec;
    QStringList desktopNames;

    static std::optional<DesktopEntry> load(const QString &path, const QString &id);
    // Resolves an id against $XDG_DATA_HOME and $XDG_DATA_DIRS/applications.
    static std::optional<DesktopEntry> find(const QString &id);
    // Finds the entry a Wayland app_id belongs to (id, reverse-DNS tail or StartupWMClass).
    static std::optional<DesktopEntry> forAppId(const QString &appId);
    static QList<DesktopEntry> all();
    static QStringList applicationDirs();

    // True if a window with this app_id belongs to this entry.
    bool matchesAppId(const QString &appId) const;
    // Exec with field codes removed, split into program and arguments.
    QStringList command() const;
    bool launch() const;
    // Launches with files: %f/%F/%u/%U receive the paths or URLs.
    bool launchWith(const QStringList &paths, qint64 *pid = nullptr) const;
    // Applications that declare the MIME type, the default (mimeapps.list) first.
    static QList<DesktopEntry> forMimeType(const QString &mimeType);
    static std::optional<DesktopEntry> defaultFor(const QString &mimeType);
};
