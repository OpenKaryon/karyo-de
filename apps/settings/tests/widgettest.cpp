// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QSignalSpy>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>

class SettingsWidgetsTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void controlsAndKeywords() {
        SettingsPage page("Display");
        auto *row = new SettingRow("monitor", "Resolution", "Choose an output", &page);
        auto *toggle = row->addToggle(false); page.column()->addWidget(row);
        row->setLink("1920 × 1080"); page.addKeywords({"screen"}); page.resize(700, 320); page.show();
        QSignalSpy changed(toggle, &QAbstractButton::toggled);
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(toggle->isChecked()); QCOMPARE(changed.size(), 1);
        QVERIFY(page.keywords().contains("Resolution")); QVERIFY(page.keywords().contains("screen"));
        auto *segments = new Segmented({"First", "Second"}); page.column()->addWidget(segments);
        segments->setCurrentIndex(1); QCOMPARE(segments->currentIndex(), 1);
        QSignalSpy clicked(row, &SettingRow::clicked);
        QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier, QPoint(25, 25));
        QCOMPARE(clicked.size(), 1);
    }

};
QTEST_MAIN(SettingsWidgetsTest)
#include "widgettest.moc"
