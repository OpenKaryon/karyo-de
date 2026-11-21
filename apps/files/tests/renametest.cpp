// SPDX-License-Identifier: GPL-3.0-or-later
#include "folderview.h"
#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>
#include <QAbstractItemView>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QtTest>

class RenameTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { Karyo::Ui::applyStyle(); }

    void rename_data()
    {
        QTest::addColumn<bool>("icons");
        QTest::newRow("details") << false;
        QTest::newRow("icons") << true;
    }

    void rename()
    {
        QFETCH(bool, icons);
        QTemporaryDir directory;
        const QString original = directory.filePath(QStringLiteral("report.tar.gz"));
        QFile file(original); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("safe data"); file.close();
        FolderView view;
        view.resize(900, 450);
        if (icons) view.setMode(FolderView::Mode::Icons);
        view.show(); view.setPath(directory.path());
        QTRY_COMPARE(view.itemCount(), 1);
        view.selectPath(original);
        view.renameSelected();
        auto *editor = view.findChild<QLineEdit *>(QStringLiteral("fileRenameEditor"));
        QTRY_VERIFY(editor && editor->isVisible());
        QCOMPARE(editor->text(), QStringLiteral("report.tar.gz"));
        QTRY_COMPARE(editor->selectedText(), QStringLiteral("report.tar"));
        const QImage editorPixels = editor->grab().toImage();
        QCOMPARE(editorPixels.pixelColor(editorPixels.width() - 4, editorPixels.height() - 4), Karyo::Ui::Theme::current().win);
        QVERIFY(editor->geometry().height() >= 24);
        QVERIFY(editor->geometry().width() >= 70);
        if (!icons) {
            QVERIFY(editor->geometry().left() >= 34); // beside the icon
            QVERIFY(editor->geometry().height() <= 30); // inside its own row
        }
        if (!qEnvironmentVariableIsEmpty("KARYO_FILES_QA_DIR"))
            view.grab().save(QDir(qEnvironmentVariable("KARYO_FILES_QA_DIR")).filePath(icons ? "rename-icons.png" : "rename-details.png"));
        QTest::keyClicks(editor, "renamed");
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("renamed.gz"))));
        QVERIFY(!QFileInfo::exists(original));
        QFile contents(directory.filePath(QStringLiteral("renamed.gz")));
        QVERIFY(contents.open(QIODevice::ReadOnly)); QCOMPARE(contents.readAll(), QByteArray("safe data"));
        view.selectPath(contents.fileName()); view.renameSelected();
        editor = view.findChild<QLineEdit *>(QStringLiteral("fileRenameEditor"));
        QTRY_VERIFY(editor && editor->isVisible());
        QTest::keyClicks(editor, "cancelled"); QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(QFileInfo::exists(contents.fileName()));
        QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("cancelled.gz"))));
    }

    void folderAndDotfileSelection()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("project.v2")));
        QFile hidden(directory.filePath(QStringLiteral(".profile"))); QVERIFY(hidden.open(QIODevice::WriteOnly)); hidden.close();
        FolderView view; view.resize(900,450); view.show(); view.setShowHidden(true); view.setPath(directory.path());
        QTRY_COMPARE(view.itemCount(), 2);
        for (const QString &name : {QStringLiteral("project.v2"), QStringLiteral(".profile")}) {
            view.selectPath(directory.filePath(name)); view.renameSelected();
            auto *editor = view.findChild<QLineEdit *>(QStringLiteral("fileRenameEditor"));
            QTRY_VERIFY(editor && editor->isVisible());
            QCOMPARE(editor->selectedText(), name);
            QTest::keyClick(editor, Qt::Key_Escape);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
};
QTEST_MAIN(RenameTest)
#include "renametest.moc"
