// SPDX-License-Identifier: GPL-3.0-or-later
#include "pages.h"
#include "softwarejob.h"
#include "widgets.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

class FakeHostname : public QDBusVirtualObject
{
    Q_OBJECT
public:
    QString name = "qa-before";
    bool denied = false, interactive = false;
    QStringList changes;
    QString introspect(const QString &) const override { return { }; }
    bool handleMessage(const QDBusMessage &call, const QDBusConnection &bus) override
    {
        if (call.member() == "Get")
        {
            bus.send(call.createReply({ QVariant::fromValue(QDBusVariant(name)) }));
            return true;
        }
        if (call.member() == "SetHostname" || call.member() == "SetStaticHostname")
        {
            changes.append(call.member());
            const bool reject = denied;
            interactive = call.isInteractiveAuthorizationAllowed() && call.arguments().last().toBool();
            QTimer::singleShot(80, this,
                [this, call, bus, reject]
                {
                    if (reject)
                        bus.send(call.createErrorReply(
                            "org.freedesktop.DBus.Error.AccessDenied", "Hostname denied by QA"));
                    else
                    {
                        name = call.arguments().first().toString();
                        bus.send(call.createReply());
                    }
                });
            return true;
        }
        return false;
    }
};
class AboutTest : public QObject
{
    Q_OBJECT
    FakeHostname m_backend;
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_path = qgetenv("PATH");
    static void put(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), data.size());
    }
    void script(const QString &name, const QByteArray &data)
    {
        const auto path = m_root->filePath("bin/" + name);
        put(path, "#!/bin/sh\n" + data);
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }
    static bool hasText(QWidget *page, const QString &text)
    {
        for (auto *label : page->findChildren<QLabel *>())
            if (label->text().contains(text))
                return true;
        return false;
    }
    void nameDialog(const QString &name)
    {
        QTimer::singleShot(0, this,
            [name]
            {
                auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                if (dialog)
                {
                    dialog->setTextValue(name);
                    dialog->accept();
                }
            });
    }
private Q_SLOTS:
    void initTestCase()
    {
        auto bus
            = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS"), "qa-hostname");
        QVERIFY(bus.registerService("org.freedesktop.hostname1"));
        QVERIFY(bus.registerVirtualObject("/org/freedesktop/hostname1", &m_backend));
    }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        qputenv("QA_SOFTWARE", m_root->path().toUtf8());
        qputenv("PATH", m_root->filePath("bin").toUtf8() + ':' + m_path);
        script("apt-get", R"(sleep .1
if [ -f "$QA_SOFTWARE/deny-query" ]; then echo 'Simulation denied' >&2; exit 2; fi
printf 'Inst libfoo [1.0-1] (1.0-2 Debian-Security [amd64])\nInst new-app (2.0 Ubuntu [amd64])\n'
)");
        script("pkexec", R"(printf '%s\n' "$@" > "$QA_SOFTWARE/requests"
sleep .2
if [ -f "$QA_SOFTWARE/deny-update" ]; then echo 'Update denied by QA' >&2; exit 2; fi
printf done > "$QA_SOFTWARE/completed"
)");
        script("dpkg-query", "printf 0.1.0\n");
        m_backend.name = "qa-before";
        m_backend.denied = false;
        m_backend.interactive = false;
        m_backend.changes.clear();
    }
    void cleanup()
    {
        qputenv("PATH", m_path);
        qunsetenv("QA_SOFTWARE");
    }
    void readOnlyUpdateCountAndFailure()
    {
        QObject context;
        int count = -99;
        countUpdates(&context, [&](int n) { count = n; });
        QTRY_COMPARE(count, 2);
        QVERIFY(!QFileInfo::exists(m_root->filePath("requests")));
        put(m_root->filePath("deny-query"), "yes");
        countUpdates(&context, [&](int n) { count = n; });
        QTRY_COMPARE(count, -1);
    }
    void failedListRefreshIsVisibleAndRetryCompletes()
    {
        put(m_root->filePath("deny-update"), "yes");
        std::unique_ptr<SettingsPage> page(createUpdatesPage(nullptr));
        auto *check = page->findChild<QPushButton *>("refreshPackageLists");
        check->click();
        QVERIFY(!check->isEnabled());
        QTRY_VERIFY(hasText(page.get(), "Update denied by QA"));
        QTRY_VERIFY(check->isEnabled());
        QVERIFY(hasText(page.get(), "Could not update package lists"));
        QFile request(m_root->filePath("requests"));
        QVERIFY(request.open(QIODevice::ReadOnly));
        const auto args = request.readAll();
        QVERIFY(args.contains("/karyo/appcenter-apt\nupdate\n"));
        QVERIFY(QFile::remove(m_root->filePath("deny-update")));
        check->click();
        QTRY_VERIFY(check->isEnabled());
        QTRY_VERIFY(QFileInfo::exists(m_root->filePath("completed")));
        QTRY_VERIFY(hasText(page.get(), "libfoo"));
        QVERIFY(!hasText(page.get(), "Update denied by QA"));
    }
    void hostnameDenialStopsSequenceAndSuccessReadsBack()
    {
        std::unique_ptr<SettingsPage> page(createAboutPage(nullptr));
        auto *rename = page->findChild<QPushButton *>("renameComputer");
        m_backend.denied = true;
        nameDialog("qa-device");
        rename->click();
        QVERIFY(!rename->isEnabled());
        QTRY_VERIFY(rename->isEnabled());
        QCOMPARE(m_backend.changes, QStringList { "SetStaticHostname" });
        QCOMPARE(m_backend.name, QString("qa-before"));
        QVERIFY(hasText(page.get(), "Hostname denied by QA"));
        m_backend.denied = false;
        m_backend.changes.clear();
        nameDialog("qa-device");
        rename->click();
        QTRY_VERIFY(rename->isEnabled());
        QCOMPARE(m_backend.changes, QStringList({ "SetStaticHostname", "SetHostname" }));
        QVERIFY(m_backend.interactive);
        QCOMPARE(m_backend.name, QString("qa-device"));
        QVERIFY(hasText(page.get(), "Now: qa-device"));
        QVERIFY(!hasText(page.get(), "Hostname denied by QA"));
    }
};
QTEST_MAIN(AboutTest)
#include "abouttest.moc"
