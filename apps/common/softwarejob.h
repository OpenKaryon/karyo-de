// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>
class QEventLoopLocker;
namespace Karyo::Apps
{
// A package transaction belongs to the application, not a page/query context.
// No timeout/cancel kills a running installer; the quit lock keeps normal GUI
// closure from interrupting it. Callers may refuse window closure while active.
class SoftwareJob : public QObject
{
    Q_OBJECT
public:
    static SoftwareJob *start(const QString &program, const QStringList &arguments);
    static SoftwareJob *startApt(const QString &operation, const QStringList &packages = { });
    static bool hasActiveJobs();
Q_SIGNALS:
    void progress(int percent);
    void finished(bool success, const QString &error);

private:
    SoftwareJob();
    ~SoftwareJob() override;
    void drain();
    void complete(bool success, QString error = { });
    QProcess *m_process;
    QEventLoopLocker *m_quitLock;
    QByteArray m_lines, m_errors, m_outputTail;
    bool m_finished = false;
    int m_progress = 0;
};
}
