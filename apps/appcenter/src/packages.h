// SPDX-License-Identifier: GPL-3.0-or-later
// What App Center installs, and how: the curated catalog, Debian packages
// (apt through pkexec, so polkit asks for an administrator), Flatpak apps
// for the user, and conda environments (Miniforge in the home folder).
#pragma once

#include "catalog.h"
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <functional>

namespace Karyo::Apps
{
class SoftwareJob;
}

struct Upgrade
{
    QString package;
    QString from;
    QString to;
};

struct CondaEnv
{
    QString name;
    QString path;
    QString python; // "Python 3.12" or "R 4.4"
    int packages = 0;
    qint64 size = -1; // bytes, -1 while unknown
};

struct CondaPackage
{
    QString name;
    QString version;
    QString channel;
    qint64 size = 0;
    bool requested = false; // explicitly installed
};

class PackageManager : public QObject
{
    Q_OBJECT
public:
    explicit PackageManager(QObject *parent = nullptr);

    QString catalogError() const { return m_catalogError; }
    const QList<CatalogApp> &catalog() const { return m_catalog; }
    QList<CatalogApp> inCategory(const QString &category) const;
    // The first featured app whose source this system has.
    CatalogApp featured() const;
    QList<CatalogApp> searchCatalog(const QString &query) const;

    // Rereads installed packages and candidate versions.
    void refresh();
    bool isInstalled(const CatalogApp &app) const;
    // The source exists here (an apt candidate, conda, flatpak).
    bool isAvailable(const CatalogApp &app) const;
    QString version(const CatalogApp &app) const;
    bool isBusy(const CatalogApp &app) const { return m_jobs.contains(jobKey(app)); }
    int progress(const CatalogApp &app) const { return m_jobs.value(jobKey(app), -1); }

    void install(const CatalogApp &app, const QString &condaEnvPath = { });
    void remove(const CatalogApp &app);
    void open(const CatalogApp &app);

    // Debian packages matching query (apt-cache search), as plain entries.
    void searchApt(const QString &query);
    // Flathub apps matching query, when flatpak is installed.
    void searchFlatpak(const QString &query);
    bool hasFlatpak() const;

    // Upgradable packages from apt's cached lists (no root needed).
    void checkUpdates();
    bool updatesKnown() const { return m_updatesKnown; }
    bool checkingUpdates() const { return m_checkingUpdates; }
    QString updatesError() const { return m_updatesError; }
    const QList<Upgrade> &updates() const { return m_updates; }
    void updateAll();
    // apt-get update through pkexec, then checkUpdates().
    void refreshSources();

    // conda (Miniforge): the install root, or empty.
    QString condaRoot() const;
    QString condaTool() const; // mamba if present, else conda
    QList<CondaEnv> condaEnvs() const;
    QList<CondaPackage> condaPackages(const QString &envPath) const;
    void measureEnv(const QString &envPath);
    // Commands App Center runs in Terminal for conda.
    // Opens Terminal with the environment active, optionally running command.
    void openEnvTerminal(const QString &envPath, const QString &command = { });
    QString installMiniforgeCommand() const;
    void importEnvironment(const QString &file);
    void createEnvironment(const QString &name);
    void removeEnvironment(const QString &envPath);
    // Query first, publish atomically only after success; preserves the old file on failure.
    void exportEnvironment(const QString &envPath, const QString &file);

    QStringList sources() const;

Q_SIGNALS:
    void queryFailed(const QString &message);
    void environmentExported(const QString &file);
    void changed();
    void jobChanged(const CatalogApp &app);
    void jobFailed(const CatalogApp &app, const QString &message);
    void aptResults(const QString &query, const QList<CatalogApp> &apps);
    void flatpakResults(const QString &query, const QList<CatalogApp> &apps);
    void updatesChanged();
    void envSizeKnown(const QString &envPath, qint64 size);

private:
    static QString jobKey(const CatalogApp &app) { return app.source + QLatin1Char(':') + app.package; }
    void loadCatalog();
    void runQuery(
        const QString &program, const QStringList &args, const std::function<void(const QString &)> &done);
    void runAptJob(const CatalogApp &app, const QString &operation);
    void trackJob(const CatalogApp &app, Karyo::Apps::SoftwareJob *job);
    bool canStart(const CatalogApp &app);
    QString m_catalogError;
    quint64 m_refreshGeneration = 0;
    bool m_candidatesLoaded = false;
    bool m_updatesKnown = false, m_checkingUpdates = false;
    QString m_updatesError;
    quint64 m_updatesGeneration = 0;
    void runJob(const CatalogApp &app, const QString &program, const QStringList &args);

    QList<CatalogApp> m_catalog;
    QStringList m_featured;
    QHash<QString, QString> m_installed; // apt package -> installed version
    QHash<QString, QString> m_candidates; // apt package -> candidate version
    QSet<QString> m_flatpaks; // installed flatpak app ids
    QHash<QString, int> m_jobs; // jobKey -> progress percent
    QList<Upgrade> m_updates;
    mutable QHash<QString, qint64> m_envSizes;
};
