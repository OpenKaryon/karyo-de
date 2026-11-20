// SPDX-License-Identifier: GPL-3.0-or-later
#include "camerawindow.h"

#include <karyo/ui/theme.h>

#include <QAudioDevice>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMediaFormat>
#include <QMimeType>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace Ui = Karyo::Ui;

CameraWindow::CameraWindow()
{
    setWindowTitle(tr("Camera"));
    setBodyColor(Ui::Theme::current().pane);

    titleLayout()->setContentsMargins(16, 0, 8, 0);
    titleLayout()->setSpacing(10);
    auto *icon = new QLabel;
    icon->setPixmap(QIcon::fromTheme(QStringLiteral("camera-photo")).pixmap(22));
    titleLayout()->addWidget(icon);
    auto *title = new QLabel(windowTitle());
    title->setFont(Ui::bodyFont(14, QFont::DemiBold));
    titleLayout()->addWidget(title);
    titleLayout()->addStretch();

    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(18, 6, 18, 16);
    layout->setSpacing(12);

    m_preview = new QStackedWidget;
    m_preview->setMinimumSize(320, 240);
    m_preview->setStyleSheet(QStringLiteral("QStackedWidget { background: #161616; border-radius: 12px; }"));
    m_placeholder = new QLabel(tr("Looking for a camera…"));
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->setTextFormat(Qt::PlainText);
    m_placeholder->setStyleSheet(QStringLiteral("color: #eee; padding: 16px;"));
    m_video = new QVideoWidget;
    m_video->setAspectRatioMode(Qt::KeepAspectRatio);
    m_preview->addWidget(m_placeholder);
    m_preview->addWidget(m_video);
    layout->addWidget(m_preview, 1);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    m_photoButton = new Ui::PillButton(QStringLiteral("image"), tr("Take photo"));
    m_photoButton->setKind(Ui::PillButton::Kind::Secondary);
    m_recordButton = new Ui::PillButton(QStringLiteral("film"), tr("Record video"));
    m_recordButton->setKind(Ui::PillButton::Kind::Primary);
    m_openButton = new Ui::PillButton(QStringLiteral("folder-open"), tr("Open folder"));
    m_recordingTime = new QLabel;
    m_recordingTime->setFont(Ui::bodyFont(13, QFont::DemiBold));
    m_recordingTime->hide();
    actions->addWidget(m_photoButton);
    actions->addWidget(m_recordButton);
    actions->addWidget(m_recordingTime);
    actions->addStretch();
    actions->addWidget(m_openButton);
    layout->addLayout(actions);

    m_inputName = new QLabel;
    m_inputName->setTextFormat(Qt::PlainText);
    m_inputName->setWordWrap(true);
    m_status = new QLabel(tr("Photos go to Pictures and recordings go to Videos."));
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_inputName);
    layout->addWidget(m_status);
    setCentralWidget(body);
    resizeToFit({820, 590});

    m_session.setVideoOutput(m_video);
    m_session.setCamera(&m_camera);
    m_session.setImageCapture(&m_photo);
    m_session.setRecorder(&m_recorder);
    connect(&m_devices, &QMediaDevices::videoInputsChanged, this, &CameraWindow::updateCamera);
    connect(&m_devices, &QMediaDevices::audioInputsChanged, this, &CameraWindow::updateMicrophone);
    connect(&m_camera, &QCamera::activeChanged, this, [this](bool active) {
        m_preview->setCurrentWidget(active ? static_cast<QWidget *>(m_video) : m_placeholder);
        updateControls();
    });
    connect(&m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error, const QString &error) {
        m_placeholder->setText(tr("Camera unavailable: %1").arg(error));
        m_preview->setCurrentWidget(m_placeholder);
        if (m_recording) {
            m_recordingError = error;
            m_recorder.stop();
        }
        updateControls();
    });
    connect(&m_photo, &QImageCapture::readyForCaptureChanged, this, &CameraWindow::updateControls);
    connect(&m_photo, &QImageCapture::imageSaved, this, [this](int, const QString &path) {
        m_photoPending = false;
        showSaved(path);
        updateControls();
    });
    connect(&m_photo, &QImageCapture::errorOccurred, this,
            [this](int, QImageCapture::Error, const QString &error) {
        m_photoPending = false;
        m_status->setText(tr("Could not take the photo: %1").arg(error));
        updateControls();
    });
    connect(&m_recorder, &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState)
            recordingFinished();
        else if (state == QMediaRecorder::RecordingState)
            m_status->setText(tr("Recording video with sound…"));
    });
    connect(&m_recorder, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &error) {
        m_recordingError = error;
        m_status->setText(tr("Could not record video: %1").arg(error));
        if (m_recorder.recorderState() == QMediaRecorder::StoppedState)
            recordingFinished();
        else
            m_recorder.stop();
    });
    connect(&m_recorder, &QMediaRecorder::durationChanged, this, [this](qint64 duration) {
        const qint64 seconds = duration / 1000;
        m_recordingTime->setText(QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0'))
                                                       .arg(seconds % 60, 2, 10, QLatin1Char('0')));
    });
    connect(m_photoButton, &QAbstractButton::clicked, this, &CameraWindow::takePhoto);
    connect(m_recordButton, &QAbstractButton::clicked, this, &CameraWindow::toggleRecording);
    connect(m_openButton, &QAbstractButton::clicked, this, [this] {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastCapture).absolutePath())))
            m_status->setText(tr("Could not open the folder."));
    });

    updateMicrophone();
    updateCamera();
}

void CameraWindow::updateCamera()
{
    const auto cameras = QMediaDevices::videoInputs();
    if (m_camera.isActive() && cameras.contains(m_camera.cameraDevice()))
        return;
    if (m_recording) {
        m_recordingError = tr("The camera was disconnected.");
        m_recorder.stop();
    }
    m_camera.stop();
    const auto camera = QMediaDevices::defaultVideoInput();
    m_placeholder->setText(camera.isNull() ? tr("No camera detected.") : tr("Starting camera…"));
    m_preview->setCurrentWidget(m_placeholder);
    if (!camera.isNull()) {
        m_camera.setCameraDevice(camera);
        m_camera.start();
    }
    updateControls();
}

void CameraWindow::updateMicrophone()
{
    const auto microphones = QMediaDevices::audioInputs();
    if (m_recording) {
        if (microphones.contains(m_microphone.device()))
            return;
        m_recordingError = tr("The microphone was disconnected.");
        m_recorder.stop();
    }
    const auto microphone = QMediaDevices::defaultAudioInput();
    m_microphone.setDevice(microphone);
    m_inputName->setText(microphone.isNull() ? tr("No microphone detected.")
                                            : tr("Microphone: %1").arg(microphone.description()));
    updateControls();
}

void CameraWindow::updateControls()
{
    const bool cameraReady = m_camera.isActive() && m_camera.error() == QCamera::NoError;
    m_photoButton->setEnabled(cameraReady && m_photo.isReadyForCapture()
                              && !m_recording && !m_photoPending);
    m_recordButton->setEnabled(!m_finalizing && (m_recording || (cameraReady
                               && !m_photoPending && !m_microphone.device().isNull())));
    m_recordButton->setText(m_recording ? tr("Stop recording") : tr("Record video"));
    m_recordButton->setGlyph(m_recording ? QStringLiteral("square") : QStringLiteral("film"));
    m_recordingTime->setVisible(m_recording);
    m_openButton->setEnabled(!m_lastCapture.isEmpty());
}

QString CameraWindow::capturePath(QStandardPaths::StandardLocation location, const QString &suffix)
{
    QString folder = QStandardPaths::writableLocation(location);
    const QDir home(QDir::homePath());
    if (folder.isEmpty() || QDir(folder) == home)
        folder = home.filePath(location == QStandardPaths::PicturesLocation
                               ? QStringLiteral("Pictures") : QStringLiteral("Videos"));
    const QDir dir(folder);
    if (!QDir().mkpath(dir.absolutePath())) {
        m_status->setText(tr("Could not create the folder: %1").arg(dir.absolutePath()));
        return {};
    }
    const QString base = QStringLiteral("Karyo-%1").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz")));
    QString path = dir.filePath(base + QLatin1Char('.') + suffix);
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = dir.filePath(QStringLiteral("%1-%2.%3").arg(base).arg(n).arg(suffix));
    return path;
}

void CameraWindow::takePhoto()
{
    const QString path = capturePath(QStandardPaths::PicturesLocation,
                                     QStringLiteral("jpg"));
    if (path.isEmpty())
        return;
    m_photoPending = true;
    m_status->setText(tr("Saving photo…"));
    m_photo.setFileFormat(QImageCapture::JPEG);
    m_photo.captureToFile(path);
    updateControls();
}

void CameraWindow::toggleRecording()
{
    if (m_recording) {
        m_finalizing = true;
        m_status->setText(tr("Saving video…"));
        updateControls();
        m_recorder.stop();
        return;
    }
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    format.resolveForEncoding(QMediaFormat::RequiresVideo);
    const QString suffix = format.mimeType().preferredSuffix();
    if (suffix.isEmpty() || format.audioCodec() == QMediaFormat::AudioCodec::Unspecified
        || format.videoCodec() == QMediaFormat::VideoCodec::Unspecified) {
        m_status->setText(tr("No supported format for recording video with sound."));
        return;
    }
    const QString path = capturePath(QStandardPaths::MoviesLocation, suffix);
    if (path.isEmpty())
        return;
    m_recording = true;
    m_recordingError.clear();
    m_recordingTime->setText(QStringLiteral("00:00"));
    m_status->setText(tr("Starting recording…"));
    m_recorder.setMediaFormat(format);
    m_recorder.setQuality(QMediaRecorder::NormalQuality);
    m_recorder.setOutputLocation(QUrl::fromLocalFile(path));
    m_session.setAudioInput(&m_microphone);
    updateControls();
    m_recorder.record();
}

void CameraWindow::recordingFinished()
{
    if (!m_recording)
        return;
    m_recording = false;
    m_finalizing = false;
    m_session.setAudioInput(nullptr);
    const QString path = m_recorder.actualLocation().toLocalFile();
    if (!m_recordingError.isEmpty())
        m_status->setText(tr("Could not record video: %1").arg(m_recordingError));
    else if (!path.isEmpty() && QFileInfo(path).size() > 0)
        showSaved(path);
    else
        m_status->setText(tr("The video could not be saved."));
    updateControls();
    if (m_closePending)
        QTimer::singleShot(0, this, &QWidget::close);
}

void CameraWindow::showSaved(const QString &path)
{
    m_lastCapture = path;
    m_status->setText(tr("Saved to %1").arg(QDir::toNativeSeparators(path)));
}

void CameraWindow::closeEvent(QCloseEvent *event)
{
    if (!m_recording) {
        KaryoWindow::closeEvent(event);
        return;
    }
    event->ignore();
    m_closePending = true;
    if (!m_finalizing)
        toggleRecording();
}
