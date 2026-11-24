// SPDX-License-Identifier: GPL-3.0-or-later
#include "folderview.h"
#include <karyo/ui/style.h>
#include <QAbstractItemView>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>

class ContextMenuTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { Karyo::Ui::applyStyle(); }

    void keyboardMenu_data()
    {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("modifiers");
        QTest::addColumn<bool>("selected");
        for (bool icons : {false, true}) {
            for (bool shift : {false, true}) {
                for (bool selected : {false, true}) {
                    const QByteArray row = QByteArray(icons ? "icons-" : "details-")
                        + (shift ? "shift-f10-" : "native-context-")
                        + (selected ? "selected" : "background");
                    QTest::newRow(row.constData()) << icons
                        << int(shift ? Qt::Key_F10 : Qt::Key_Menu)
                        << int(shift ? Qt::ShiftModifier : Qt::NoModifier) << selected;
                }
            }
        }
    }

    void keyboardMenu()
    {
        QFETCH(bool, icons);
        QFETCH(int, key);
        QFETCH(int, modifiers);
        QFETCH(bool, selected);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("ordinary-note.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Karyo keyboard context menu fixture\n");
        file.close();
        FolderView folder;
        folder.resize(700, 400);
        folder.setMode(icons ? FolderView::Mode::Icons : FolderView::Mode::Details);
        folder.setPath(directory.path());
        folder.show();
        folder.activateWindow();
        QTRY_COMPARE(folder.itemCount(), 1);
        folder.selectPath(path);
        QTRY_COMPARE(folder.selectedPaths(), QStringList{path});
        auto *view = icons ? static_cast<QAbstractItemView *>(folder.findChild<QListView *>())
                           : folder.findChild<QTreeView *>();
        QVERIFY(view);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        if (!selected)
            view->clearSelection();
        QSignalSpy requested(&folder, &FolderView::contextMenuRequested);
        if (key == Qt::Key_Menu) {
            // QPA delivers this separately from key events on Wayland.
            const QPoint origin(0, 0);
            QContextMenuEvent event(QContextMenuEvent::Keyboard, origin,
                                    view->viewport()->mapToGlobal(origin));
            QApplication::sendEvent(view->viewport(), &event);
        } else {
            QTest::keyClick(view, Qt::Key(key), Qt::KeyboardModifiers(modifiers));
        }
        QTRY_COMPARE_WITH_TIMEOUT(requested.count(), 1, 600);
        const QList<QVariant> args = requested.takeFirst();
        QCOMPARE(args.at(1).toBool(), selected);
        const QPoint global = args.at(0).toPoint();
        const QPoint local = view->viewport()->mapFromGlobal(global);
        QVERIFY(view->viewport()->rect().contains(local));
        if (selected) {
            QVERIFY(view->visualRect(view->currentIndex()).contains(local));
            QCOMPARE(folder.selectedPaths(), QStringList{path});
        } else {
            QVERIFY(folder.selectedPaths().isEmpty());
        }
    }

    void nativeEventAfterShortcutKeepsOneMenu()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("ordinary-note.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Karyo native context fixture\n");
        file.close();
        FolderView folder;
        folder.resize(700, 400);
        folder.setPath(directory.path());
        folder.show();
        folder.activateWindow();
        QTRY_COMPARE(folder.itemCount(), 1);
        folder.selectPath(path);
        QTRY_COMPARE(folder.selectedPaths(), QStringList{path});
        auto *view = folder.findChild<QTreeView *>();
        QVERIFY(view);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QMenu menu(&folder);
        menu.addAction(QStringLiteral("Open"));
        QSignalSpy requested(&folder, &FolderView::contextMenuRequested);
        connect(&folder, &FolderView::contextMenuRequested, &menu,
                [&menu](const QPoint &position, bool) { menu.popup(position); });
        QTest::keyClick(view, Qt::Key_F10, Qt::ShiftModifier);
        QTRY_COMPARE(requested.count(), 1);
        QTRY_VERIFY(menu.isVisible());
        QContextMenuEvent native(QContextMenuEvent::Keyboard, QPoint(0, 0),
                                 view->viewport()->mapToGlobal(QPoint(0, 0)));
        QApplication::sendEvent(view->viewport(), &native);
        QCOMPARE(requested.count(), 1);
        menu.close();
    }

    void inlineRenameKeepsItsKeyboardFocus_data()
    {
        QTest::addColumn<bool>("icons");
        QTest::newRow("details") << false;
        QTest::newRow("icons") << true;
    }

    void inlineRenameKeepsItsKeyboardFocus()
    {
        QFETCH(bool, icons);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("ordinary-note.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Karyo rename context fixture\n");
        file.close();
        FolderView folder;
        folder.resize(700, 400);
        folder.setMode(icons ? FolderView::Mode::Icons : FolderView::Mode::Details);
        folder.setPath(directory.path());
        folder.show();
        folder.activateWindow();
        QTRY_COMPARE(folder.itemCount(), 1);
        folder.selectPath(path);
        folder.renameSelected();
        auto *editor = folder.findChild<QLineEdit *>(QStringLiteral("fileRenameEditor"));
        QTRY_VERIFY(editor && editor->isVisible());
        QTRY_VERIFY(editor->hasFocus());
        QSignalSpy requested(&folder, &FolderView::contextMenuRequested);
        QTest::keyClick(editor, Qt::Key_F10, Qt::ShiftModifier);
        QTest::keyClick(editor, Qt::Key_Menu);
        QCOMPARE(requested.count(), 0);
        QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(QFileInfo::exists(path));
    }
};
QTEST_MAIN(ContextMenuTest)
#include "contextmenutest.moc"
