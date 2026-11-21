// SPDX-License-Identifier: GPL-3.0-or-later
#include "editorwindow.h"
#include "buffer.h"
#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QMessageBox>
#include <QTimer>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

class WindowTest : public QObject
{
    Q_OBJECT
    void write(const QString &path, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), qint64(data.size()));
    }
    QString dataDir(const QTemporaryDir &root)
    {
        qputenv("XDG_DATA_HOME", root.path().toUtf8());
        return root.filePath("openkaryon/openkaryon-editor");
    }
private Q_SLOTS:
    void legacyUnsavedTabsSurvivePublicIdentity()
    {
        QTemporaryDir root;
        const QString data = dataDir(root);
        Buffer original("recovered");
        QTextCursor(original.document()).insertText("Unsaved before migration");
        QVERIFY(original.writeCache(data + "/buffers/recovered"));
        write(data + "/session.json", QJsonDocument(QJsonObject{
            {"version",1},{"clean",false},{"current",0},
            {"tabs",QJsonArray{QJsonObject{{"id","recovered"},{"path",""}}}}}).toJson());
        EditorWindow window;
        QVERIFY(window.restoreSession());
        const auto buffers = window.findChildren<Buffer *>();
        QCOMPARE(buffers.size(), 1);
        QCOMPARE(buffers.first()->text(), QStringLiteral("Unsaved before migration"));
        QVERIFY(buffers.first()->canUndo());
        QVERIFY(buffers.first()->isModified());
        window.close();
    }
    void corruptMetadataDoesNotPurgeRecovery()
    {
        QTemporaryDir root;
        const QString data = dataDir(root);
        const QString orphan = data + "/buffers/orphan/keep-text";
        write(orphan, "unreferenced recovery");
        write(data + "/session.json", "{broken");
        {
            EditorWindow window;
            QVERIFY(!window.restoreSession());
            QVERIFY(QFile::exists(orphan));
            QVERIFY(!QDir(data).entryList({"session.invalid-*.json"},QDir::Files).isEmpty());
            window.newUntitled();
            window.close();
        }
        EditorWindow reopened;
        QVERIFY(reopened.restoreSession());
        QVERIFY(QFile::exists(orphan));
        reopened.close();
    }
    void encodingChoiceCancelsOrPreservesUnicode()
    {
        QTemporaryDir root;
        dataDir(root);
        const QString path = root.filePath("latin1.txt");
        const QByteArray original("caf\xe9\n");
        write(path,original);
        EditorWindow window;
        window.openFiles({path});
        window.show();
        auto *buffer=window.findChild<Buffer *>();
        QVERIFY(buffer);
        QTextCursor cursor(buffer->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QString::fromUtf8("Привет"));
        QAction *save=nullptr;
        for (auto *action:window.findChildren<QAction *>()) {
            if (action->shortcut()==QKeySequence::Save) save=action;
        }
        QVERIFY(save);
        const auto choose=[&](QMessageBox::StandardButton choice) {
            bool seen=false;
            QTimer timer;
            timer.setSingleShot(true);
            connect(&timer,&QTimer::timeout,&window,[&] {
                auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!box) {
                    if (auto *modal=QApplication::activeModalWidget()) modal->close();
                    return;
                }
                auto *button=box->button(choice);
                if (!button) { box->reject(); return; }
                seen=true;
                button->click();
            });
            timer.start(0);
            save->trigger();
            timer.stop();
            return seen;
        };
        QVERIFY(choose(QMessageBox::Cancel));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(),original);
        file.close();
        QVERIFY(buffer->isModified());
        QVERIFY(choose(QMessageBox::Yes));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()),QString::fromUtf8("café\nПривет"));
        QCOMPARE(buffer->encoding(),QStringLiteral("UTF-8"));
        QVERIFY(!buffer->isModified());
        window.close();
    }
    void secondProcessUsesExistingWindow()
    {
        QTemporaryDir root;
        dataDir(root);
        const QString file = root.filePath("shared notes.txt");
        write(file,"shared document");
        EditorWindow window;
        window.openFiles({file});
        EditorService service(&window);
        auto bus=QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.openkaryon.Editor"));
        QVERIFY(bus.registerObject("/org/openkaryon/Editor",&service,QDBusConnection::ExportScriptableSlots));
        QProcess child;
        child.start(QString::fromUtf8(KARYO_EDITOR_EXECUTABLE),{file});
        QVERIFY(child.waitForStarted(2000));
        QTRY_COMPARE_WITH_TIMEOUT(child.state(),QProcess::NotRunning,5000);
        QCOMPARE(child.exitStatus(),QProcess::NormalExit);
        QCOMPARE(child.exitCode(),0);
        QCOMPARE(window.findChildren<Buffer *>().size(),1);
        bus.unregisterObject("/org/openkaryon/Editor");
        bus.unregisterService("org.openkaryon.Editor");
        window.close();
    }
};
int main(int argc,char **argv)
{
    QTemporaryDir config;
    if (!config.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME",config.path().toUtf8());
    QApplication app(argc,argv);
    app.setApplicationName("karyo-editor");
    app.setOrganizationName("openkaryon");
    WindowTest test;
    return QTest::qExec(&test,argc,argv);
}
#include "windowtest.moc"
