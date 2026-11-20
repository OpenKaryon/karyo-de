// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <karyo/ui/window.h>

#include <QAudioInput>
#include <QCamera>
#include <QImageCapture>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaRecorder>
#include <QStandardPaths>

class QLabel;
class QStackedWidget;
class QVideoWidget;

class CameraWindow final : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    CameraWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void updateCamera();
    void updateMicrophone();
    void updateControls();
    void takePhoto();
    void toggleRecording();
    void recordingFinished();
    QString capturePath(QStandardPaths::StandardLocation location, const QString &suffix);
    void showSaved(const QString &path);

    QMediaDevices m_devices;
    QCamera m_camera;
    QAudioInput m_microphone;
    QImageCapture m_photo;
    QMediaRecorder m_recorder;
    QMediaCaptureSession m_session;

    QStackedWidget *m_preview;
    QVideoWidget *m_video;
    QLabel *m_placeholder;
    QLabel *m_status;
    QLabel *m_recordingTime;
    QLabel *m_inputName;
    Karyo::Ui::PillButton *m_photoButton;
    Karyo::Ui::PillButton *m_recordButton;
    Karyo::Ui::PillButton *m_openButton;

    QString m_lastCapture;
    bool m_photoPending = false;
    bool m_recording = false;
    bool m_finalizing = false;
    bool m_closePending = false;
    QString m_recordingError;
};
