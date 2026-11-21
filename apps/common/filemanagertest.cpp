// SPDX-License-Identifier: GPL-3.0-or-later
#include "filemanager.h"
#include <QDBusConnection>
#include <QDesktopServices>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class Manager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
    QStringList selected;
    QStringList folders;
    QString startup;
public Q_SLOTS:
    Q_SCRIPTABLE void ShowItems(const QStringList &uris, const QString &id)
    { selected = uris; startup = id; }
    Q_SCRIPTABLE void ShowFolders(const QStringList &uris, const QString &id)
    { folders = uris; startup = id; }
};
class FileManagerTest : public QObject
{
    Q_OBJECT
    QUrl m_opened;
public Q_SLOTS:
    void openFolder(const QUrl &url) { m_opened = url; }
private Q_SLOTS:
    void opensTheRequestedFolderThenFallsBack()
    {
        auto bus = QDBusConnection::sessionBus(); Manager manager; QTemporaryDir root;
        QVERIFY(bus.registerService("org.freedesktop.FileManager1"));
        QVERIFY(bus.registerObject("/org/freedesktop/FileManager1", &manager, QDBusConnection::ExportScriptableSlots));
        QDesktopServices::setUrlHandler("file", this, "openFolder");
        Karyo::Apps::openFolder(root.path(), this);
        QTRY_COMPARE(manager.folders, QStringList{QUrl::fromLocalFile(root.path()).toString()});
        QVERIFY(manager.selected.isEmpty()); QVERIFY(m_opened.isEmpty());
        bus.unregisterObject("/org/freedesktop/FileManager1");
        QVERIFY(bus.unregisterService("org.freedesktop.FileManager1"));
        Karyo::Apps::openFolder(root.path(), this);
        QTRY_COMPARE(m_opened, QUrl::fromLocalFile(root.path()));
        m_opened.clear(); QDesktopServices::unsetUrlHandler("file");
    }
    void selectsThenFallsBack()
    {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QString::fromUtf8("Отчёт #1.pdf"));
        Manager manager;
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.FileManager1")));
        QVERIFY(bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), &manager,
                                   QDBusConnection::ExportScriptableSlots));
        QDesktopServices::setUrlHandler(QStringLiteral("file"), this, "openFolder");
        Karyo::Apps::showInFiles(path, this);
        QTRY_COMPARE(manager.selected, QStringList{QUrl::fromLocalFile(path).toString()});
        QCOMPARE(manager.startup, QString());
        QVERIFY(m_opened.isEmpty());
        bus.unregisterObject(QStringLiteral("/org/freedesktop/FileManager1"));
        QVERIFY(bus.unregisterService(QStringLiteral("org.freedesktop.FileManager1")));
        Karyo::Apps::showInFiles(path, this);
        QTRY_COMPARE(m_opened, QUrl::fromLocalFile(dir.path()));
        m_opened.clear();
        Karyo::Apps::showInFiles(QString(), this);
        QCoreApplication::processEvents();
        QVERIFY(m_opened.isEmpty());
        QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
    }
};
QTEST_MAIN(FileManagerTest)
#include "filemanagertest.moc"
