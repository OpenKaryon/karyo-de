// SPDX-License-Identifier: GPL-3.0-or-later
#include "pickerwindow.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/theme.h>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>
#include <unistd.h>

namespace Ui = Karyo::Ui;
namespace {
constexpr qsizetype ProbeLimit = 65536;
constexpr int OutputLimit = 200000;
QLabel *plainLabel(const QString &text, QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}
}

MacOSPicker::MacOSPicker(const QString &helper, const QString &initial,
                         const QStringList &arguments, QWidget *parent)
    : Ui::KaryoWindow(parent), m_helper(helper), m_arguments(arguments),
      m_autoLaunch(!initial.isEmpty())
{
    setWindowTitle(tr("macOS Applications"));
    setBodyColor(Ui::Theme::current().pane);
    titleLayout()->setContentsMargins(16, 0, 8, 0);
    auto *icon = new QLabel;
    icon->setPixmap(Ui::icon(QStringLiteral("app-window"), 22, Ui::Theme::current().accent, devicePixelRatioF()));
    titleLayout()->addWidget(icon);
    titleLayout()->addWidget(plainLabel(windowTitle()));
    titleLayout()->addStretch();

    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(20, 8, 20, 20);
    layout->setSpacing(12);
    auto *heading = plainLabel(tr("Run an Intel macOS application"));
    heading->setFont(Ui::bodyFont(17, QFont::DemiBold));
    layout->addWidget(heading);
    layout->addWidget(plainLabel(tr("Experimental compatibility through Darling. Choose an .app folder or an x86_64 Mach-O executable. Application compatibility varies.")));
    m_path = new QLineEdit(initial);
    m_path->setObjectName(QStringLiteral("applicationPath"));
    m_path->setPlaceholderText(tr("Application path"));
    m_path->setAccessibleName(tr("Application path"));
    layout->addWidget(m_path);
    auto *choose = new QHBoxLayout;
    m_bundle = new Ui::PillButton(QStringLiteral("folder-open"), tr("Choose .app…"));
    m_bundle->setObjectName(QStringLiteral("chooseBundle"));
    m_executable = new Ui::PillButton(QStringLiteral("file-text"), tr("Choose executable…"));
    m_executable->setObjectName(QStringLiteral("chooseExecutable"));
    choose->addWidget(m_bundle);
    choose->addWidget(m_executable);
    choose->addStretch();
    layout->addLayout(choose);
    m_status = plainLabel(tr("Checking Darling…"));
    m_status->setObjectName(QStringLiteral("launchStatus"));
    m_storage = plainLabel({});
    m_storage->setObjectName(QStringLiteral("storageNotice"));
    layout->addWidget(m_status);
    layout->addWidget(m_storage);
    auto *actions = new QHBoxLayout;
    m_launch = new Ui::PillButton(QStringLiteral("play"), tr("Launch"));
    m_launch->setKind(Ui::PillButton::Kind::Primary);
    m_launch->setObjectName(QStringLiteral("launchApplication"));
    m_refresh = new Ui::PillButton(QStringLiteral("refresh-cw"), tr("Check again"));
    m_refresh->setObjectName(QStringLiteral("refreshRuntime"));
    m_logs = new Ui::PillButton(QStringLiteral("folder"), tr("Open logs"));
    m_logs->setObjectName(QStringLiteral("openLogs"));
    actions->addWidget(m_launch);
    actions->addWidget(m_refresh);
    actions->addStretch();
    actions->addWidget(m_logs);
    layout->addLayout(actions);
    m_output = new QPlainTextEdit;
    m_output->setObjectName(QStringLiteral("applicationOutput"));
    m_output->setReadOnly(true);
    m_output->document()->setMaximumBlockCount(1200);
    m_output->setMinimumHeight(140);
    m_output->setPlaceholderText(tr("Application output and error details appear here."));
    layout->addWidget(m_output, 1);
    setCentralWidget(body);
    resizeToFit({760, 570});

    connect(m_bundle, &QAbstractButton::clicked, this, &MacOSPicker::chooseBundle);
    connect(m_executable, &QAbstractButton::clicked, this, &MacOSPicker::chooseExecutable);
    connect(m_launch, &QAbstractButton::clicked, this, &MacOSPicker::requestLaunch);
    connect(m_refresh, &QAbstractButton::clicked, this, &MacOSPicker::refreshRuntime);
    connect(m_logs, &QAbstractButton::clicked, this, &MacOSPicker::openLogs);
    connect(m_path, &QLineEdit::textChanged, this, [this] { updateControls(); });
    connect(m_path, &QLineEdit::textEdited, this, [this] { m_autoLaunch = false; });
    connect(m_path, &QLineEdit::returnPressed, this, [this] {
        if (m_launch->isEnabled()) requestLaunch();
    });
    connect(&m_probe, &QProcess::readyReadStandardOutput, this, &MacOSPicker::readProbe);
    connect(&m_probe, &QProcess::readyReadStandardError, this, &MacOSPicker::readProbe);
    connect(&m_probe, &QProcess::finished, this, &MacOSPicker::finishProbe);
    connect(&m_probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_purpose != Probe::None) {
            m_probeFailure = tr("Could not start the launcher: %1").arg(m_probe.errorString());
            finishProbe(-1, QProcess::CrashExit);
        }
    });
    m_probeTimer.setSingleShot(true);
    m_probeTimer.setInterval(8000);
    connect(&m_probeTimer, &QTimer::timeout, this, [this] {
        m_probeFailure = tr("The launcher did not respond. Check the installation and try again.");
        m_probe.kill();
    });
    m_runner.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_runner, &QProcess::readyReadStandardOutput, this, &MacOSPicker::readOutput);
    connect(&m_runner, &QProcess::stateChanged, this, [this] { updateControls(); });
    connect(&m_runner, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_status->setText(tr("Could not start the application: %1").arg(m_runner.errorString()));
            updateControls();
        }
    });
    connect(&m_runner, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exitStatus) {
        readOutput();
        m_status->setText(exitStatus == QProcess::NormalExit && code == 0
            ? tr("Application finished.") : tr("Application failed (code %1). See the details below and the logs.").arg(code));
        updateControls();
    });
    updateControls();
    QTimer::singleShot(0, this, &MacOSPicker::refreshRuntime);
}

void MacOSPicker::updateControls()
{
    const bool running = m_runner.state() != QProcess::NotRunning || m_runner.bytesAvailable() > 0;
    const bool checking = m_purpose != Probe::None;
    m_launch->setEnabled(!m_closing && !running && !checking && m_available && !m_path->text().trimmed().isEmpty());
    m_refresh->setEnabled(!running && !checking);
    m_path->setReadOnly(running || m_purpose == Probe::Inspect);
    m_bundle->setEnabled(!running && m_purpose != Probe::Inspect);
    m_executable->setEnabled(m_bundle->isEnabled());
    m_logs->setEnabled(!m_logFolder.isEmpty() && QDir(m_logFolder).exists());
}

void MacOSPicker::refreshRuntime()
{
    if (m_purpose != Probe::None || m_runner.state() != QProcess::NotRunning || m_closing) return;
    m_status->setText(tr("Checking Darling…"));
    startProbe(Probe::Status, {QStringLiteral("--status"), QStringLiteral("--json")});
}

void MacOSPicker::startProbe(Probe purpose, const QStringList &arguments)
{
    m_purpose = purpose;
    m_probeOut.clear(); m_probeError.clear(); m_probeFailure.clear();
    updateControls();
    m_probe.start(m_helper, arguments);
    m_probeTimer.start();
}

void MacOSPicker::readProbe()
{
    if (!m_probe.isOpen()) return;
    m_probeOut += m_probe.readAllStandardOutput();
    m_probeError += m_probe.readAllStandardError();
    if (m_probeOut.size() + m_probeError.size() > ProbeLimit && m_probeFailure.isEmpty()) {
        m_probeFailure = tr("The launcher returned too much metadata.");
        m_probe.kill();
    }
}

void MacOSPicker::finishProbe(int code, QProcess::ExitStatus exitStatus)
{
    m_probeTimer.stop();
    readProbe();
    const Probe purpose = m_purpose;
    m_purpose = Probe::None;
    if (purpose == Probe::None || m_closing) { updateControls(); return; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(m_probeOut, &parseError);
    const QJsonObject data = document.object();
    const bool common = parseError.error == QJsonParseError::NoError && document.isObject()
        && data.value(QStringLiteral("schema")).toInt() == 1
        && data.value(QStringLiteral("experimental")).toBool();
    if (!m_probeFailure.isEmpty()) {
        m_status->setText(m_probeFailure);
        if (purpose == Probe::Status) m_available = false;
        updateControls(); return;
    }
    if (purpose == Probe::Status) {
        const QJsonValue installed = data.value(QStringLiteral("installed"));
        const bool valid = common && installed.isBool() && exitStatus == QProcess::NormalExit
            && code == (installed.toBool() ? 0 : 1);
        m_available = valid && installed.toBool();
        m_status->setText(!valid ? tr("Could not read the runtime status. Check the launcher installation.")
            : m_available ? tr("Darling is available. Choose an application.")
            : tr("Darling is not installed. Install the optional karyo-darling-runtime package, then check again."));
        updateControls();
        const bool autoLaunch = m_autoLaunch && m_available;
        m_autoLaunch = false;
        if (autoLaunch) QTimer::singleShot(0, this, &MacOSPicker::requestLaunch);
        return;
    }
    if (code != 0 || exitStatus != QProcess::NormalExit) {
        m_status->setText(tr("Could not prepare the application: %1").arg(QString::fromUtf8(m_probeError).trimmed()));
        updateControls(); return;
    }
    const QString log = data.value(QStringLiteral("log")).toString();
    const bool valid = common && data.value(QStringLiteral("ownerUid")).toInteger(-1) == qint64(getuid())
        && data.value(QStringLiteral("persistent")).isBool()
        && data.value(QStringLiteral("prefixCreated")).isBool() && !data.value(QStringLiteral("prefixCreated")).toBool()
        && QFileInfo(log).isAbsolute() && data.value(QStringLiteral("runtime")).toObject().value(QStringLiteral("installed")).toBool();
    if (!valid) {
        m_status->setText(tr("Could not read the application details. Nothing was launched."));
        updateControls(); return;
    }
    m_logFolder = QFileInfo(log).absolutePath();
    m_storage->setText(data.value(QStringLiteral("persistent")).toBool() ? QString()
        : tr("Temporary profile: application data lasts only for this login session."));
    m_output->clear(); m_decoder.resetState();
    m_status->setText(tr("Running: %1").arg(data.value(QStringLiteral("name")).toString()));
    QStringList arguments{QStringLiteral("--"), m_selected, QStringLiteral("--")};
    arguments.append(m_arguments);
    m_runner.start(m_helper, arguments);
    updateControls();
}

void MacOSPicker::requestLaunch()
{
    if (!m_launch->isEnabled()) return;
    m_autoLaunch = false;
    m_selected = m_path->text();
    m_status->setText(tr("Checking the application…"));
    m_storage->clear(); m_logFolder.clear();
    startProbe(Probe::Inspect, {QStringLiteral("--inspect"), QStringLiteral("--"), m_selected});
}

void MacOSPicker::readOutput()
{
    int chunks = 0;
    while (m_runner.bytesAvailable() > 0 && chunks++ < 4) {
        const QByteArray bytes = m_runner.read(16384);
        QTextCursor cursor(m_output->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(m_decoder(bytes));
        const int extra = m_output->document()->characterCount() - OutputLimit;
        if (extra > 0) {
            QTextCursor trim(m_output->document());
            trim.setPosition(0); trim.setPosition(extra, QTextCursor::KeepAnchor); trim.removeSelectedText();
        }
        m_output->setTextCursor(cursor);
    }
    if (m_runner.bytesAvailable() > 0) QTimer::singleShot(0, this, &MacOSPicker::readOutput);
    updateControls();
}

void MacOSPicker::chooseBundle()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Choose an .app folder"), QDir::homePath());
    if (!path.isEmpty()) { m_autoLaunch = false; m_path->setText(path); updateControls(); }
}

void MacOSPicker::chooseExecutable()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose an Intel macOS executable"), QDir::homePath());
    if (!path.isEmpty()) { m_autoLaunch = false; m_path->setText(path); updateControls(); }
}

void MacOSPicker::openLogs()
{
    if (!m_logFolder.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_logFolder));
}

void MacOSPicker::closeEvent(QCloseEvent *event)
{
    if (m_runner.state() != QProcess::NotRunning) {
        m_status->setText(tr("Close the macOS application before closing this window."));
        event->ignore(); return;
    }
    m_closing = true; m_autoLaunch = false;
    if (m_probe.state() != QProcess::NotRunning) m_probe.kill();
    Ui::KaryoWindow::closeEvent(event);
}
