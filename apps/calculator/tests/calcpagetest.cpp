// SPDX-License-Identifier: GPL-3.0-or-later
#include "calcpage.h"
#include <QApplication>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QtTest>

class CalcPageTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void keyboardEvaluationAndSharedHistory()
    {
        History history;
        CalcPage standard(false, &history);
        standard.show();
        auto *entry = standard.findChild<QLineEdit *>();
        QVERIFY(entry);
        entry->setText("50+10%");
        QTest::keyClick(entry, Qt::Key_Return);
        QCOMPARE(history.entries().size(), 1);
        QCOMPARE(history.entries().first().result, QStringLiteral("55"));
        entry->setText("1/0");
        QTest::keyClick(entry, Qt::Key_Return);
        QCOMPARE(history.entries().size(), 1);
        CalcPage scientific(true, &history);
        scientific.show();
        auto *advanced = scientific.findChild<QLineEdit *>();
        QVERIFY(advanced);
        advanced->setText("2^3^2");
        QTest::keyClick(advanced, Qt::Key_Return);
        QCOMPARE(history.entries().size(), 2);
        QCOMPARE(history.entries().first().result, QStringLiteral("512"));
        history.clear();
        QVERIFY(history.entries().isEmpty());
    }
};
int main(int argc, char **argv)
{
    QTemporaryDir configuration;
    if (!configuration.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", configuration.path().toUtf8());
    QApplication app(argc, argv);
    CalcPageTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "calcpagetest.moc"
