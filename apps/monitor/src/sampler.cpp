// SPDX-License-Identifier: GPL-3.0-or-later
#include "sampler.h"

#include <chrono>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <unistd.h>

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString readLine(const QString &path)
{
    return QString::fromLatin1(readAll(path)).trimmed();
}

qint64 meminfoValue(const QByteArray &meminfo, const char *key)
{
    const QByteArray prefix = QByteArray(key) + ':';
    for (const QByteArray &line : meminfo.split('\n')) {
        if (line.startsWith(prefix))
            return line.mid(prefix.size()).simplified().split(' ').first().toLongLong() * 1024;
    }
    return 0;
}

bool isDiskName(const QString &name)
{
    static const QRegularExpression disk(
        QStringLiteral("^(sd[a-z]+|vd[a-z]+|xvd[a-z]+|hd[a-z]+|nvme\\d+n\\d+|mmcblk\\d+)$"));
    return disk.match(name).hasMatch();
}

} // namespace

Sampler::Sampler(QObject *parent)
    : Sampler(QStringLiteral("/proc"), QStringLiteral("/sys"), [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }, parent)
{
}

Sampler::Sampler(const QString &procRoot, const QString &sysRoot,
                 std::function<qint64()> clock, QObject *parent)
    : QObject(parent), m_procRoot(QDir::cleanPath(procRoot)), m_sysRoot(QDir::cleanPath(sysRoot)),
      m_clock(std::move(clock))
{
    m_ticksPerSecond = sysconf(_SC_CLK_TCK);
    const QString cpuinfo = QString::fromLatin1(readAll((m_procRoot + QStringLiteral("/cpuinfo"))));
    static const QRegularExpression model(QStringLiteral("^model name\\s*:\\s*(.+)$"),
                                          QRegularExpression::MultilineOption);
    m_cpuModel = model.match(cpuinfo).captured(1).trimmed();
    m_cpuModel.replace(QStringLiteral("(R)"), QString()).replace(QStringLiteral("(TM)"), QString());
    m_cpuCount = qMax(1, int(sysconf(_SC_NPROCESSORS_ONLN)));
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Sampler::refresh);
    refresh();
    m_timer.start(m_interval);
}

void Sampler::setInterval(int msec)
{
    m_interval = msec;
    if (msec <= 0)
        m_timer.stop();
    else
        m_timer.start(msec);
}

void Sampler::refresh()
{
    const qint64 now = m_clock();
    const double elapsed = m_lastSample ? (now - m_lastSample) / 1000.0 : 0.0;
    m_lastSample = now;
    sampleCpu(elapsed);
    sampleMemory();
    sampleProcesses(elapsed);
    sampleDisks(elapsed);
    sampleNetwork(elapsed);
    Q_EMIT updated();
}

double Sampler::memoryPercent() const
{
    return m_memTotal ? 100.0 * (m_memTotal - m_memAvailable) / m_memTotal : 0;
}

double Sampler::diskPercent() const
{
    double busiest = 0;
    for (const DiskInfo &disk : m_disks)
        busiest = qMax(busiest, disk.busyPercent);
    return busiest;
}

double Sampler::networkPercent() const
{
    double busiest = 0;
    for (const NetInfo &net : m_nets) {
        if (net.linkMbps > 0) {
            const double bits = (net.rxBytesPerSec + net.txBytesPerSec) * 8;
            busiest = qMax(busiest, 100.0 * bits / (net.linkMbps * 1e6));
        }
    }
    return busiest;
}

void Sampler::sampleCpu(double elapsed)
{
    const QByteArray stat = readAll((m_procRoot + QStringLiteral("/stat")));
    const QList<QByteArray> first = stat.left(stat.indexOf('\n')).simplified().split(' ');
    quint64 total = 0;
    quint64 idle = 0;
    for (int i = 1; i < first.size() && i <= 8; ++i) {
        const quint64 value = first.at(i).toULongLong();
        total += value;
        if (i == 4 || i == 5)
            idle += value;
    }
    if (elapsed > 0 && total > m_cpuTotal && idle >= m_cpuIdle) {
        const double busy = double(total - m_cpuTotal) - double(idle - m_cpuIdle);
        m_cpuPercent = qBound(0.0, 100.0 * busy / double(total - m_cpuTotal), 100.0);
        m_cpuHistory.push(m_cpuPercent / 100.0);
    } else {
        m_cpuPercent = 0;
    }
    m_cpuTotal = total;
    m_cpuIdle = idle;

    // Mean current clock over all cores.
    const QString cpuinfo = QString::fromLatin1(readAll((m_procRoot + QStringLiteral("/cpuinfo"))));
    static const QRegularExpression mhz(QStringLiteral("^cpu MHz\\s*:\\s*([0-9.]+)"),
                                        QRegularExpression::MultilineOption);
    double sum = 0;
    int count = 0;
    for (auto it = mhz.globalMatch(cpuinfo); it.hasNext();) {
        sum += it.next().captured(1).toDouble();
        ++count;
    }
    m_cpuMhz = count ? sum / count : 0;

    const QStringList load = readLine((m_procRoot + QStringLiteral("/loadavg"))).split(QLatin1Char(' '));
    for (int i = 0; i < 3 && i < load.size(); ++i)
        m_load[i] = load.at(i).toDouble();
    if (load.size() > 3)
        m_threads = load.at(3).section(QLatin1Char('/'), 1).toInt();
    m_uptime = qint64(readLine((m_procRoot + QStringLiteral("/uptime"))).section(QLatin1Char(' '), 0, 0).toDouble());
}

void Sampler::sampleMemory()
{
    const QByteArray meminfo = readAll((m_procRoot + QStringLiteral("/meminfo")));
    m_memTotal = meminfoValue(meminfo, "MemTotal");
    m_memAvailable = meminfoValue(meminfo, "MemAvailable");
    m_memCached = meminfoValue(meminfo, "Cached") + meminfoValue(meminfo, "Buffers")
                  + meminfoValue(meminfo, "SReclaimable");
    m_swapTotal = meminfoValue(meminfo, "SwapTotal");
    m_swapFree = meminfoValue(meminfo, "SwapFree");
    m_memHistory.push(memoryPercent() / 100.0);
}

void Sampler::sampleProcesses(double elapsed)
{
    QHash<pid_t, ProcessInfo> next;
    next.reserve(m_processes.size() + 16);
    const uid_t self = getuid();
    const long pageSize = sysconf(_SC_PAGESIZE);
    const QStringList entries = QDir(m_procRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        const pid_t pid = entry.toInt(&numeric);
        if (!numeric)
            continue;
        const QString base = (m_procRoot + QStringLiteral("/")) + entry;
        const QByteArray stat = readAll(base + QStringLiteral("/stat"));
        const qsizetype open = stat.indexOf('(');
        const qsizetype close = stat.lastIndexOf(')');
        if (open < 0 || close < open)
            continue;
        const QList<QByteArray> f = stat.mid(close + 2).split(' ');
        if (f.size() < 22)
            continue;
        // f[n] is /proc/pid/stat field n + 3.
        ProcessInfo info;
        const auto previous = m_processes.constFind(pid);
        const bool known = previous != m_processes.constEnd()
                           && previous->startTicks == f.at(19).toULongLong();
        if (known) {
            info = *previous;
        } else {
            info.pid = pid;
            info.uid = QFileInfo(base).ownerId();
            info.startTicks = f.at(19).toULongLong();
            QByteArray cmdline = readAll(base + QStringLiteral("/cmdline"));
            while (cmdline.endsWith('\0'))
                cmdline.chop(1);
            const QList<QByteArray> argv = cmdline.split('\0');
            info.command = QString::fromLocal8Bit(cmdline).replace(QChar(u'\0'), QChar(u' '));
            info.exe = argv.isEmpty() ? QString() : QFileInfo(QString::fromLocal8Bit(argv.first())).fileName();
            info.name = QString::fromLocal8Bit(stat.mid(open + 1, close - open - 1));
            // comm is cut at 15 characters; prefer the executable's name then.
            if (info.name.size() >= 15 && info.exe.startsWith(info.name))
                info.name = info.exe;
            if (info.uid == self) {
                const QByteArray environ = readAll(base + QStringLiteral("/environ"));
                for (const QByteArray &var : environ.split('\0')) {
                    if (var.startsWith("CONDA_DEFAULT_ENV=")) {
                        info.environment = QString::fromLocal8Bit(var.mid(18));
                        break;
                    }
                }
            }
        }
        info.state = f.at(0).isEmpty() ? '?' : f.at(0).at(0);
        info.ppid = f.at(1).toInt();
        info.nice = f.at(16).toInt();
        info.threads = f.at(17).toInt();
        info.rssBytes = f.at(21).toLongLong() * pageSize;
        const quint64 ticks = f.at(11).toULongLong() + f.at(12).toULongLong();
        info.cpuPercent = (known && elapsed > 0 && ticks >= info.cpuTicks)
                              ? 100.0 * (ticks - info.cpuTicks) / m_ticksPerSecond / elapsed / m_cpuCount
                              : 0;
        info.cpuTicks = ticks;
        // Per-process I/O is readable for the user's own processes only.
        quint64 io = 0;
        if (info.uid == self) {
            const QList<QByteArray> lines = readAll(base + QStringLiteral("/io")).split('\n');
            for (const QByteArray &line : lines) {
                if (line.startsWith("read_bytes:") || line.startsWith("write_bytes:"))
                    io += line.mid(line.indexOf(':') + 1).trimmed().toULongLong();
            }
        }
        info.diskBytesPerSec = (known && elapsed > 0 && io >= info.ioBytes) ? (io - info.ioBytes) / elapsed : 0;
        info.ioBytes = io;
        next.insert(pid, info);
    }
    m_processes = std::move(next);
}

void Sampler::sampleDisks(double elapsed)
{
    QList<DiskInfo> next;
    const QString mounts = QString::fromLatin1(readAll((m_procRoot + QStringLiteral("/mounts"))));
    const QList<QByteArray> lines = readAll((m_procRoot + QStringLiteral("/diskstats"))).split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() < 14)
            continue;
        const QString name = QString::fromLatin1(f.at(2));
        if (!isDiskName(name))
            continue;
        DiskInfo disk;
        bool known = false;
        for (const DiskInfo &old : std::as_const(m_disks)) {
            if (old.name == name) {
                disk = old;
                known = true;
            }
        }
        disk.name = name;
        disk.rotational = readLine((m_sysRoot + QStringLiteral("/block/%1/queue/rotational")).arg(name)) == QLatin1String("1");
        const quint64 read = f.at(5).toULongLong();
        const quint64 written = f.at(9).toULongLong();
        const quint64 ioTicks = f.at(12).toULongLong();
        if (elapsed > 0 && known && ioTicks >= disk.ioTicks
            && read >= disk.sectorsRead && written >= disk.sectorsWritten) {
            disk.busyPercent = qBound(0.0, (ioTicks - disk.ioTicks) / (elapsed * 10.0), 100.0);
            disk.readBytesPerSec = (read - disk.sectorsRead) * 512.0 / elapsed;
            disk.writeBytesPerSec = (written - disk.sectorsWritten) * 512.0 / elapsed;
            disk.history.push(disk.busyPercent / 100.0);
        } else {
            disk.busyPercent = disk.readBytesPerSec = disk.writeBytesPerSec = 0;
        }
        disk.ioTicks = ioTicks;
        disk.sectorsRead = read;
        disk.sectorsWritten = written;
        if (disk.mountPoint.isEmpty()) {
            for (const QString &mount : mounts.split(QLatin1Char('\n'))) {
                const QStringList parts = mount.split(QLatin1Char(' '));
                // Bind mounts of single files (containers) are not a disk's place.
                if (parts.size() > 1 && parts.at(0).startsWith(QStringLiteral("/dev/") + name)
                    && QFileInfo(parts.at(1)).isDir()) {
                    disk.mountPoint = parts.at(1);
                    if (disk.mountPoint == QLatin1String("/"))
                        break;
                }
            }
        }
        next.append(disk);
    }
    m_disks = next;
}

void Sampler::sampleNetwork(double elapsed)
{
    QList<NetInfo> next;
    const QList<QByteArray> lines = readAll((m_procRoot + QStringLiteral("/net/dev"))).split('\n');
    for (const QByteArray &line : lines.mid(2)) {
        const qsizetype colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QString name = QString::fromLatin1(line.left(colon).trimmed());
        // Physical interfaces only: lo, bridges and veth pairs have no device.
        if (name == QLatin1String("lo") || !QFileInfo::exists((m_sysRoot + QStringLiteral("/class/net/%1/device")).arg(name)))
            continue;
        const QList<QByteArray> f = line.mid(colon + 1).simplified().split(' ');
        if (f.size() < 9)
            continue;
        NetInfo net;
        bool known = false;
        for (const NetInfo &old : std::as_const(m_nets)) {
            if (old.name == name) {
                net = old;
                known = true;
            }
        }
        net.name = name;
        const quint64 rx = f.at(0).toULongLong();
        const quint64 tx = f.at(8).toULongLong();
        bool ok = false;
        const qint64 speed = readLine((m_sysRoot + QStringLiteral("/class/net/%1/speed")).arg(name)).toLongLong(&ok);
        net.linkMbps = ok && speed > 0 ? speed : 0;
        if (elapsed > 0 && known && rx >= net.rxBytes && tx >= net.txBytes) {
            net.rxBytesPerSec = (rx - net.rxBytes) / elapsed;
            net.txBytesPerSec = (tx - net.txBytes) / elapsed;
            const double rate = net.rxBytesPerSec + net.txBytesPerSec;
            net.peak = qMax(net.peak, rate);
            const double capacity = net.linkMbps > 0 ? net.linkMbps * 1e6 / 8 : net.peak;
            net.history.push(qBound(0.0, rate / capacity, 1.0));
        } else {
            net.rxBytesPerSec = net.txBytesPerSec = 0;
        }
        net.rxBytes = rx;
        net.txBytes = tx;
        next.append(net);
    }
    m_nets = next;
}
