// SPDX-License-Identifier: GPL-3.0-or-later
// Karyo Photos, after Windows Photos: a gallery of the Pictures folder by
// month, and a viewer with zoom, rotation, a filmstrip of the neighbours,
// file details, a slideshow, Move to Trash and Set as desktop background.
#pragma once

#include "gallery.h"

#include <karyo/ui/window.h>

class ImageView;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QTimer;
class Thumbnails;

class PhotosWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    explicit PhotosWindow(QWidget *parent = nullptr);

    // Opens a picture with the other pictures of its folder around it.
    void openFile(const QString &path);
    void showGallery();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void scanGallery();
    void chooseFolder();
    void showPicture(int index);
    void step(int delta);
    void deleteCurrent();
    void setAsBackground();
    void copyCurrent();
    void showInFiles();
    void saveRotation();
    void toggleInfo();
    void toggleFullScreen();
    void startSlideshow();
    void stopSlideshow();
    void leaveViewer();
    void updateInfo();
    void updateZoomLabel();
    void placeArrows();
    void showMoreMenu();

    Thumbnails *m_thumbnails;
    QStackedWidget *m_pages;

    // Title strip: the gallery's title, or the viewer's name and tools.
    QWidget *m_galleryTitle;
    QWidget *m_viewerTitle;
    QLabel *m_name;
    QLabel *m_counter;
    QAbstractButton *m_zoomLabel;
    QAbstractButton *m_saveRotation;

    QScrollArea *m_galleryScroll;
    GalleryGrid *m_grid;
    QLabel *m_galleryTitleText;
    QLabel *m_gallerySubtitle;
    QLabel *m_empty;
    QString m_folder;

    QWidget *m_viewer;
    QWidget *m_stage;
    ImageView *m_view;
    QAbstractButton *m_previous;
    QAbstractButton *m_next;
    Filmstrip *m_filmstrip;
    QWidget *m_info;
    QLabel *m_infoText;

    QStringList m_paths;
    int m_current = -1;
    QTimer *m_slideshow;
    bool m_wasFullScreen = false;
};
