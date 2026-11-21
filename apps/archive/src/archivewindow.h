// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "archivebackend.h"
#include <karyo/ui/window.h>
#include <memory>

class QLabel;
class QLineEdit;
class QProgressBar;
class QTreeWidget;
class QTreeWidgetItem;
class QTemporaryDir;

class ArchiveWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    explicit ArchiveWindow(QWidget *parent = nullptr);
    ~ArchiveWindow() override;
    void openArchive(const QString &path);
    void createArchive(const QStringList &initialSources = {});
    bool busy() const { return m_busy; }
    QString archivePath() const { return m_archive; }

private:
    using Work = std::function<ArchiveBackend::Result(std::atomic_bool &, const ArchiveBackend::Progress &)>;
    void run(const QString &label, const Work &work,
             const std::function<void(const ArchiveBackend::Result &)> &done,
             const std::function<void(const QString &)> &retry = {}, const QString &archiveName = {});
    void setBusy(bool busy);
    void populate();
    void updateActions();
    void openArchiveWithPassword(const QString &path, const QString &password);
    void extract(bool selected);
    void extractTo(const QString &parent, const QStringList &paths, const QString &password);
    void openEntry(const QString &path, const QString &password);
    void activate(QTreeWidgetItem *item);
    QStringList selectedPaths() const;

    QString m_archive, m_folder, m_password;
    QList<ArchiveBackend::Entry> m_entries;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QList<std::shared_ptr<QTemporaryDir>> m_previews;
    bool m_busy = false;
    QLabel *m_title, *m_path, *m_status, *m_empty;
    QLineEdit *m_search;
    QTreeWidget *m_files;
    QProgressBar *m_progress;
    Karyo::Ui::PillButton *m_open, *m_new, *m_extract, *m_selected, *m_cancelButton;
    Karyo::Ui::IconButton *m_up;
};
