// SPDX-License-Identifier: GPL-3.0-or-later
#include "sampler.h"
#include "processmodel.h"
#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <unistd.h>

class SamplerTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_root;
    void put(const QString &path, const QByteArray &data)
    {
        const QString target = m_root.filePath(path);
        QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));
        QFile file(target);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
    void process(int pid, int parent, quint64 ticks, quint64 start)
    {
        QList<QByteArray> fields(22, QByteArray("0"));
        fields[0] = "S";
        fields[1] = QByteArray::number(parent);
        fields[11] = QByteArray::number(ticks);
        fields[17] = "2";
        fields[19] = QByteArray::number(start);
        fields[21] = "10";
        put(QStringLiteral("proc/%1/stat").arg(pid), QByteArray::number(pid) + " (sample (job)) " + fields.join(' '));
        put(QStringLiteral("proc/%1/cmdline").arg(pid), QByteArray("mock-worker\0--work", 18));
        put(QStringLiteral("proc/%1/io").arg(pid), "read_bytes: " + QByteArray::number(ticks * 512) + "\nwrite_bytes: 0\n");
    }
    void counters(quint64 tick, quint64 net, quint64 disk)
    {
        put(QStringLiteral("proc/stat"), "cpu " + QByteArray::number(tick) + " 0 0 100 0 0 0 0\n");
        put(QStringLiteral("proc/net/dev"), "header\nheader\neth0: " + QByteArray::number(net) + " 0 0 0 0 0 0 0 " + QByteArray::number(net) + " 0\n");
        put(QStringLiteral("proc/diskstats"), "8 0 sda 1 0 " + QByteArray::number(disk) + " 0 1 0 " + QByteArray::number(disk) + " 0 0 " + QByteArray::number(disk) + " 0\n");
    }
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        qputenv("XDG_DATA_HOME", m_root.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_DATA_DIRS", m_root.filePath(QStringLiteral("empty")).toUtf8());
        put(QStringLiteral("proc/cpuinfo"), "model name : Fixture CPU\ncpu MHz : 1200\n");
        // Cached must match its exact key, not the suffix inside SwapCached.
        put(QStringLiteral("proc/meminfo"), "MemTotal: 1000 kB\nMemAvailable: 600 kB\nSwapCached: 999 kB\nCached: 100 kB\nBuffers: 20 kB\nSReclaimable: 10 kB\nSwapTotal: 200 kB\nSwapFree: 150 kB\n");
        put(QStringLiteral("proc/loadavg"), "0.10 0.20 0.30 1/4 999\n");
        put(QStringLiteral("proc/uptime"), "100.20 0\n");
        put(QStringLiteral("proc/mounts"), "/dev/sda1 / ext4 rw 0 0\n");
        put(QStringLiteral("sys/class/net/eth0/speed"), "1000\n");
        QVERIFY(QDir().mkpath(m_root.filePath(QStringLiteral("sys/class/net/eth0/device"))));
        put(QStringLiteral("sys/block/sda/queue/rotational"), "0\n");
    }
    void deltasResetAndPidReuse()
    {
        qint64 now = 1000;
        counters(100, 100, 10);
        process(100, 1, 100, 50);
        process(101, 100, 10, 51);
        Sampler sampler(m_root.filePath(QStringLiteral("proc")), m_root.filePath(QStringLiteral("sys")), [&] { return now; });
        sampler.setInterval(0);
        QCOMPARE(sampler.cpuModel(), QStringLiteral("Fixture CPU"));
        QCOMPARE(sampler.memCached(), 130 * 1024);
        QCOMPARE(sampler.memoryPercent(), 40.0);
        QCOMPARE(sampler.processes().value(100).name, QStringLiteral("sample (job)"));
        QCOMPARE(sampler.processes().value(100).rssBytes, 10 * sysconf(_SC_PAGESIZE));
        QCOMPARE(sampler.disks().size(), 1);
        QCOMPARE(sampler.interfaces().size(), 1);
        QCOMPARE(sampler.disks().first().mountPoint, QStringLiteral("/"));
        now += 1000;
        counters(200, 300, 30);
        process(100, 1, 150, 50);
        sampler.refresh();
        QCOMPARE(sampler.cpuPercent(), 100.0);
        QCOMPARE(sampler.interfaces().first().rxBytesPerSec, 200.0);
        QCOMPARE(sampler.disks().first().readBytesPerSec, 20 * 512.0);
        QVERIFY(sampler.processes().value(100).cpuPercent > 0);
        // Counter resets and a reused PID cannot be interpreted as huge deltas.
        now += 1000;
        counters(5, 5, 1);
        process(100, 1, 1, 999);
        sampler.refresh();
        QCOMPARE(sampler.cpuPercent(), 0.0);
        QCOMPARE(sampler.interfaces().first().rxBytesPerSec, 0.0);
        QCOMPARE(sampler.disks().first().readBytesPerSec, 0.0);
        QCOMPARE(sampler.processes().value(100).cpuPercent, 0.0);
        QCOMPARE(sampler.processes().value(100).startTicks, quint64(999));
        ProcessModel model(&sampler);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(model.indexForKey(QStringLiteral("pid:100:999")).isValid());
        model.sort(ProcessModel::Cpu, Qt::DescendingOrder);
        model.setFilter(QStringLiteral("absent-filter"));
        QCOMPARE(model.backgroundCount(), 0);
        model.setFilter(QString());
        QVERIFY(model.indexForKey(QStringLiteral("pid:100:999")).isValid());
        QVERIFY(QDir(m_root.filePath(QStringLiteral("proc/101"))).removeRecursively());
        sampler.refresh();
        QVERIFY(!sampler.processes().contains(101));
    }
    void liveReadOnlySnapshot()
    {
        Sampler sampler;
        sampler.setInterval(0);
        QVERIFY(sampler.memTotal() > 0);
        QVERIFY(sampler.cpuCount() >= 1);
        QVERIFY(sampler.processes().contains(getpid()));
    }
};
QTEST_MAIN(SamplerTest)
#include "samplertest.moc"
