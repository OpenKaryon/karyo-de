// SPDX-License-Identifier: GPL-3.0-or-later
#include "filesservice.h"
#include "fileswindow.h"
#include "folderview.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

class RecordingFilesService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.openkaryon.Files")
public:
    QStringList received;
    QString token;
public Q_SLOTS:
    Q_SCRIPTABLE void Open(const QStringList &paths, bool, bool, const QString &activationToken)
    {
        received = paths;
        token = activationToken;
    }
};

class FileServiceTest : public QObject
{
    Q_OBJECT
    FilesService m_files;
    FileManagerService m_standard{&m_files};
    QTemporaryDir m_config;

    QList<FilesWindow *> windows() const
    {
        QList<FilesWindow *> result;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *window = qobject_cast<FilesWindow *>(widget))
                result.append(window);
        return result;
    }
    FolderView *current(FilesWindow *window) const
    {
        return qobject_cast<FolderView *>(window->findChild<QStackedWidget *>()->currentWidget());
    }
    static QString uri(const QString &path) { return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded); }
    static bool touch(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7;
    }
    void call(const QString &method, const QStringList &uris)
    {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
            QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1"), method);
        message << uris << QString();
        QDBusPendingCallWatcher watcher(QDBusConnection::sessionBus().asyncCall(message));
        QTRY_VERIFY(watcher.isFinished());
        QCOMPARE(watcher.reply().type(), QDBusMessage::ReplyMessage);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QApplication::setQuitOnLastWindowClosed(false);
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        auto bus = QDBusConnection::sessionBus();
        QVERIFY2(bus.isConnected(), "Run this test inside dbus-run-session.");
        QVERIFY(bus.registerObject(QStringLiteral("/org/openkaryon/Files"), &m_files, QDBusConnection::ExportScriptableSlots));
        QVERIFY(bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), &m_standard, QDBusConnection::ExportScriptableSlots));
        QVERIFY(bus.registerService(QStringLiteral("org.openkaryon.Files")));
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.FileManager1")));
    }
    void cleanup()
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/org/openkaryon/Files"));
        QVERIFY(bus.registerObject(QStringLiteral("/org/openkaryon/Files"), &m_files, QDBusConnection::ExportScriptableSlots));
        for (auto *window : windows())
            window->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(windows().size(), 0);
    }
    void revealReusesBackgroundTabAndSelectsEncodedName()
    {
        QTemporaryDir root;
        const QString other = root.filePath(QStringLiteral("other"));
        QVERIFY(QDir().mkpath(other));
        const QString file = root.filePath(QStringLiteral("Отчёт #1.csv"));
        QVERIFY(touch(file));
        m_files.Open({root.path()}, false, false, {});
        auto *window = windows().first();
        window->openTab(other);
        auto *stack = window->findChild<QStackedWidget *>();
        QCOMPARE(stack->count(), 2);
        QCOMPARE(current(window)->path(), other);
        call(QStringLiteral("ShowItems"), {uri(file)});
        QCOMPARE(windows().size(), 1);
        QCOMPARE(stack->count(), 2);
        QCOMPARE(current(window)->path(), root.path());
        QTRY_COMPARE(current(window)->selectedPaths(), QStringList{file});
        call(QStringLiteral("ShowItems"), {uri(file)});
        QCOMPARE(windows().size(), 1);
        QCOMPARE(stack->count(), 2);
    }
    void itemsShareSelectionAndClearAnOldFilter()
    {
        QTemporaryDir root;
        const QString first = root.filePath(QStringLiteral("visible.txt"));
        const QString second = root.filePath(QStringLiteral(".hidden.txt"));
        QVERIFY(touch(first) && touch(second));
        m_files.Open({root.path()}, false, false, {});
        auto *view = current(windows().first());
        view->setShowHidden(false);
        view->setFilter(QStringLiteral("no-match"));
        call(QStringLiteral("ShowItems"), {uri(first), uri(second)});
        QTRY_COMPARE(view->selectedPaths().size(), 2);
        QVERIFY(view->selectedPaths().contains(first));
        QVERIFY(view->selectedPaths().contains(second));
        QVERIFY(view->showHidden());
        QCOMPARE(windows().size(), 1);
        view->setMode(FolderView::Mode::Icons);
        QTRY_COMPARE(view->selectedPaths().size(), 2);
    }
    void directoryItemsRevealParentButShowFoldersOpenDirectory()
    {
        QTemporaryDir root;
        const QString child = root.filePath(QStringLiteral("child"));
        QVERIFY(QDir().mkpath(child));
        call(QStringLiteral("ShowItems"), {uri(child)});
        auto *parentWindow = windows().first();
        QCOMPARE(current(parentWindow)->path(), root.path());
        QTRY_COMPARE(current(parentWindow)->selectedPaths(), QStringList{child});
        call(QStringLiteral("ShowFolders"), {uri(child)});
        QCOMPARE(windows().size(), 2);
        bool found = false;
        for (auto *window : windows())
            found |= current(window)->path() == child;
        QVERIFY(found);
    }
    void localHostUrisWorkAndRemoteUrisDoNotOpenWindows()
    {
        QTemporaryDir root;
        const QString file = root.filePath(QStringLiteral("a b.txt"));
        QVERIFY(touch(file));
        call(QStringLiteral("ShowItems"), {QStringLiteral("https://example.com/file"), QStringLiteral("file://remote/tmp/file")});
        QVERIFY(windows().isEmpty());
        QUrl local = QUrl::fromLocalFile(file);
        local.setHost(QStringLiteral("localhost"));
        call(QStringLiteral("ShowItemProperties"), {local.toString(QUrl::FullyEncoded)});
        QCOMPARE(windows().size(), 1);
        QTRY_COMPARE(current(windows().first())->selectedPaths(), QStringList{file});
    }
    void repeatedCliStartForwardsWithoutCreatingAnotherWindow()
    {
        QTemporaryDir root;
        const QString file = root.filePath(QStringLiteral("selected file.txt"));
        QVERIFY(touch(file));
        m_files.Open({root.path()}, false, false, {});
        auto *window = windows().first();
        QProcess client;
        client.start(QCoreApplication::applicationDirPath() + QStringLiteral("/karyo-files"),
                     {QStringLiteral("--select"), file});
        QVERIFY(client.waitForStarted());
        QTRY_COMPARE(client.state(), QProcess::NotRunning);
        QCOMPARE(client.exitCode(), 0);
        QCOMPARE(windows().size(), 1);
        QTRY_COMPARE(current(window)->selectedPaths(), QStringList{file});
    }
    void cliForwardsTheCallersActivationToken()
    {
        QTemporaryDir root;
        const QString file = root.filePath(QStringLiteral("token target.txt"));
        QVERIFY(touch(file));
        RecordingFilesService recording;
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/org/openkaryon/Files"));
        QVERIFY(bus.registerObject(QStringLiteral("/org/openkaryon/Files"), &recording, QDBusConnection::ExportScriptableSlots));
        QProcess client;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), QStringLiteral("test-user-activation-token"));
        client.setProcessEnvironment(environment);
        client.start(QCoreApplication::applicationDirPath() + QStringLiteral("/karyo-files"),
                     {QStringLiteral("--select"), file});
        QVERIFY(client.waitForStarted());
        QTRY_COMPARE(client.state(), QProcess::NotRunning);
        QCOMPARE(client.exitCode(), 0);
        QCOMPARE(recording.received, QStringList{file});
        QCOMPARE(recording.token, QStringLiteral("test-user-activation-token"));
        QVERIFY(windows().isEmpty());
    }

    void explicitNewWindowKeepsTheExistingWindow()
    {
        QTemporaryDir root;
        m_files.Open({root.path()}, false, false, {});
        auto *first = windows().first();
        m_files.Open({root.path()}, false, true, {});
        QCOMPARE(windows().size(), 2);
        QVERIFY(windows().contains(first));
    }
    void serverDoesNotReplaceAnExistingFileManager()
    {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.unregisterService(QStringLiteral("org.openkaryon.Files")));
        QProcess server;
        server.start(QCoreApplication::applicationDirPath() + QStringLiteral("/karyo-files"), {"--dbus-service"});
        QVERIFY(server.waitForStarted());
        QTRY_COMPARE(server.state(), QProcess::NotRunning);
        QCOMPARE(server.exitCode(), 1);
        QDBusInterface existing("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                "org.freedesktop.DBus.Introspectable", bus);
        QVERIFY(existing.isValid());
        QVERIFY(bus.registerService(QStringLiteral("org.openkaryon.Files")));
    }

    void revealActivatesTheExistingWindow()
    {
        if (!qEnvironmentVariableIsSet("KARYO_TEST_HEADLESS")
            || !qEnvironmentVariable("XDG_RUNTIME_DIR").startsWith(QLatin1String("/tmp/karyo-files-wayland.")))
            QSKIP("Focus requires the isolated headless test compositor.");
        QTemporaryDir root;
        const QString other = root.filePath(QStringLiteral("other"));
        const QString file = root.filePath(QStringLiteral("focus.txt"));
        QVERIFY(QDir().mkpath(other) && touch(file));
        m_files.Open({root.path()}, false, false, {});
        auto *first = windows().first();
        QTRY_VERIFY(first->isActiveWindow());
        // Give the isolated client a genuine Wayland input serial. Synthetic
        // Qt mouse events cannot authorize compositor activation requests.
        QCOMPARE(QProcess::execute(QStringLiteral("wlrctl"),
                 {QStringLiteral("pointer"), QStringLiteral("click")}), 0);
        QTest::qWait(50);
        m_files.Open({other}, false, false, {});
        QTRY_VERIFY(!first->isActiveWindow());
        call(QStringLiteral("ShowItems"), {uri(file)});
        QTRY_VERIFY(first->isActiveWindow());
        QCOMPARE(windows().size(), 2);
    }
};

QTEST_MAIN(FileServiceTest)
#include "fileservicetest.moc"
