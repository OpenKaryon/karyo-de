// SPDX-License-Identifier: GPL-3.0-or-later
// Details pane of Files: the selection's tile, properties, checksum and
// default application.
#pragma once

#include <QFileInfo>
#include <QPointer>
#include <QWidget>

#include <memory>

class QLabel;
class QThread;
class QVBoxLayout;

class DetailsPane : public QWidget
{
    Q_OBJECT
public:
    explicit DetailsPane(QWidget *parent = nullptr);
    ~DetailsPane() override;
    void showInfos(const QList<QFileInfo> &infos, const QString &folder);
    void clearFolderSizes();

Q_SIGNALS:
    void openWithRequested(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QLabel *addProperty(const QString &label, const QString &value, bool mono = false);
    void clearProperties();
    void computeChecksum(const QString &path, QLabel *target);

    struct FolderStats;
    class FolderSizeCache;
    void updateFolderSize();
    void showFolderSize(const FolderStats &stats);

    std::shared_ptr<FolderSizeCache> m_folderSizes;
    QThread *m_folderWorker = nullptr;
    QString m_scanningPath;
    QLabel *m_sizeValue = nullptr;
    QLabel *m_contentsValue = nullptr;

    class TileWidget *m_tile;
    QLabel *m_name;
    QLabel *m_subtitle;
    QVBoxLayout *m_properties;
    QWidget *m_buttons;
    QString m_path;
    quint64 m_generation = 0;
};
