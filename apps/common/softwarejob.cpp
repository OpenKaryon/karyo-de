// SPDX-License-Identifier: GPL-3.0-or-later
#include "softwarejob.h"
#include <QCoreApplication>
#include <QEventLoopLocker>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <cmath>

namespace Karyo::Apps
{
namespace
{
    QSet<SoftwareJob *> active;
    constexpr qsizetype ErrorLimit = 256 * 1024, LineLimit = 64 * 1024;
    void appendTail(QByteArray &buffer, const QByteArray &part, qsizetype limit)
    {
        buffer.append(part);
        if (buffer.size() > limit)
            buffer.remove(0, buffer.size() - limit);
    }
}
SoftwareJob::SoftwareJob()
    : QObject(QCoreApplication::instance())
    , m_process(new QProcess(this))
    , m_quitLock(new QEventLoopLocker)
{
    active.insert(this);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    m_process->setProcessEnvironment(environment);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &SoftwareJob::drain);
    connect(m_process, &QProcess::readyReadStandardError, this, &SoftwareJob::drain);
    connect(m_process, &QProcess::finished, this,
        [this](int code, QProcess::ExitStatus status)
        {
            drain();
            QString error;
            if (code == 126)
                error = tr("Authentication was cancelled.");
            else if (code == 127)
                error = tr("Authorization failed or the package tool could not start.");
            complete(code == 0 && status == QProcess::NormalExit, error);
        });
    connect(m_process, &QProcess::errorOccurred, this,
        [this](QProcess::ProcessError error)
        {
            if (error == QProcess::FailedToStart)
                complete(false, m_process->errorString());
        });
}
SoftwareJob::~SoftwareJob()
{
    active.remove(this);
    delete m_quitLock;
}
bool SoftwareJob::hasActiveJobs() { return !active.isEmpty(); }
SoftwareJob *SoftwareJob::start(const QString &program, const QStringList &arguments)
{
    auto *job = new SoftwareJob;
    QTimer::singleShot(0, job, [job, program, arguments] { job->m_process->start(program, arguments); });
    return job;
}
SoftwareJob *SoftwareJob::startApt(const QString &operation, const QStringList &packages)
{
    return start(QStringLiteral("pkexec"),
        QStringList { QString::fromUtf8(KARYO_APT_HELPER_PATH), operation } + packages);
}
void SoftwareJob::drain()
{
    appendTail(m_errors, m_process->readAllStandardError(), ErrorLimit);
    const auto output = m_process->readAllStandardOutput();
    appendTail(m_outputTail, output, LineLimit);
    // Discard oversized unstructured lines rather than buffering installer logs.
    appendTail(m_lines, output, LineLimit);
    static const QRegularExpression status(QStringLiteral("^(pmstatus|dlstatus):[^:]*:([0-9.]+):"));
    qsizetype newline;
    while ((newline = m_lines.indexOf('\n')) >= 0)
    {
        const auto line = m_lines.left(newline);
        m_lines.remove(0, newline + 1);
        const auto match = status.match(QString::fromUtf8(line));
        if (!match.hasMatch())
            continue;
        bool valid = false;
        const double percent = match.captured(2).toDouble(&valid);
        if (!valid || !std::isfinite(percent))
            continue;
        const double bounded = qBound(0.0, percent, 100.0);
        const int next = match.captured(1) == QLatin1String("pmstatus") ? 33 + qRound(bounded * .67)
                                                                        : qRound(bounded / 3);
        if (qMin(next, 99) > m_progress)
        {
            m_progress = qMin(next, 99);
            Q_EMIT progress(m_progress);
        }
    }
}
void SoftwareJob::complete(bool success, QString error)
{
    if (m_finished)
        return;
    m_finished = true;
    if (!success && error.isEmpty())
        error = QString::fromUtf8(m_errors).trimmed();
    if (!success && error.isEmpty())
        error = QString::fromUtf8(m_outputTail).trimmed();
    if (!success && error.isEmpty())
        error = tr("The package operation failed.");
    if (success)
        Q_EMIT progress(100);
    active.remove(this);
    Q_EMIT finished(success, error);
    deleteLater();
}
}
