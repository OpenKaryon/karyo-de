// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <karyo/ui/window.h>
#include <QByteArray>
#include <QProcess>
#include <QStringDecoder>
#include <QTimer>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QCloseEvent;

class MacOSPicker final : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    MacOSPicker(const QString &helper, const QString &initial = {},
                const QStringList &arguments = {}, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class Probe { None, Status, Inspect };
    void refreshRuntime();
    void requestLaunch();
    void startProbe(Probe purpose, const QStringList &arguments);
    void readProbe();
    void finishProbe(int code, QProcess::ExitStatus exitStatus);
    void readOutput();
    void updateControls();
    void chooseBundle();
    void chooseExecutable();
    void openLogs();

    QString m_helper;
    QStringList m_arguments;
    QString m_selected;
    QString m_logFolder;
    QProcess m_probe;
    QProcess m_runner;
    QTimer m_probeTimer;
    Probe m_purpose = Probe::None;
    QByteArray m_probeOut;
    QByteArray m_probeError;
    QString m_probeFailure;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    bool m_available = false;
    bool m_autoLaunch = false;
    bool m_closing = false;
    QLineEdit *m_path;
    QLabel *m_status;
    QLabel *m_storage;
    QPlainTextEdit *m_output;
    Karyo::Ui::PillButton *m_bundle;
    Karyo::Ui::PillButton *m_executable;
    Karyo::Ui::PillButton *m_launch;
    Karyo::Ui::PillButton *m_refresh;
    Karyo::Ui::PillButton *m_logs;
};
