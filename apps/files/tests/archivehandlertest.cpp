// SPDX-License-Identifier: GPL-3.0-or-later
#include "detailspane.h"
#include "fileswindow.h"
#include "folderview.h"
#include <karyo/ui/style.h>
#include <karyo/ui/desktopentry.h>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QScopeGuard>
#include <QMimeDatabase>
#include <QTemporaryDir>
#include <QStackedWidget>
#include <QTreeView>
#include <QtTest>

class ArchiveHandlerTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void detailsOpenOnNavigationAndItemClick()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString child = root.filePath(QStringLiteral("child"));
        QVERIFY(QDir().mkpath(child));

        FilesWindow window;
        window.openTab(root.path());
        window.show();
        auto *pane = window.findChild<DetailsPane *>();
        auto *folder = window.findChild<FolderView *>();
        auto *tree = folder->findChild<QTreeView *>();
        QVERIFY(pane->isVisible());

        pane->hide();
        folder->setPath(child);
        QVERIFY(pane->isVisible());
        folder->setMode(FolderView::Mode::Details);
        folder->setPath(root.path());
        folder->selectPath(child);
        QTRY_COMPARE(folder->selectedPaths(), QStringList{child});
        const auto row = tree->selectionModel()->selectedRows().first();
        const QPoint item = tree->visualRect(row).center();
        QVERIFY(tree->viewport()->rect().contains(item));
        pane->hide();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, item);
        QVERIFY(pane->isVisible());
    }

    void folderDetailsReuseSubtreeTotals()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString parent = root.filePath(QStringLiteral("parent"));
        const QString child = parent + QStringLiteral("/child");
        QVERIFY(QDir().mkpath(child));
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write(parent + QStringLiteral("/visible"), "abc"));
        QVERIFY(write(child + QStringLiteral("/.hidden"), "12345"));
        QVERIFY(QFile::link(parent, child + QStringLiteral("/cycle")));

        DetailsPane pane;
        const auto contains = [&pane](const QString &text) {
            for (const auto *label : pane.findChildren<QLabel *>()) {
                if (label->text() == text)
                    return true;
            }
            return false;
        };
        pane.show();
        pane.showInfos({}, parent);
        QVERIFY(!contains(QStringLiteral("Calculating…")));
        pane.showInfos({QFileInfo(parent)}, root.path());
        QTRY_VERIFY(contains(QStringLiteral("8 B")));
        QVERIFY(contains(QStringLiteral("Files: 2 · Folders: 1")));

        QVERIFY(write(child + QStringLiteral("/new"), "1234567"));
        pane.showInfos({}, parent);
        pane.showInfos({QFileInfo(child)}, parent);
        QVERIFY(contains(QStringLiteral("5 B")));
        QVERIFY(contains(QStringLiteral("Files: 1 · Folders: 0")));

        pane.clearFolderSizes();
        pane.showInfos({QFileInfo(child)}, parent);
        QTRY_VERIFY(contains(QStringLiteral("12 B")));
        QVERIFY(contains(QStringLiteral("Files: 2 · Folders: 0")));
    }


    void contextMenuLaunchesCreationWithLiteralSelection()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QByteArray oldPath = qgetenv("PATH");
        auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); qunsetenv("KARYO_CREATE_RECORD"); });
        const QString record = root.filePath("arguments");
        qputenv("KARYO_CREATE_RECORD", record.toUtf8());
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        const QString launcher = root.filePath("karyo-archive");
        QVERIFY(write(launcher, "#!/bin/sh\nprintf '%s\\0' \"$@\" > \"$KARYO_CREATE_RECORD\"\n"));
        QVERIFY(QFile::setPermissions(launcher, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("PATH", root.path().toUtf8() + ':' + oldPath);
        const QString first = root.filePath(QStringLiteral("имя с пробелом.txt"));
        const QString second = root.filePath(QStringLiteral("-name;literal\nfile.txt"));
        QVERIFY(write(first, "one")); QVERIFY(write(second, "two"));
        FilesWindow window; window.openTab(root.path()); window.show();
        auto *folder = window.findChildren<FolderView *>().last();
        folder->selectPaths({first, second}); QTRY_COMPARE(folder->selectedPaths().size(), 2);
        const QStringList selected = folder->selectedPaths();
        folder->contextMenuRequested(QPoint(10, 10), true);
        auto *action = window.findChild<QAction *>("addToArchive"); QVERIFY(action);
        action->trigger();
        for (auto *menu : window.findChildren<QMenu *>()) menu->close();
        QTRY_VERIFY(QFileInfo::exists(record)); QFile arguments(record); QVERIFY(arguments.open(QIODevice::ReadOnly));
        QByteArray expected("--create\0--\0", 12);
        for (const QString &path : selected) { expected += path.toUtf8(); expected += '\0'; }
        QCOMPARE(arguments.readAll(), expected);
    }
    void openArchiveUsesGuiAndHonorsRealUserHandler()
    {
        QTemporaryDir root;
        const QString data = root.filePath(QStringLiteral("data"));
        const QString config = root.filePath(QStringLiteral("config"));
        QVERIFY(QDir().mkpath(data + QStringLiteral("/applications")));
        QVERIFY(QDir().mkpath(config));
        qputenv("XDG_DATA_HOME", data.toUtf8());
        qputenv("XDG_DATA_DIRS", QByteArray("/usr/share"));
        qputenv("XDG_CONFIG_HOME", config.toUtf8());
        const QString record = root.filePath(QStringLiteral("launched"));
        qputenv("KARYO_ARCHIVE_RECORD", record.toUtf8());
        auto write = [](const QString &path, const QByteArray &text) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
        };
        const QString launcher = root.filePath(QStringLiteral("launcher"));
        QVERIFY(write(launcher, "#!/bin/sh\nprintf '%s\\n' \"$@\" >> \"$KARYO_ARCHIVE_RECORD\"\n[ \"$1\" != archive ] || sleep 0.8\n"));
        QVERIFY(QFile::setPermissions(launcher, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        auto entry = [&](const QString &id, const QString &kind, const QString &mime) {
            return write(data + QStringLiteral("/applications/") + id,
                QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=\"%2\" %1 %F\nMimeType=%3;\n")
                    .arg(kind, launcher, mime).toUtf8());
        };
        QVERIFY(entry(QStringLiteral("karyo-archive.desktop"), QStringLiteral("archive"), QStringLiteral("application/x-compressed-tar")));
        QVERIFY(entry(QStringLiteral("browser.desktop"), QStringLiteral("browser"), QStringLiteral("text/html")));
        const QString archive = root.filePath(QStringLiteral("sample archive.tar.gz"));
        QVERIFY(write(archive, "fixture"));
        const QString mime = QMimeDatabase().mimeTypeForFile(archive).name();
        QVERIFY(!mime.isEmpty());
        const QString defaults = config + QStringLiteral("/mimeapps.list");
        QVERIFY(write(defaults, QStringLiteral("[Default Applications]\n%1=browser.desktop;\n").arg(mime).toUtf8()));
        Karyo::Ui::applyStyle();
        FilesWindow window;
        window.openTab(root.path()); window.show();
        const auto folders = window.findChildren<FolderView *>();
        QVERIFY(!folders.isEmpty());
        folders.last()->fileActivated(archive);
        const auto *stack = window.findChild<QStackedWidget *>();
        QVERIFY(stack);
        QCOMPARE(stack->cursor().shape(), Qt::BusyCursor);
        folders.last()->fileActivated(archive);
        QTRY_VERIFY(stack->cursor().shape() != Qt::BusyCursor);
        QTRY_VERIFY(QFileInfo::exists(record));
        QFile result(record); QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QStringLiteral("archive\n%1\n").arg(archive).toUtf8());
        result.close(); QVERIFY(QFile::remove(record));
        QVERIFY(entry(QStringLiteral("preferred.desktop"), QStringLiteral("preferred"), mime));
        QVERIFY(write(defaults, QStringLiteral("[Default Applications]\n%1=preferred.desktop;\n").arg(mime).toUtf8()));
        const auto preferred = DesktopEntry::defaultFor(mime);
        QVERIFY(preferred);
        QCOMPARE(preferred->id, QStringLiteral("preferred.desktop"));
        QCOMPARE(preferred->mimeTypes.join(QLatin1Char(';')), mime);
        folders.last()->fileActivated(archive);
        QTRY_VERIFY(QFileInfo::exists(record));
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QStringLiteral("preferred\n%1\n").arg(archive).toUtf8());
        QVERIFY(!window.isMinimized());
    }
};
QTEST_MAIN(ArchiveHandlerTest)
#include "archivehandlertest.moc"
