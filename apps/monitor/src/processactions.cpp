// SPDX-License-Identifier: GPL-3.0-or-later
#include "processactions.h"
#include <QFile>
#include <cerrno>
#include <cstring>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {
bool matches(const ProcessInfo &process, QString *error)
{
    if (process.pid <= 1 || process.startTicks == 0) {
        if (error) *error = QObject::tr("The process is not a valid task.");
        return false;
    }
    QFile file(QStringLiteral("/proc/%1/stat").arg(process.pid));
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray stat = file.readAll();
        const auto fields = stat.mid(stat.lastIndexOf(')') + 2).split(' ');
        if (fields.size() > 19 && fields.at(19).toULongLong() == process.startTicks)
            return true;
    }
    if (error) *error = QObject::tr("The process has exited or changed. Refresh and try again.");
    return false;
}
bool systemError(QString *error)
{
    if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
}
}

bool ProcessActions::signal(const ProcessInfo &process, int signal, QString *error)
{
    if (process.pid <= 1)
        return matches(process, error);
    // Open the handle first: a PID recycled after validation cannot receive the signal.
    const int fd = int(syscall(SYS_pidfd_open, process.pid, 0U));
    if (fd < 0)
        return systemError(error);
    const bool same = matches(process, error);
    const int result = same ? int(syscall(SYS_pidfd_send_signal, fd, signal, nullptr, 0U)) : -1;
    const int saved = errno;
    close(fd);
    errno = saved;
    return same && (result == 0 || systemError(error));
}

bool ProcessActions::efficiency(const ProcessInfo &process, QString *error)
{
    // Linux setpriority has no pidfd variant. Validate immediately before the
    // ordinary unprivileged operation and retain the kernel permission checks.
    if (!matches(process, error))
        return false;
    return setpriority(PRIO_PROCESS, id_t(process.pid), 19) == 0 || systemError(error);
}
