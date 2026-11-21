// SPDX-License-Identifier: GPL-3.0-or-later
#include "command.h"
#include <QCoreApplication>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <memory>
namespace Karyo::Apps
{
QString shellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

void runCommand(QObject *context, const QString &program, const QStringList &args,
    const std::function<void(const QString &)> &done, const std::function<void(const QString &)> &failed,
    int timeoutMs)
{
    struct State
    {
        QByteArray output, errors;
        bool completed = false, timedOut = false, overflow = false;
    };
    const auto state = std::make_shared<State>();
    const QPointer<QObject> owner(context ? context : QCoreApplication::instance());
    // Reap asynchronously after page destruction rather than running callbacks
    // from QProcess destruction while the page is already being torn down.
    auto *process = new QProcess(QCoreApplication::instance());
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    process->setProcessEnvironment(environment);
    QObject::connect(owner, &QObject::destroyed, process, [process] { process->kill(); });
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    const auto finish = [process, deadline, state, done, failed, owner](bool success, QString message)
    {
        if (state->completed)
            return;
        state->completed = true;
        deadline->stop();
        if (state->timedOut)
            message = QCoreApplication::translate("SettingsPage", "The command timed out.");
        else if (state->overflow)
            message = QCoreApplication::translate("SettingsPage", "The command returned too much data.");
        else if (!success && message.isEmpty())
            message = QString::fromLocal8Bit(state->errors).trimmed();
        if (!success && message.isEmpty())
            message = QCoreApplication::translate("SettingsPage", "The command failed.");
        process->deleteLater(); // The callback may delete its owning page.
        if (!owner)
            return;
        if (success && done)
            done(QString::fromUtf8(state->output));
        else if (!success && failed)
            failed(message);
    };
    const auto drain = [process, state]
    {
        const QByteArray output = process->readAllStandardOutput();
        const QByteArray errors = process->readAllStandardError();
        constexpr qsizetype OutputLimit = 16 * 1024 * 1024, ErrorLimit = 256 * 1024;
        if (state->output.size() + output.size() > OutputLimit
            || state->errors.size() + errors.size() > ErrorLimit)
        {
            state->overflow = true;
            process->kill();
        }
        else
        {
            state->output += output;
            state->errors += errors;
        }
    };
    QObject::connect(process, &QProcess::readyReadStandardOutput, process, drain);
    QObject::connect(process, &QProcess::readyReadStandardError, process, drain);
    QObject::connect(process, &QProcess::finished, process,
        [state, drain, finish](int code, QProcess::ExitStatus status)
        {
            drain();
            finish(code == 0 && status == QProcess::NormalExit && !state->timedOut && !state->overflow, { });
        });
    QObject::connect(process, &QProcess::errorOccurred, process,
        [process, finish](QProcess::ProcessError error)
        {
            if (error == QProcess::FailedToStart)
                finish(false, process->errorString());
        });
    QObject::connect(deadline, &QTimer::timeout, process,
        [process, state]
        {
            state->timedOut = true;
            process->kill();
        });
    QTimer::singleShot(0, process,
        [process, deadline, state, program, args, timeoutMs, owner]
        {
            if (!owner)
            {
                process->deleteLater();
                return;
            }
            process->start(program, args);
            if (!state->completed)
                deadline->start(qMax(1, timeoutMs));
        });
}

}
