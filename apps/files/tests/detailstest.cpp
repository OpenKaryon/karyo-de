// SPDX-License-Identifier: GPL-3.0-or-later
#include "detailspane.h"
#include <QFile>
#include <QLabel>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class DetailsTest : public QObject {
    Q_OBJECT
    static QString value(DetailsPane &pane, const QString &key) {
        for (auto *label : pane.findChildren<QLabel *>()) {
            if (label->text() != key)
                continue;
            const auto siblings = label->parentWidget()->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
            for (auto *sibling : siblings)
                if (sibling != label)
                    return sibling->text();
        }
        return {};
    }
    static void put(const QString &path, int bytes) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArray(bytes, 'x')), bytes);
    }
private Q_SLOTS:
    void hierarchicalCacheRefreshAndSymlinks() {
        QTemporaryDir root;
        const QString folder = root.filePath("project"), child = folder + "/nested";
        QVERIFY(QDir().mkpath(child));
        put(folder + "/root.txt", 12); put(child + "/child.txt", 40);
        QVERIFY(QFile::link(folder, child + "/loop"));
        DetailsPane pane; pane.show();
        pane.showInfos({QFileInfo(folder)}, root.path());
        QTRY_COMPARE(value(pane, "Contains"), QString("Files: 2 · Folders: 1"));
        QCOMPARE(value(pane, "Size"), QString("52 B"));
        // A completed ancestor scan caches each child, avoiding a second tree walk.
        put(child + "/later.txt", 100);
        pane.showInfos({QFileInfo(child)}, folder);
        QCOMPARE(value(pane, "Contains"), QString("Files: 1 · Folders: 0"));
        QCOMPARE(value(pane, "Size"), QString("40 B"));
        pane.clearFolderSizes(); pane.showInfos({QFileInfo(child)}, folder);
        QTRY_COMPARE(value(pane, "Contains"), QString("Files: 2 · Folders: 0"));
        QCOMPARE(value(pane, "Size"), QString("140 B"));
    }
    void selectionChangeAndCloseDuringScan() {
        QTemporaryDir root; const QString slow = root.filePath("slow"), other = root.filePath("other");
        QVERIFY(QDir().mkpath(slow)); QVERIFY(QDir().mkpath(other));
        for (int i = 0; i < 400; ++i) put(slow + "/f" + QString::number(i), 1);
        put(other + "/small", 3);
        auto *pane = new DetailsPane; pane->show();
        pane->showInfos({QFileInfo(slow)}, root.path());
        pane->showInfos({QFileInfo(other)}, root.path());
        QTRY_COMPARE(value(*pane, "Size"), QString("3 B"));
        QCOMPARE(value(*pane, "Contains"), QString("Files: 1 · Folders: 0"));
        pane->clearFolderSizes(); pane->showInfos({QFileInfo(slow)}, root.path());
        delete pane; QTest::qWait(30); // Worker completion must not access the deleted pane.
    }
};
QTEST_MAIN(DetailsTest)
#include "detailstest.moc"
