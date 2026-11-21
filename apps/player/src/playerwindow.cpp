// SPDX-License-Identifier: GPL-3.0-or-later
#include "playerwindow.h"

#include "playerwidgets.h"
#include "filemanager.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QAudioOutput>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThreadPool>
#include <QVBoxLayout>

#include <cmath>

namespace Ui = Karyo::Ui;

namespace {

QSettings playerSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("player"));
}

void setInk(QLabel *label, const QColor &color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

QLabel *label(const QFont &font, const QColor &color, QWidget *parent = nullptr)
{
    auto *l = new QLabel(parent);
    l->setFont(font);
    setInk(l, color);
    return l;
}

// The app's tile in the title strip: play on the accent.
class AppTile : public QWidget
{
public:
    explicit AppTile(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(30, 30);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, QRectF(rect()), 9, Ui::accent(600));
        p.drawPixmap(8, 7, Ui::icon(QStringLiteral("play"), 16, Ui::neutral(100), devicePixelRatioF()));
    }
};

// The playing song at the left of the transport bar opens Now playing.
class NowPlayingButton : public QAbstractButton
{
public:
    explicit NowPlayingButton(QWidget *parent)
        : QAbstractButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!underMouse())
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, QRectF(rect()), 8, Ui::Theme::current().hover);
    }
};

QStringList mediaFilter()
{
    return {QStringLiteral("*.mp3"),  QStringLiteral("*.flac"), QStringLiteral("*.ogg"), QStringLiteral("*.opus"),
            QStringLiteral("*.m4a"),  QStringLiteral("*.aac"),  QStringLiteral("*.wav"), QStringLiteral("*.mp4"),
            QStringLiteral("*.mkv"),  QStringLiteral("*.webm"), QStringLiteral("*.avi"), QStringLiteral("*.mov"),
            QStringLiteral("*.m4v"),  QStringLiteral("*.mpg"),  QStringLiteral("*.ogv"), QStringLiteral("*.wma"),
            QStringLiteral("*.wmv")};
}

} // namespace

PlayerWindow::PlayerWindow(QWidget *parent)
    : KaryoWindow(parent)
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
{
    const Ui::Theme &t = Ui::Theme::current();
    setWindowTitle(tr("Media Player"));
    setBodyColor(t.pane);
    setAcceptDrops(true);
    m_player->setAudioOutput(m_audio);

    titleLayout()->setContentsMargins(16, 0, 8, 0);
    titleLayout()->setSpacing(10);
    titleLayout()->addWidget(new AppTile);
    auto *appName = new QLabel(tr("Media Player"));
    appName->setFont(Ui::bodyFont(14, QFont::DemiBold));
    titleLayout()->addWidget(appName);
    titleLayout()->addStretch(1);

    // Navigation.
    m_navColumn = new QWidget;
    auto *navColumn = new QVBoxLayout(m_navColumn);
    navColumn->setContentsMargins(0, 4, 0, 4);
    m_nav = new Ui::NavList;
    m_nav->setFixedWidth(216);
    m_nav->addItem(QStringLiteral("music"), tr("Music"));
    m_nav->addItem(QStringLiteral("film"), tr("Videos"));
    m_nav->addItem(QStringLiteral("list-music"), tr("Play queue"));
    m_nav->addItem(QStringLiteral("circle-play"), tr("Now playing"));
    connect(m_nav, &Ui::NavList::currentChanged, this, &PlayerWindow::showPage);
    navColumn->addWidget(m_nav);
    navColumn->addStretch(1);
    auto *open = new Ui::PillButton(QStringLiteral("folder-open"), tr("Open files"));
    open->setKind(Ui::PillButton::Kind::Secondary);
    connect(open, &QAbstractButton::clicked, this, &PlayerWindow::openFilesDialog);
    navColumn->addWidget(open);

    // Pages.
    m_pages = new QStackedWidget;
    m_music = new TrackList;
    connect(m_music, &TrackList::activated, this, [this](int index) { playItems(m_music->items(), index); });
    connect(m_music, &TrackList::contextMenuRequested, this,
            [this](int index, const QPoint &at) { showTrackMenu(m_music->items().at(index), at); });
    m_pages->addWidget(libraryPage(tr("Music"), &m_musicSubtitle, m_music, &m_musicEmpty));
    m_videos = new VideoGrid;
    connect(m_videos, &VideoGrid::activated, this, [this](int index) { playItems(m_videos->items(), index); });
    m_pages->addWidget(libraryPage(tr("Videos"), &m_videosSubtitle, m_videos, &m_videosEmpty));
    m_queueList = new TrackList;
    connect(m_queueList, &TrackList::activated, this, &PlayerWindow::playIndex);
    QWidget *queueEmpty = nullptr;
    m_pages->addWidget(libraryPage(tr("Play queue"), &m_queueSubtitle, m_queueList, &queueEmpty));
    queueEmpty->hide();

    // Now playing: the cover and names, or the video.
    m_nowPlaying = new QStackedWidget;
    auto *coverPage = new QWidget;
    auto *coverColumn = new QVBoxLayout(coverPage);
    coverColumn->addStretch(1);
    m_bigCover = new CoverArt(280);
    coverColumn->addWidget(m_bigCover, 0, Qt::AlignHCenter);
    coverColumn->addSpacing(22);
    m_bigTitle = label(Ui::headingFont(30), t.text);
    m_bigTitle->setAlignment(Qt::AlignCenter);
    coverColumn->addWidget(m_bigTitle);
    m_bigSubtitle = label(Ui::bodyFont(15), t.muted);
    m_bigSubtitle->setAlignment(Qt::AlignCenter);
    m_bigSubtitle->setWordWrap(true);
    coverColumn->addWidget(m_bigSubtitle);
    coverColumn->addStretch(1);
    m_nowPlaying->addWidget(coverPage);
    m_video = new VideoView;
    m_player->setVideoSink(m_video->sink());
    connect(m_video, &VideoView::doubleClicked, this, &PlayerWindow::toggleFullScreen);
    m_nowPlaying->addWidget(m_video);
    auto *nowPanel = new QWidget;
    auto *nowLayout = new QVBoxLayout(nowPanel);
    nowLayout->setContentsMargins(0, 0, 0, 0);
    nowLayout->addWidget(m_nowPlaying);
    m_pages->addWidget(nowPanel);

    auto *panel = new Ui::InsetPanel;
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->addWidget(m_pages);

    // Transport bar.
    auto *transport = new QWidget;
    transport->setFixedHeight(92);
    auto *bar = new QHBoxLayout(transport);
    bar->setContentsMargins(4, 6, 12, 6);
    bar->setSpacing(12);
    auto *nowButton = new NowPlayingButton(transport);
    nowButton->setFixedWidth(300);
    connect(nowButton, &QAbstractButton::clicked, this, [this] { m_nav->setCurrentIndex(NowPlaying); });
    auto *nowRow = new QHBoxLayout(nowButton);
    nowRow->setContentsMargins(10, 8, 10, 8);
    nowRow->setSpacing(12);
    m_smallCover = new CoverArt(56, nowButton);
    m_smallCover->setAttribute(Qt::WA_TransparentForMouseEvents);
    nowRow->addWidget(m_smallCover);
    auto *names = new QVBoxLayout;
    names->setSpacing(2);
    names->addStretch(1);
    m_title = label(Ui::bodyFont(14, QFont::DemiBold), t.text, nowButton);
    m_subtitle = label(Ui::bodyFont(12.5), t.muted, nowButton);
    for (QLabel *l : {m_title, m_subtitle})
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
    names->addWidget(m_title);
    names->addWidget(m_subtitle);
    names->addStretch(1);
    nowRow->addLayout(names, 1);
    bar->addWidget(nowButton);

    auto *center = new QVBoxLayout;
    center->setSpacing(4);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto iconButton = [transport](const QString &glyph, const QString &hint) {
        auto *button = new Ui::IconButton(glyph, transport, {40, 40}, 18);
        button->setHint(hint);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    m_shuffleButton = iconButton(QStringLiteral("shuffle"), tr("Shuffle"));
    connect(m_shuffleButton, &QAbstractButton::clicked, this, [this] {
        m_shuffle = !m_shuffle;
        m_shuffleButton->setActive(m_shuffle);
        m_shuffleButton->setGlyphColor(m_shuffle ? Ui::Theme::current().accent : QColor());
        playerSettings().setValue(QStringLiteral("shuffle"), m_shuffle);
    });
    auto *previousButton = iconButton(QStringLiteral("skip-back"), tr("Previous (Ctrl+Left)"));
    connect(previousButton, &QAbstractButton::clicked, this, &PlayerWindow::previous);
    m_play = new PlayButton(transport);
    m_play->setPlaying(false);
    connect(m_play, &QAbstractButton::clicked, this, &PlayerWindow::togglePlay);
    auto *nextButton = iconButton(QStringLiteral("skip-forward"), tr("Next (Ctrl+Right)"));
    connect(nextButton, &QAbstractButton::clicked, this, [this] { next(false); });
    m_repeatButton = iconButton(QStringLiteral("repeat"), tr("Repeat"));
    connect(m_repeatButton, &QAbstractButton::clicked, this, [this] {
        m_repeat = (m_repeat + 1) % 3;
        m_repeatButton->setGlyph(m_repeat == 2 ? QStringLiteral("repeat-1") : QStringLiteral("repeat"));
        m_repeatButton->setActive(m_repeat != 0);
        m_repeatButton->setGlyphColor(m_repeat ? Ui::Theme::current().accent : QColor());
        m_repeatButton->setHint(m_repeat == 0 ? tr("Repeat") : m_repeat == 1 ? tr("Repeat all") : tr("Repeat one"));
        playerSettings().setValue(QStringLiteral("repeat"), m_repeat);
    });
    for (QWidget *w : {static_cast<QWidget *>(m_shuffleButton), static_cast<QWidget *>(previousButton),
                       static_cast<QWidget *>(m_play), static_cast<QWidget *>(nextButton),
                       static_cast<QWidget *>(m_repeatButton)})
        buttons->addWidget(w);
    buttons->addStretch(1);
    center->addLayout(buttons);
    auto *seekRow = new QHBoxLayout;
    seekRow->setSpacing(10);
    m_elapsed = label(Ui::bodyFont(12), t.muted, transport);
    m_elapsed->setFixedWidth(52);
    m_elapsed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_seek = new SeekBar(transport);
    connect(m_seek, &SeekBar::moved, m_player, &QMediaPlayer::setPosition);
    m_elapsed->setText(Library::duration(0));
    m_total = label(Ui::bodyFont(12), t.muted, transport);
    m_total->setText(Library::duration(0));
    m_total->setFixedWidth(52);
    seekRow->addWidget(m_elapsed);
    seekRow->addWidget(m_seek, 1);
    seekRow->addWidget(m_total);
    center->addLayout(seekRow);
    bar->addLayout(center, 1);

    auto *right = new QHBoxLayout;
    right->setSpacing(4);
    m_volumeButton = iconButton(QStringLiteral("volume-2"), tr("Mute (M)"));
    connect(m_volumeButton, &QAbstractButton::clicked, this, &PlayerWindow::toggleMute);
    m_volume = new SeekBar(transport);
    m_volume->setFixedWidth(120);
    m_volume->setRange(100);
    connect(m_volume, &SeekBar::moved, this, [this](qint64 value) { setVolume(int(value)); });
    m_fullScreen = iconButton(QStringLiteral("maximize"), tr("Full screen (F)"));
    connect(m_fullScreen, &QAbstractButton::clicked, this, &PlayerWindow::toggleFullScreen);
    right->addWidget(m_volumeButton);
    right->addWidget(m_volume);
    right->addSpacing(6);
    right->addWidget(m_fullScreen);
    auto *rightBox = new QWidget(transport);
    rightBox->setFixedWidth(300);
    rightBox->setLayout(right);
    right->insertStretch(0, 1);
    bar->addWidget(rightBox);

    auto *body = new QWidget;
    auto *column = new QVBoxLayout(body);
    column->setContentsMargins(12, 0, 12, 4);
    column->setSpacing(0);
    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    row->addWidget(m_navColumn);
    row->addWidget(panel, 1);
    column->addLayout(row, 1);
    column->addWidget(transport);
    setCentralWidget(body);

    // The player's state drives the bar.
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        m_play->setPlaying(state == QMediaPlayer::PlayingState);
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        m_seek->setRange(duration);
        m_total->setText(Library::duration(duration));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        m_seek->setValue(position);
        m_elapsed->setText(Library::duration(position));
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            next(true);
    });
    connect(m_player, &QMediaPlayer::metaDataChanged, this, &PlayerWindow::updateNowPlaying);
    connect(m_player, &QMediaPlayer::hasVideoChanged, this, &PlayerWindow::updateNowPlaying);
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        m_error = message;
        updateNowPlaying();
    });

    auto shortcut = [this](const QKeySequence &keys, auto slot) {
        auto *s = new QShortcut(keys, this);
        connect(s, &QShortcut::activated, this, slot);
    };
    shortcut(QKeySequence(Qt::Key_Space), [this] { togglePlay(); });
    shortcut(QKeySequence(Qt::Key_MediaPlay), [this] { togglePlay(); });
    shortcut(QKeySequence(Qt::Key_MediaTogglePlayPause), [this] { togglePlay(); });
    shortcut(QKeySequence(Qt::Key_MediaNext), [this] { next(false); });
    shortcut(QKeySequence(Qt::Key_MediaPrevious), [this] { previous(); });
    shortcut(QKeySequence(Qt::Key_Right), [this] { seekBy(5000); });
    shortcut(QKeySequence(Qt::Key_Left), [this] { seekBy(-5000); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+Right")), [this] { next(false); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+Left")), [this] { previous(); });
    shortcut(QKeySequence(Qt::Key_Up), [this] { setVolume(m_volumePercent + 5); });
    shortcut(QKeySequence(Qt::Key_Down), [this] { setVolume(m_volumePercent - 5); });
    shortcut(QKeySequence(Qt::Key_M), [this] { toggleMute(); });
    shortcut(QKeySequence(Qt::Key_F), [this] { toggleFullScreen(); });
    shortcut(QKeySequence(Qt::Key_F11), [this] { toggleFullScreen(); });
    shortcut(QKeySequence(Qt::Key_Escape), [this] {
        if (isFullScreen())
            toggleFullScreen();
    });
    shortcut(QKeySequence::Open, [this] { openFilesDialog(); });

    QSettings settings = playerSettings();
    m_volumePercent = settings.value(QStringLiteral("volume"), 70).toInt();
    m_shuffle = settings.value(QStringLiteral("shuffle"), false).toBool();
    m_shuffleButton->setActive(m_shuffle);
    m_shuffleButton->setGlyphColor(m_shuffle ? t.accent : QColor());
    m_repeat = qBound(0, settings.value(QStringLiteral("repeat"), 0).toInt(), 2);
    if (m_repeat) {
        m_repeat = (m_repeat + 2) % 3;
        m_repeatButton->click();
    }
    setVolume(m_volumePercent);
    updateNowPlaying();
    m_nav->setCurrentIndex(Music);
    scanLibrary();
    resizeToFit({1360, 880});
}

QWidget *PlayerWindow::libraryPage(const QString &title, QLabel **subtitle, QWidget *content, QWidget **empty)
{
    const Ui::Theme &t = Ui::Theme::current();
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(32, 26, 32, 26);
    v->setSpacing(4);
    auto *heading = label(Ui::headingFont(28), t.text);
    heading->setText(title);
    v->addWidget(heading);
    *subtitle = label(Ui::bodyFont(13.5), t.muted);
    v->addWidget(*subtitle);
    v->addSpacing(18);
    v->addWidget(content);
    auto *emptyLabel = label(Ui::bodyFont(14), t.muted);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setMinimumHeight(220);
    emptyLabel->setWordWrap(true);
    emptyLabel->hide();
    v->addWidget(emptyLabel);
    *empty = emptyLabel;
    v->addStretch(1);
    scroll->setWidget(page);
    page->setAutoFillBackground(false);
    return scroll;
}

void PlayerWindow::scanLibrary()
{
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    m_musicSubtitle->setText(tr("Looking in %1…").arg(QDir::toNativeSeparators(music)));
    m_videosSubtitle->setText(tr("Looking in %1…").arg(QDir::toNativeSeparators(videos)));
    QPointer<PlayerWindow> self(this);
    QThreadPool::globalInstance()->start([self, music, videos] {
        const QList<MediaItem> songs = Library::scan(music, false);
        const QList<MediaItem> films = Library::scan(videos, true);
        QMetaObject::invokeMethod(
            self.data(),
            [self, music, videos, songs, films] {
                if (!self)
                    return;
                self->m_music->setItems(songs);
                self->m_musicSubtitle->setText(
                    (songs.size() == 1 ? tr("1 song in %1") : tr("%n songs in %1", nullptr, int(songs.size())))
                        .arg(QDir::toNativeSeparators(music)));
                static_cast<QLabel *>(self->m_musicEmpty)
                    ->setText(tr("No music in %1 yet. Open files or drop them here.").arg(QDir::toNativeSeparators(music)));
                self->m_musicEmpty->setVisible(songs.isEmpty());
                self->m_videos->setItems(films);
                self->m_videosSubtitle->setText(
                    (films.size() == 1 ? tr("1 video in %1") : tr("%n videos in %1", nullptr, int(films.size())))
                        .arg(QDir::toNativeSeparators(videos)));
                static_cast<QLabel *>(self->m_videosEmpty)
                    ->setText(tr("No videos in %1 yet. Open files or drop them here.").arg(QDir::toNativeSeparators(videos)));
                self->m_videosEmpty->setVisible(films.isEmpty());
            },
            Qt::QueuedConnection);
    });
}

void PlayerWindow::showPage(int page)
{
    m_pages->setCurrentIndex(page);
    if (page == Queue) {
        m_queueList->setItems(m_queue);
        m_queueList->setPlaying(m_index >= 0 ? m_queue.at(m_index).path : QString());
        m_queueSubtitle->setText(m_queue.isEmpty() ? tr("Nothing is queued. Play a song or a video, or drop files here.")
                                                   : tr("%1 of %2").arg(m_index + 1).arg(m_queue.size()));
    }
}

void PlayerWindow::openPaths(const QStringList &paths)
{
    QList<MediaItem> items;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            items += Library::scan(info.absoluteFilePath(), false);
            items += Library::scan(info.absoluteFilePath(), true);
        } else if (info.exists()) {
            items.append(Library::item(info.absoluteFilePath()));
        }
    }
    if (!items.isEmpty())
        playItems(items, 0);
}

void PlayerWindow::playItems(const QList<MediaItem> &items, int index)
{
    m_queue = items;
    playIndex(index);
}

void PlayerWindow::playIndex(int index)
{
    if (index < 0 || index >= m_queue.size())
        return;
    m_index = index;
    m_error.clear();
    m_video->clear();
    const MediaItem &item = m_queue.at(index);
    m_player->setSource(QUrl::fromLocalFile(item.path));
    m_player->play();
    m_music->setPlaying(item.path);
    m_queueList->setPlaying(item.path);
    if (m_pages->currentIndex() == Queue)
        showPage(Queue);
    // A video shows at once; a song keeps the list in view.
    if (item.video)
        m_nav->setCurrentIndex(NowPlaying);
    updateNowPlaying();
}

void PlayerWindow::next(bool automatic)
{
    if (m_queue.isEmpty())
        return;
    if (automatic && m_repeat == 2) {
        m_player->setPosition(0);
        m_player->play();
        return;
    }
    int index = m_index + 1;
    if (m_shuffle && m_queue.size() > 1) {
        do {
            index = int(QRandomGenerator::global()->bounded(m_queue.size()));
        } while (index == m_index);
    } else if (index >= m_queue.size()) {
        // At the end: around again with Repeat all, otherwise stop there.
        if (automatic && m_repeat == 0) {
            m_player->stop();
            return;
        }
        index = 0;
    }
    playIndex(index);
}

void PlayerWindow::previous()
{
    if (m_queue.isEmpty())
        return;
    // Like every player: a few seconds in, back to the start of this one.
    if (m_player->position() > 3000) {
        m_player->setPosition(0);
        return;
    }
    playIndex(m_index > 0 ? m_index - 1 : int(m_queue.size()) - 1);
}

void PlayerWindow::togglePlay()
{
    if (m_player->source().isEmpty()) {
        if (!m_music->items().isEmpty())
            playItems(m_music->items(), 0);
        return;
    }
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void PlayerWindow::seekBy(qint64 ms)
{
    if (!m_player->source().isEmpty())
        m_player->setPosition(qBound<qint64>(0, m_player->position() + ms, m_player->duration()));
}

void PlayerWindow::setVolume(int percent)
{
    m_volumePercent = qBound(0, percent, 100);
    m_muted = false;
    // Perceived loudness follows the cube of the amplitude, roughly.
    m_audio->setVolume(std::pow(m_volumePercent / 100.0, 3));
    m_audio->setMuted(false);
    m_volume->setValue(m_volumePercent);
    playerSettings().setValue(QStringLiteral("volume"), m_volumePercent);
    updateVolumeGlyph();
}

void PlayerWindow::toggleMute()
{
    m_muted = !m_muted;
    m_audio->setMuted(m_muted);
    updateVolumeGlyph();
}

void PlayerWindow::updateVolumeGlyph()
{
    m_volumeButton->setGlyph(m_muted || m_volumePercent == 0 ? QStringLiteral("volume-x")
                             : m_volumePercent < 50          ? QStringLiteral("volume-1")
                                                             : QStringLiteral("volume-2"));
    m_volumeButton->setHint(m_muted ? tr("Unmute (M)") : tr("Mute (M)"));
}

void PlayerWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
        titleBar()->show();
        m_navColumn->show();
        m_fullScreen->setGlyph(QStringLiteral("maximize"));
        return;
    }
    // Full screen is for watching: the video alone, with the bar under it.
    m_nav->setCurrentIndex(NowPlaying);
    titleBar()->hide();
    m_navColumn->hide();
    m_fullScreen->setGlyph(QStringLiteral("minimize"));
    showFullScreen();
}

void PlayerWindow::addToQueue(const MediaItem &item, bool playNext)
{
    if (m_queue.isEmpty()) {
        playItems({item}, 0);
        return;
    }
    m_queue.insert(playNext ? m_index + 1 : m_queue.size(), item);
    if (m_pages->currentIndex() == Queue)
        showPage(Queue);
}

void PlayerWindow::showTrackMenu(const MediaItem &item, const QPoint &globalPos)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    menu->addAction(tr("Play"), this, [this, item] {
        const QList<MediaItem> items = m_music->items();
        for (int i = 0; i < items.size(); ++i) {
            if (items.at(i).path == item.path)
                playItems(items, i);
        }
    });
    menu->addAction(tr("Play next"), this, [this, item] { addToQueue(item, true); });
    menu->addAction(tr("Add to queue"), this, [this, item] { addToQueue(item, false); });
    menu->addSeparator();
    menu->addAction(tr("Show in Files"), this, [this, item] {
        Karyo::Apps::showInFiles(item.path, this);
    });
    menu->popup(globalPos);
}

void PlayerWindow::openFilesDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Open files"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        tr("Music and videos (%1)").arg(mediaFilter().join(QLatin1Char(' '))));
    openPaths(files);
}

void PlayerWindow::updateNowPlaying()
{
    const bool playing = m_index >= 0 && m_index < m_queue.size();
    MediaItem item = playing ? m_queue.at(m_index) : MediaItem();
    const QMediaMetaData meta = m_player->metaData();
    QString title = meta.stringValue(QMediaMetaData::Title);
    if (title.isEmpty())
        title = item.title;
    QString artist = meta.stringValue(QMediaMetaData::ContributingArtist);
    if (artist.isEmpty())
        artist = meta.stringValue(QMediaMetaData::AlbumArtist);
    const QString album = meta.stringValue(QMediaMetaData::AlbumTitle);
    QString subtitle = QStringList{artist, album}.join(QStringLiteral(" · "));
    if (artist.isEmpty() || album.isEmpty())
        subtitle = artist.isEmpty() ? album : artist;
    if (subtitle.isEmpty())
        subtitle = item.subtitle;
    QImage cover = meta.value(QMediaMetaData::CoverArtImage).value<QImage>();
    if (cover.isNull())
        cover = meta.value(QMediaMetaData::ThumbnailImage).value<QImage>();

    if (!playing) {
        title = tr("Nothing playing");
        subtitle = tr("Pick a song or a video, or drop files here.");
    }
    if (!m_error.isEmpty())
        subtitle = tr("This file cannot be played: %1").arg(m_error);
    m_title->setText(QFontMetrics(m_title->font()).elidedText(title, Qt::ElideRight, 210));
    m_subtitle->setText(QFontMetrics(m_subtitle->font()).elidedText(subtitle, Qt::ElideRight, 210));
    m_bigTitle->setText(title);
    m_bigSubtitle->setText(subtitle);
    m_smallCover->setImage(cover);
    m_bigCover->setImage(cover);
    const bool video = playing && (item.video || m_player->hasVideo());
    m_smallCover->setGlyph(video ? QStringLiteral("film") : QStringLiteral("music"));
    m_nowPlaying->setCurrentIndex(video ? 1 : 0);
    m_fullScreen->setEnabled(video);
    setWindowTitle(playing ? title + QStringLiteral(" — ") + tr("Media Player") : tr("Media Player"));
}

void PlayerWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void PlayerWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    openPaths(paths);
}

void PlayerWindow::closeEvent(QCloseEvent *event)
{
    m_player->stop();
    event->accept();
}

PlayerService::PlayerService(PlayerWindow *window)
    : QObject(window)
    , m_window(window)
{
}

void PlayerService::Open(const QStringList &paths)
{
    m_window->openPaths(paths);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}
