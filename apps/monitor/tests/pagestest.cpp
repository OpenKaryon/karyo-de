// SPDX-License-Identifier: GPL-3.0-or-later
#include "processactions.h"
#include "processespage.h"
#include "performancepage.h"
#include "tablepages.h"
#include <QAbstractButton>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QSettings>
#include <QFileInfo>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <csignal>
#include <sys/resource.h>

class PagesTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_root;
    QByteArray m_systemEntry;
    void write(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
    QByteArray read(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    QAbstractButton *button(QWidget &page, const QString &text)
    {
        for (auto *button : page.findChildren<QAbstractButton *>())
            if (button->text() == text) return button;
        return nullptr;
    }
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        qputenv("XDG_CONFIG_HOME", m_root.filePath(QStringLiteral("user")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_root.filePath(QStringLiteral("system")).toUtf8());
        m_systemEntry = "[Desktop Entry]\nType=Application\nName=Startup fixture\nExec=true\nX-GNOME-Autostart-enabled=false\nActions=Extra;\n\n[Desktop Action Extra]\nName=Extra\nExec=true\nHidden=true\n";
        write(m_root.filePath(QStringLiteral("system/autostart/fixture.desktop")), m_systemEntry);
    }
    void startupOverridePreservesSourceAndActions()
    {
        StartupPage page;
        auto *view = page.findChild<QTreeView *>();
        QVERIFY(view);
        QCOMPARE(view->model()->rowCount(), 1);
        view->setCurrentIndex(view->model()->index(0, 0));
        auto *enable = button(page, QStringLiteral("Enable"));
        auto *disable = button(page, QStringLiteral("Disable"));
        QVERIFY(enable && disable);
        enable->click();
        const QString target = m_root.filePath(QStringLiteral("user/autostart/fixture.desktop"));
        const QByteArray enabled = read(target);
        QVERIFY(enabled.contains("Hidden=false"));
        QVERIFY(enabled.contains("X-GNOME-Autostart-enabled=true"));
        QVERIFY(enabled.endsWith("[Desktop Action Extra]\nName=Extra\nExec=true\nHidden=true\n"));
        QCOMPARE(read(m_root.filePath(QStringLiteral("system/autostart/fixture.desktop"))), m_systemEntry);
        view->setCurrentIndex(view->model()->index(0, 0));
        disable->click();
        QSettings saved(target, QSettings::IniFormat);
        QVERIFY(saved.value(QStringLiteral("Desktop Entry/Hidden")).toBool());
        QCOMPARE(read(m_root.filePath(QStringLiteral("system/autostart/fixture.desktop"))), m_systemEntry);
    }
    void servicesStayResponsiveAndKeepUserScope()
    {
        const QString bin = m_root.filePath(QStringLiteral("bin"));
        const QString command = bin + QStringLiteral("/systemctl");
        const QString log = m_root.filePath(QStringLiteral("systemctl-args"));
        write(command, "#!/bin/sh\ncase \"$*\" in *list-units*) /bin/sleep 1; printf 'fixture.service loaded active running Fixture\\n';; *) printf '%s\\n' \"$*\" > \"$KARYO_QA_SYSTEMCTL_LOG\";; esac\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", bin.toUtf8() + ":" + oldPath);
        qputenv("KARYO_QA_SYSTEMCTL_LOG", log.toUtf8());
        ServicesPage page;
        bool tick = false;
        QTimer::singleShot(10, &page, [&] { tick = true; });
        QElapsedTimer elapsed;
        elapsed.start();
        page.show();
        QTRY_VERIFY_WITH_TIMEOUT(tick, 300);
        QVERIFY(elapsed.elapsed() < 500);
        auto *view = page.findChild<QTreeView *>();
        QVERIFY(view);
        QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 2, 5000);
        for (int row = 0; row < 2; ++row) {
            if (view->model()->index(row, 1).data().toString() == QStringLiteral("User"))
                view->setCurrentIndex(view->model()->index(row, 0));
        }
        auto *start = button(page, QStringLiteral("Start"));
        QVERIFY(start);
        start->click();
        QTRY_VERIFY(QFile::exists(log));
        QCOMPARE(read(log).trimmed(), QByteArray("--user start -- fixture.service"));
        QTRY_VERIFY_WITH_TIMEOUT(page.findChildren<QProcess *>().isEmpty(), 5000);
        qputenv("PATH", oldPath);
    }
    void controlsOnlyTheSampledChild()
    {
        ProcessInfo invalid;
        QString invalidError;
        QVERIFY(!ProcessActions::signal(invalid, SIGTERM, &invalidError));
        invalid.pid = -1;
        QVERIFY(!ProcessActions::signal(invalid, SIGTERM, &invalidError));
        QProcess child;
        child.start(QStringLiteral("/bin/sleep"), {QStringLiteral("30")});
        QVERIFY(child.waitForStarted());
        Sampler sampler;
        sampler.setInterval(0);
        const auto process = sampler.processes().value(pid_t(child.processId()));
        QVERIFY(process.pid > 1);
        auto stale = process;
        ++stale.startTicks;
        QString error;
        QVERIFY(!ProcessActions::signal(stale, SIGTERM, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!child.waitForFinished(50));
        QVERIFY(ProcessActions::efficiency(process, &error));
        QCOMPARE(getpriority(PRIO_PROCESS, id_t(process.pid)), 19);
        QVERIFY(ProcessActions::signal(process, SIGTERM, &error));
        QVERIFY(child.waitForFinished(3000));
        QVERIFY(!ProcessActions::signal(process, SIGTERM, &error));
        ProcessesPage processes(&sampler);
        PerformancePage performance(&sampler);
        QVERIFY(processes.findChild<QTreeView *>());
    }
};
QTEST_MAIN(PagesTest)
#include "pagestest.moc"
