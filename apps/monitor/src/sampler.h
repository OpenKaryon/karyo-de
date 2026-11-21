// SPDX-License-Identifier: GPL-3.0-or-later
// Periodic snapshots of /proc and /sys: processes, CPU, memory, disks, network.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <sys/types.h>
#include <functional>

struct ProcessInfo
{
    pid_t pid = 0;
    pid_t ppid = 0;
    uid_t uid = 0;
    QString name;        // comm, or the executable basename when comm is truncated
    QString command;     // full command line
    QString exe;         // executable basename
    QString environment; // conda env (CONDA_DEFAULT_ENV) for the user's own processes
    char state = '?';
    int threads = 0;
    int nice = 0;
    qint64 rssBytes = 0;
    double cpuPercent = 0;    // of the whole machine, like Task Manager
    double diskBytesPerSec = 0;
    quint64 cpuTicks = 0;
    quint64 ioBytes = 0;
    quint64 startTicks = 0;
};

struct History
{
    QList<double> values;  // newest last, 0..1 for percentages
    void push(double value, int limit = 60)
    {
        values.append(value);
        while (values.size() > limit)
            values.removeFirst();
    }
};

struct DiskInfo
{
    QString name;        // nvme0n1, sda
    QString mountPoint;  // first mounted partition, for the label
    bool rotational = false;
    double busyPercent = 0;
    double readBytesPerSec = 0;
    double writeBytesPerSec = 0;
    quint64 ioTicks = 0;
    quint64 sectorsRead = 0;
    quint64 sectorsWritten = 0;
    History history;
};

struct NetInfo
{
    QString name;
    double rxBytesPerSec = 0;
    double txBytesPerSec = 0;
    qint64 linkMbps = 0;  // 0 if unknown
    quint64 rxBytes = 0;
    quint64 txBytes = 0;
    History history;      // fraction of link speed, or of the peak seen
    double peak = 1;
};

class Sampler : public QObject
{
    Q_OBJECT
public:
    explicit Sampler(QObject *parent = nullptr);
    // Alternate read-only sources and a monotonic clock support deterministic
    // sampling tests without touching live processes or machine counters.
    Sampler(const QString &procRoot, const QString &sysRoot,
            std::function<qint64()> clock, QObject *parent = nullptr);

    void setInterval(int msec);  // 0 pauses
    int interval() const { return m_interval; }
    void refresh();

    const QHash<pid_t, ProcessInfo> &processes() const { return m_processes; }
    double cpuPercent() const { return m_cpuPercent; }
    double cpuMhz() const { return m_cpuMhz; }
    QString cpuModel() const { return m_cpuModel; }
    int cpuCount() const { return m_cpuCount; }
    int threadCount() const { return m_threads; }
    double loadAverage(int index) const { return m_load[index]; }
    qint64 uptimeSeconds() const { return m_uptime; }
    qint64 memTotal() const { return m_memTotal; }
    qint64 memAvailable() const { return m_memAvailable; }
    qint64 memCached() const { return m_memCached; }
    qint64 swapTotal() const { return m_swapTotal; }
    qint64 swapFree() const { return m_swapFree; }
    double memoryPercent() const;
    double diskPercent() const;     // busiest disk
    double networkPercent() const;  // busiest interface, of its link speed
    const History &cpuHistory() const { return m_cpuHistory; }
    const History &memoryHistory() const { return m_memHistory; }
    const QList<DiskInfo> &disks() const { return m_disks; }
    const QList<NetInfo> &interfaces() const { return m_nets; }

Q_SIGNALS:
    void updated();

private:
    void sampleCpu(double elapsed);
    void sampleMemory();
    void sampleProcesses(double elapsed);
    void sampleDisks(double elapsed);
    void sampleNetwork(double elapsed);

    QString m_procRoot;
    QString m_sysRoot;
    std::function<qint64()> m_clock;
    QTimer m_timer;
    int m_interval = 2000;
    qint64 m_lastSample = 0;
    long m_ticksPerSecond = 100;
    QHash<pid_t, ProcessInfo> m_processes;
    quint64 m_cpuTotal = 0;
    quint64 m_cpuIdle = 0;
    double m_cpuPercent = 0;
    double m_cpuMhz = 0;
    QString m_cpuModel;
    int m_cpuCount = 1;
    int m_threads = 0;
    double m_load[3] = {0, 0, 0};
    qint64 m_uptime = 0;
    qint64 m_memTotal = 0;
    qint64 m_memAvailable = 0;
    qint64 m_memCached = 0;
    qint64 m_swapTotal = 0;
    qint64 m_swapFree = 0;
    History m_cpuHistory;
    History m_memHistory;
    QList<DiskInfo> m_disks;
    QList<NetInfo> m_nets;
};
