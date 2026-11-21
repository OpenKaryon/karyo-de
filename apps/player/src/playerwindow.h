// SPDX-License-Identifier: GPL-3.0-or-later
// Karyo Media Player, after Windows Media Player: Music and Videos from the
// home folders, a play queue, Now playing with the cover or the video, and
// a transport bar (shuffle, previous, play, next, repeat, seek, volume).
#pragma once

#include "library.h"

#include <karyo/ui/window.h>

class CoverArt;
class PlayButton;
class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QStackedWidget;
class SeekBar;
class TrackList;
class VideoGrid;
class VideoView;

namespace Karyo::Ui {
class IconButton;
class NavList;
}

class PlayerWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    explicit PlayerWindow(QWidget *parent = nullptr);

    // Files and folders: they become the queue and the first one plays.
    void openPaths(const QStringList &paths);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum Page { Music, Videos, Queue, NowPlaying };

    QWidget *libraryPage(const QString &title, QLabel **subtitle, QWidget *content, QWidget **empty);
    void scanLibrary();
    void showPage(int page);
    void playItems(const QList<MediaItem> &items, int index);
    void playIndex(int index);
    void next(bool automatic);
    void previous();
    void togglePlay();
    void seekBy(qint64 ms);
    void setVolume(int percent);
    void toggleMute();
    void toggleFullScreen();
    void addToQueue(const MediaItem &item, bool next);
    void showTrackMenu(const MediaItem &item, const QPoint &globalPos);
    void openFilesDialog();
    void updateNowPlaying();
    void updateVolumeGlyph();

    QMediaPlayer *m_player;
    QAudioOutput *m_audio;

    QWidget *m_navColumn;
    Karyo::Ui::NavList *m_nav;
    QStackedWidget *m_pages;
    TrackList *m_music;
    QLabel *m_musicSubtitle;
    QWidget *m_musicEmpty;
    VideoGrid *m_videos;
    QLabel *m_videosSubtitle;
    QWidget *m_videosEmpty;
    TrackList *m_queueList;
    QLabel *m_queueSubtitle;
    QStackedWidget *m_nowPlaying;
    CoverArt *m_bigCover;
    QLabel *m_bigTitle;
    QLabel *m_bigSubtitle;
    VideoView *m_video;

    CoverArt *m_smallCover;
    QLabel *m_title;
    QLabel *m_subtitle;
    Karyo::Ui::IconButton *m_shuffleButton;
    Karyo::Ui::IconButton *m_repeatButton;
    PlayButton *m_play;
    SeekBar *m_seek;
    QLabel *m_elapsed;
    QLabel *m_total;
    Karyo::Ui::IconButton *m_volumeButton;
    SeekBar *m_volume;
    Karyo::Ui::IconButton *m_fullScreen;

    QList<MediaItem> m_queue;
    int m_index = -1;
    bool m_shuffle = false;
    int m_repeat = 0;  // 0 off, 1 all, 2 one
    int m_volumePercent = 70;
    bool m_muted = false;
    QString m_error;
};

// org.openkaryon.Player on the session bus: a second start hands its files
// to the running player.
class PlayerService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.openkaryon.Player")
public:
    explicit PlayerService(PlayerWindow *window);

public Q_SLOTS:
    Q_SCRIPTABLE void Open(const QStringList &paths);

private:
    PlayerWindow *m_window;
};
