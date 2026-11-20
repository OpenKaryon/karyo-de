// SPDX-License-Identifier: GPL-3.0-or-later
#include "photoswindow.h"

#include "imageview.h"
#include "thumbnails.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QDesktopServices>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace Ui = Karyo::Ui;

namespace {

constexpr int MaxGalleryPictures = 5000;

QSettings photosSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("photos"));
}

void setInk(QLabel *label, const QColor &color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

// The app's tile in the title strip, as on the taskbar and in Start.
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
        Ui::fillRounded(p, QRectF(rect()), 9, Ui::accent(200));
        p.drawPixmap(7, 7, Ui::icon(QStringLiteral("image"), 16, Ui::accent(800), devicePixelRatioF()));
    }
};

// The viewer's ground: the desk color, so photos sit on a calm background.
class Stage : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, QRectF(rect()), 8, Ui::Theme::current().desk);
    }
};

// "100%" in the title strip; a click fits the picture again.
class ZoomLabel : public QAbstractButton
{
public:
    explicit ZoomLabel(QWidget *parent)
        : QAbstractButton(parent)
    {
        setFixedSize(64, 34);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse())
            Ui::fillPill(p, rect(), t.hover);
        p.setFont(Ui::bodyFont(13, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(rect(), Qt::AlignCenter, text());
    }
};

QString formatName(const QString &path)
{
    QString format = QString::fromLatin1(QImageReader::imageFormat(path)).toUpper();
    if (format == QLatin1String("JPG"))
        format = QStringLiteral("JPEG");
    return format;
}

} // namespace

PhotosWindow::PhotosWindow(QWidget *parent)
    : KaryoWindow(parent)
    , m_thumbnails(new Thumbnails(int(176 * qApp->devicePixelRatio()), this))
    , m_pages(new QStackedWidget)
    , m_slideshow(new QTimer(this))
{
    const Ui::Theme &t = Ui::Theme::current();
    setWindowTitle(tr("Photos"));
    setBodyColor(t.pane);
    titleLayout()->setContentsMargins(16, 0, 8, 0);
    titleLayout()->setSpacing(4);

    // Gallery title: tile and name.
    m_galleryTitle = new QWidget;
    auto *galleryTitle = new QHBoxLayout(m_galleryTitle);
    galleryTitle->setContentsMargins(0, 0, 0, 0);
    galleryTitle->setSpacing(10);
    galleryTitle->addWidget(new AppTile);
    auto *appName = new QLabel(tr("Photos"));
    appName->setFont(Ui::bodyFont(14, QFont::DemiBold));
    galleryTitle->addWidget(appName);
    galleryTitle->addStretch(1);
    titleLayout()->addWidget(m_galleryTitle, 1);

    // Viewer title: back, name, counter; then the tools.
    m_viewerTitle = new QWidget;
    auto *viewerTitle = new QHBoxLayout(m_viewerTitle);
    viewerTitle->setContentsMargins(0, 0, 0, 0);
    viewerTitle->setSpacing(4);
    auto tool = [this, viewerTitle](const QString &glyph, const QString &hint, auto slot) {
        auto *button = new Ui::IconButton(glyph, m_viewerTitle);
        button->setHint(hint);
        connect(button, &QAbstractButton::clicked, this, slot);
        viewerTitle->addWidget(button);
        return button;
    };
    tool(QStringLiteral("arrow-left"), tr("Gallery (Esc)"), [this] { leaveViewer(); });
    viewerTitle->addSpacing(6);
    m_name = new QLabel;
    m_name->setFont(Ui::bodyFont(14, QFont::DemiBold));
    viewerTitle->addWidget(m_name);
    m_counter = new QLabel;
    m_counter->setFont(Ui::bodyFont(12.5));
    setInk(m_counter, t.muted);
    viewerTitle->addSpacing(8);
    viewerTitle->addWidget(m_counter);
    viewerTitle->addStretch(1);
    m_saveRotation = new Ui::PillButton(QStringLiteral("download"), tr("Save rotation"), m_viewerTitle);
    static_cast<Ui::PillButton *>(m_saveRotation)->setKind(Ui::PillButton::Kind::Primary);
    connect(m_saveRotation, &QAbstractButton::clicked, this, &PhotosWindow::saveRotation);
    m_saveRotation->hide();
    viewerTitle->addWidget(m_saveRotation);
    viewerTitle->addSpacing(8);
    tool(QStringLiteral("zoom-out"), tr("Zoom out (Ctrl+-)"), [this] { m_view->zoomBy(1 / 1.25); });
    m_zoomLabel = new ZoomLabel(m_viewerTitle);
    m_zoomLabel->setToolTip(tr("Fit to window (Ctrl+0)"));
    connect(m_zoomLabel, &QAbstractButton::clicked, this, [this] { m_view->fit(); });
    viewerTitle->addWidget(m_zoomLabel);
    tool(QStringLiteral("zoom-in"), tr("Zoom in (Ctrl++)"), [this] { m_view->zoomBy(1.25); });
    viewerTitle->addSpacing(6);
    tool(QStringLiteral("rotate-ccw"), tr("Rotate (Ctrl+R)"), [this] {
        m_view->rotate(-1);
        m_saveRotation->setVisible(m_view->rotation() != 0);
    });
    tool(QStringLiteral("trash-2"), tr("Move to Trash (Delete)"), [this] { deleteCurrent(); });
    tool(QStringLiteral("info"), tr("Details (I)"), [this] { toggleInfo(); });
    tool(QStringLiteral("presentation"), tr("Slideshow (F5)"), [this] { startSlideshow(); });
    tool(QStringLiteral("ellipsis"), tr("More"), [this] { showMoreMenu(); });
    titleLayout()->addWidget(m_viewerTitle, 1);
    m_viewerTitle->hide();

    // Gallery page.
    auto *galleryPage = new QWidget;
    auto *galleryColumn = new QVBoxLayout(galleryPage);
    galleryColumn->setContentsMargins(0, 0, 0, 0);
    auto *panel = new Ui::InsetPanel;
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    m_galleryScroll = new QScrollArea;
    m_galleryScroll->setFrameShape(QFrame::NoFrame);
    m_galleryScroll->setWidgetResizable(true);
    auto *galleryContent = new QWidget;
    auto *content = new QVBoxLayout(galleryContent);
    content->setContentsMargins(32, 26, 32, 26);
    content->setSpacing(4);
    auto *header = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    m_galleryTitleText = new QLabel(tr("Gallery"));
    m_galleryTitleText->setFont(Ui::headingFont(28));
    titles->addWidget(m_galleryTitleText);
    m_gallerySubtitle = new QLabel;
    m_gallerySubtitle->setFont(Ui::bodyFont(13.5));
    setInk(m_gallerySubtitle, t.muted);
    titles->addWidget(m_gallerySubtitle);
    header->addLayout(titles, 1);
    auto *choose = new Ui::PillButton(QStringLiteral("folder-open"), tr("Choose folder"));
    choose->setKind(Ui::PillButton::Kind::Secondary);
    connect(choose, &QAbstractButton::clicked, this, &PhotosWindow::chooseFolder);
    header->addWidget(choose, 0, Qt::AlignTop);
    content->addLayout(header);
    m_grid = new GalleryGrid(m_thumbnails);
    connect(m_grid, &GalleryGrid::activated, this, [this](int index) {
        m_paths.clear();
        for (const Picture &picture : m_grid->pictures())
            m_paths.append(picture.path);
        m_filmstrip->setPictures(m_paths, index);
        showPicture(index);
    });
    content->addWidget(m_grid);
    m_empty = new QLabel;
    m_empty->setFont(Ui::bodyFont(14));
    setInk(m_empty, t.muted);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setMinimumHeight(240);
    m_empty->hide();
    content->addWidget(m_empty);
    content->addStretch(1);
    m_galleryScroll->setWidget(galleryContent);
    galleryContent->setAutoFillBackground(false);
    panelLayout->addWidget(m_galleryScroll);
    galleryColumn->addWidget(panel);
    m_pages->addWidget(galleryPage);

    // Viewer page: the stage with arrows, the filmstrip, the details panel.
    m_viewer = new QWidget;
    auto *viewerRow = new QHBoxLayout(m_viewer);
    viewerRow->setContentsMargins(0, 0, 0, 0);
    viewerRow->setSpacing(12);
    auto *viewerColumn = new QVBoxLayout;
    viewerColumn->setSpacing(8);
    m_stage = new Stage;
    auto *stageLayout = new QVBoxLayout(m_stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    m_view = new ImageView(m_stage);
    stageLayout->addWidget(m_view);
    m_previous = new Ui::IconButton(QStringLiteral("chevron-left"), m_stage, {44, 44}, 20);
    m_next = new Ui::IconButton(QStringLiteral("chevron-right"), m_stage, {44, 44}, 20);
    for (QAbstractButton *arrow : {m_previous, m_next}) {
        static_cast<Ui::IconButton *>(arrow)->setHoverColor(t.card);
        arrow->raise();
    }
    connect(m_previous, &QAbstractButton::clicked, this, [this] { step(-1); });
    connect(m_next, &QAbstractButton::clicked, this, [this] { step(1); });
    connect(m_view, &ImageView::zoomChanged, this, &PhotosWindow::updateZoomLabel);
    viewerColumn->addWidget(m_stage, 1);
    m_filmstrip = new Filmstrip(m_thumbnails);
    connect(m_filmstrip, &Filmstrip::activated, this, &PhotosWindow::showPicture);
    viewerColumn->addWidget(m_filmstrip);
    viewerRow->addLayout(viewerColumn, 1);
    m_info = new Ui::InsetPanel;
    m_info->setFixedWidth(300);
    auto *infoLayout = new QVBoxLayout(m_info);
    infoLayout->setContentsMargins(22, 22, 22, 22);
    auto *infoTitle = new QLabel(tr("Details"));
    infoTitle->setFont(Ui::headingFont(20));
    infoLayout->addWidget(infoTitle);
    infoLayout->addSpacing(8);
    m_infoText = new QLabel;
    m_infoText->setFont(Ui::bodyFont(13));
    m_infoText->setWordWrap(true);
    m_infoText->setTextFormat(Qt::RichText);
    m_infoText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addWidget(m_infoText);
    infoLayout->addStretch(1);
    m_info->setVisible(photosSettings().value(QStringLiteral("details"), false).toBool());
    viewerRow->addWidget(m_info);
    m_pages->addWidget(m_viewer);

    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 0, 12, 12);
    bodyLayout->addWidget(m_pages);
    setCentralWidget(body);

    m_slideshow->setInterval(4000);
    connect(m_slideshow, &QTimer::timeout, this, [this] { step(1); });

    auto shortcut = [this](const QKeySequence &keys, auto slot) {
        auto *s = new QShortcut(keys, this);
        connect(s, &QShortcut::activated, this, slot);
    };
    auto viewing = [this] { return m_pages->currentWidget() == m_viewer; };
    shortcut(QKeySequence(Qt::Key_Left), [this, viewing] { if (viewing()) step(-1); });
    shortcut(QKeySequence(Qt::Key_Right), [this, viewing] { if (viewing()) step(1); });
    shortcut(QKeySequence(Qt::Key_Home), [this, viewing] { if (viewing()) showPicture(0); });
    shortcut(QKeySequence(Qt::Key_End), [this, viewing] { if (viewing()) showPicture(int(m_paths.size()) - 1); });
    shortcut(QKeySequence(Qt::Key_Delete), [this, viewing] { if (viewing()) deleteCurrent(); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+=")), [this] { m_view->zoomBy(1.25); });
    shortcut(QKeySequence(QStringLiteral("Ctrl++")), [this] { m_view->zoomBy(1.25); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+-")), [this] { m_view->zoomBy(1 / 1.25); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+0")), [this] { m_view->fit(); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+1")), [this] { m_view->zoomTo(1.0); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+R")), [this] {
        m_view->rotate(-1);
        m_saveRotation->setVisible(m_view->rotation() != 0);
    });
    shortcut(QKeySequence(QStringLiteral("Ctrl+C")), [this, viewing] { if (viewing()) copyCurrent(); });
    shortcut(QKeySequence(Qt::Key_I), [this, viewing] { if (viewing()) toggleInfo(); });
    shortcut(QKeySequence(Qt::Key_F5), [this, viewing] { if (viewing()) startSlideshow(); });
    shortcut(QKeySequence(Qt::Key_F11), [this] { toggleFullScreen(); });
    shortcut(QKeySequence(Qt::Key_Escape), [this, viewing] {
        if (m_slideshow->isActive())
            stopSlideshow();
        else if (isFullScreen())
            toggleFullScreen();
        else if (viewing())
            leaveViewer();
    });

    m_folder = photosSettings().value(QStringLiteral("folder"),
                                      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                   .toString();
    resizeToFit({1360, 880});
}

void PhotosWindow::showGallery()
{
    m_pages->setCurrentIndex(0);
    m_galleryTitle->show();
    m_viewerTitle->hide();
    setWindowTitle(tr("Photos"));
    if (m_grid->pictures().isEmpty())
        scanGallery();
}

void PhotosWindow::scanGallery()
{
    const QString folder = m_folder;
    m_gallerySubtitle->setText(tr("Looking for pictures in %1…").arg(QDir::toNativeSeparators(folder)));
    QPointer<PhotosWindow> self(this);
    const QStringList filters = Pictures::nameFilters();
    QThreadPool::globalInstance()->start([self, folder, filters] {
        QList<Picture> pictures;
        // Hidden folders (thumbnails caches, .git) stay out.
        QDirIterator it(folder, filters, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (it.hasNext() && pictures.size() < MaxGalleryPictures) {
            it.next();
            pictures.append({it.filePath(), it.fileInfo().lastModified()});
        }
        std::sort(pictures.begin(), pictures.end(),
                  [](const Picture &a, const Picture &b) { return a.modified > b.modified; });
        QMetaObject::invokeMethod(
            self.data(),
            [self, folder, pictures] {
                if (!self || folder != self->m_folder)
                    return;
                self->m_grid->setPictures(pictures);
                const QString where = QDir::toNativeSeparators(folder);
                const int count = int(pictures.size());
                self->m_gallerySubtitle->setText(
                    (count == 1 ? tr("1 picture in %1") : tr("%n pictures in %1", nullptr, count)).arg(where));
                self->m_empty->setText(tr("No pictures in %1 yet.").arg(where));
                self->m_empty->setVisible(pictures.isEmpty());
            },
            Qt::QueuedConnection);
    });
}

void PhotosWindow::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose folder"), m_folder);
    if (folder.isEmpty())
        return;
    m_folder = folder;
    photosSettings().setValue(QStringLiteral("folder"), folder);
    m_grid->setPictures({});
    scanGallery();
}

void PhotosWindow::openFile(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    m_paths = Pictures::inFolder(QFileInfo(absolute).absolutePath());
    if (!m_paths.contains(absolute))
        m_paths.prepend(absolute);
    const int index = int(m_paths.indexOf(absolute));
    m_filmstrip->setPictures(m_paths, index);
    showPicture(index);
}

void PhotosWindow::showPicture(int index)
{
    if (index < 0 || index >= m_paths.size())
        return;
    m_current = index;
    m_pages->setCurrentWidget(m_viewer);
    m_galleryTitle->hide();
    m_viewerTitle->show();
    const QString path = m_paths.at(index);
    if (!m_view->open(path))
        m_name->setText(QFileInfo(path).fileName() + QStringLiteral(" — ") + m_view->error());
    else
        m_name->setText(QFileInfo(path).fileName());
    m_counter->setText(tr("%1 of %2").arg(index + 1).arg(m_paths.size()));
    m_saveRotation->hide();
    m_filmstrip->setCurrent(index);
    m_previous->setVisible(index > 0);
    m_next->setVisible(index + 1 < m_paths.size());
    setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" — ") + tr("Photos"));
    updateInfo();
    updateZoomLabel();
    m_view->setFocus();
    QTimer::singleShot(0, this, &PhotosWindow::placeArrows);
}

void PhotosWindow::step(int delta)
{
    if (m_paths.isEmpty())
        return;
    int index = m_current + delta;
    // The slideshow goes round; the arrows stop at the ends.
    if (m_slideshow->isActive())
        index = (index + int(m_paths.size())) % int(m_paths.size());
    showPicture(index);
}

void PhotosWindow::leaveViewer()
{
    m_view->open(QString());
    showGallery();
}

void PhotosWindow::deleteCurrent()
{
    if (m_current < 0)
        return;
    const QString path = m_paths.at(m_current);
    QMessageBox box(this);
    box.setWindowTitle(tr("Photos"));
    box.setText(tr("Move “%1” to the Trash?").arg(QFileInfo(path).fileName()));
    box.setInformativeText(tr("You can restore it from the Trash in Files."));
    QAbstractButton *move = box.addButton(tr("Move to Trash"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != move)
        return;
    if (!QFile::moveToTrash(path)) {
        QMessageBox::warning(this, tr("Photos"), tr("“%1” could not be moved to the Trash.").arg(QFileInfo(path).fileName()));
        return;
    }
    m_paths.removeAt(m_current);
    QList<Picture> remaining = m_grid->pictures();
    remaining.erase(std::remove_if(remaining.begin(), remaining.end(),
                                   [&path](const Picture &p) { return p.path == path; }),
                    remaining.end());
    m_grid->setPictures(remaining);
    if (m_paths.isEmpty()) {
        leaveViewer();
        return;
    }
    const int index = qMin(m_current, int(m_paths.size()) - 1);
    m_filmstrip->setPictures(m_paths, index);
    showPicture(index);
}

void PhotosWindow::setAsBackground()
{
    if (m_current < 0)
        return;
    // The shell follows desktop.conf and shows it at once.
    QSettings desktop(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                      QStringLiteral("desktop"));
    desktop.setValue(QStringLiteral("Background/mode"), QStringLiteral("picture"));
    desktop.setValue(QStringLiteral("Background/picture"), m_paths.at(m_current));
}

void PhotosWindow::copyCurrent()
{
    if (m_current < 0)
        return;
    const QString path = m_paths.at(m_current);
    auto *data = new QMimeData;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    data->setImageData(reader.read());
    data->setUrls({QUrl::fromLocalFile(path)});
    QGuiApplication::clipboard()->setMimeData(data);
}

void PhotosWindow::showInFiles()
{
    if (m_current < 0)
        return;
    const QString path = m_paths.at(m_current);
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    message << QStringList{QUrl::fromLocalFile(path).toString()} << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, path] {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        watcher->deleteLater();
    });
}

void PhotosWindow::saveRotation()
{
    QString error;
    if (!m_view->saveRotation(&error)) {
        QMessageBox::warning(this, tr("Photos"), tr("The rotation could not be saved: %1").arg(error));
        return;
    }
    m_saveRotation->hide();
    updateInfo();
}

void PhotosWindow::showMoreMenu()
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    menu->addAction(tr("Copy"), QKeySequence::Copy, this, &PhotosWindow::copyCurrent);
    menu->addAction(tr("Show in Files"), this, &PhotosWindow::showInFiles);
    menu->addAction(tr("Set as desktop background"), this, &PhotosWindow::setAsBackground);
    menu->addSeparator();
    menu->addAction(tr("Actual size"), QKeySequence(QStringLiteral("Ctrl+1")), this, [this] { m_view->zoomTo(1.0); });
    menu->addAction(isFullScreen() ? tr("Leave full screen") : tr("Full screen"), QKeySequence(Qt::Key_F11), this,
                    &PhotosWindow::toggleFullScreen);
    const QPoint at = mapToGlobal(QPoint(width() - menu->sizeHint().width() - 16, 52));
    menu->popup(at);
}

void PhotosWindow::toggleInfo()
{
    m_info->setVisible(!m_info->isVisible());
    photosSettings().setValue(QStringLiteral("details"), m_info->isVisible());
    updateInfo();
    QTimer::singleShot(0, this, &PhotosWindow::placeArrows);
}

void PhotosWindow::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void PhotosWindow::startSlideshow()
{
    if (m_paths.isEmpty())
        return;
    m_wasFullScreen = isFullScreen();
    m_filmstrip->hide();
    m_info->hide();
    m_previous->hide();
    m_next->hide();
    showFullScreen();
    m_slideshow->start();
}

void PhotosWindow::stopSlideshow()
{
    m_slideshow->stop();
    m_filmstrip->show();
    m_info->setVisible(photosSettings().value(QStringLiteral("details"), false).toBool());
    if (!m_wasFullScreen)
        showNormal();
    showPicture(m_current);
}

void PhotosWindow::updateInfo()
{
    if (!m_info->isVisible() || m_current < 0)
        return;
    const QFileInfo file(m_paths.at(m_current));
    const QSize size = m_view->pictureSize();
    const QLocale locale;
    auto row = [](const QString &name, const QString &value) {
        return QStringLiteral("<p style='margin:0 0 12px 0'><span style='color:%1'>%2</span><br>%3</p>")
            .arg(Ui::Theme::current().muted.name(), name.toHtmlEscaped(), value.toHtmlEscaped());
    };
    QString html;
    html += row(tr("Name"), file.fileName());
    if (size.isValid())
        html += row(tr("Dimensions"), tr("%1 × %2 · %3 MP")
                                          .arg(size.width())
                                          .arg(size.height())
                                          .arg(locale.toString(size.width() * qint64(size.height()) / 1e6, 'f', 1)));
    html += row(tr("Size"), locale.formattedDataSize(file.size(), 1, QLocale::DataSizeTraditionalFormat));
    html += row(tr("Type"), formatName(file.filePath()));
    const QDateTime modified = file.lastModified();
    html += row(tr("Modified"), locale.toString(modified.date(), QLocale::LongFormat) + QStringLiteral(", ")
                                    + locale.toString(modified.time(), QLocale::ShortFormat));
    html += row(tr("Folder"), QDir::toNativeSeparators(file.absolutePath()));
    m_infoText->setText(html);
}

void PhotosWindow::updateZoomLabel()
{
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_view->scale() * 100)));
    m_zoomLabel->update();
}

void PhotosWindow::placeArrows()
{
    const int y = (m_stage->height() - m_previous->height()) / 2;
    m_previous->move(16, y);
    m_next->move(m_stage->width() - m_next->width() - 16, y);
}

void PhotosWindow::resizeEvent(QResizeEvent *event)
{
    KaryoWindow::resizeEvent(event);
    QTimer::singleShot(0, this, &PhotosWindow::placeArrows);
}
