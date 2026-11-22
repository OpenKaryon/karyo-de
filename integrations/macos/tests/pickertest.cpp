// SPDX-License-Identifier: GPL-3.0-or-later
#include "pickerwindow.h"
#include <karyo/ui/style.h>
#include <QAbstractButton>
#include <QFile>
#include <QDir>
#include <qscopeguard.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class PickerTest final : public QObject
{
    Q_OBJECT
private:
    const QString helper = QStringLiteral(KARYO_MACOS_PICKER_FIXTURE);
    template<class T> T *child(MacOSPicker &window, const char *name) { return window.findChild<T *>(QString::fromLatin1(name)); }
    QString path(QTemporaryDir &dir, const QString &name) { return dir.path() + QLatin1Char('/') + name; }
    void select(MacOSPicker &window, const QString &value) { child<QLineEdit>(window, "applicationPath")->setText(value); }
    void launch(MacOSPicker &window) { QTest::mouseClick(child<QAbstractButton>(window, "launchApplication"), Qt::LeftButton); }
    QLabel *status(MacOSPicker &window) { return child<QLabel>(window, "launchStatus"); }
    QAbstractButton *button(MacOSPicker &window) { return child<QAbstractButton>(window, "launchApplication"); }

private slots:
    void initTestCase() { Karyo::Ui::applyStyle(); }
    void cleanup() { qunsetenv("KARYO_PICKER_TEST_STATUS"); }

    void runtimeCheckKeepsEventLoopResponsive()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        MacOSPicker window(helper); window.show(); select(window, path(dir, "ordinary"));
        int ticks = 0; QTimer timer; timer.setInterval(5); connect(&timer, &QTimer::timeout, this, [&] { ++ticks; }); timer.start();
        QVERIFY(!button(window)->isEnabled());
        QTRY_VERIFY(button(window)->isEnabled());
        QVERIFY(ticks > 3);
        QVERIFY(status(window)->text().contains("available"));
    }

    void missingRuntimeCanBeRefreshed()
    {
        qputenv("KARYO_PICKER_TEST_STATUS", "missing");
        QTemporaryDir dir; MacOSPicker window(helper); window.show(); select(window, path(dir, "ordinary"));
        QTRY_VERIFY(status(window)->text().contains("not installed"));
        QVERIFY(!button(window)->isEnabled());
        qputenv("KARYO_PICKER_TEST_STATUS", "ready");
        QTest::mouseClick(child<QAbstractButton>(window, "refreshRuntime"), Qt::LeftButton);
        QTRY_VERIFY(button(window)->isEnabled());
    }

    void malformedStatusAndExcessiveMetadataFailClosed_data()
    {
        QTest::addColumn<QByteArray>("mode");
        QTest::newRow("malformed") << QByteArray("bad");
        QTest::newRow("oversized") << QByteArray("large");
        QTest::newRow("timeout") << QByteArray("timeout");
    }
    void malformedStatusAndExcessiveMetadataFailClosed()
    {
        QFETCH(QByteArray, mode); qputenv("KARYO_PICKER_TEST_STATUS", mode);
        QTemporaryDir dir; MacOSPicker window(helper); window.show(); select(window, path(dir, "ordinary"));
        const QString expected = mode == "bad" ? QStringLiteral("runtime status")
            : mode == "large" ? QStringLiteral("too much metadata") : QStringLiteral("did not respond");
        QTRY_VERIFY_WITH_TIMEOUT(status(window)->text().contains(expected), 12000);
        QVERIFY(child<QAbstractButton>(window, "refreshRuntime")->isEnabled());
        QVERIFY(!button(window)->isEnabled());
    }

    void failedHelperDoesNotLeaveControlsStuck()
    {
        MacOSPicker window(QStringLiteral("/missing/karyo-macos-test-helper")); window.show();
        QTRY_VERIFY(status(window)->text().contains("Could not start"));
        QVERIFY(child<QAbstractButton>(window, "refreshRuntime")->isEnabled());
        QVERIFY(!button(window)->isEnabled());
    }

    void invalidInspectionDoesNotStartAnApplication_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("format-error") << QStringLiteral("invalid-file");
        QTest::newRow("bad-json") << QStringLiteral("invalid-json");
    }
    void invalidInspectionDoesNotStartAnApplication()
    {
        QFETCH(QString, name); QTemporaryDir dir; const QString file = path(dir, name);
        MacOSPicker window(helper); window.show(); select(window, file); QTRY_VERIFY(button(window)->isEnabled());
        launch(window); QTRY_VERIFY(status(window)->text().contains("Could not"));
        QVERIFY(!QFile::exists(file + ".arguments.json"));
        QCOMPARE(status(window)->textFormat(), Qt::PlainText);
        if (name == "invalid-file") QVERIFY(status(window)->text().contains("<b>bad input</b>"));
        QVERIFY(button(window)->isEnabled());
    }

    void runningControllerCannotCloseAndArgumentsRemainLiteral()
    {
        QTemporaryDir dir; const QString file = path(dir, QStringLiteral("hold-transient"));
        const QStringList arguments{QStringLiteral("spaces '$()'"), QStringLiteral("; touch ") + path(dir, "must-not-exist"), QStringLiteral("one\ntwo")};
        MacOSPicker window(helper, {}, arguments); window.show(); select(window, file); QTRY_VERIFY(button(window)->isEnabled());
        launch(window);
        QTRY_VERIFY(child<QPlainTextEdit>(window, "applicationOutput")->toPlainText().contains("fixture-running"));
        QVERIFY(!window.close()); QVERIFY(window.isVisible());
        QVERIFY(status(window)->text().contains("Close the macOS application"));
        QVERIFY(child<QLineEdit>(window, "applicationPath")->isReadOnly());
        QVERIFY(child<QLabel>(window, "storageNotice")->text().contains("Temporary profile"));
        QFile record(file + ".arguments.json"); QVERIFY(record.open(QIODevice::ReadOnly));
        const QJsonArray actual = QJsonDocument::fromJson(record.readAll()).array();
        QStringList expected{QStringLiteral("--"), file, QStringLiteral("--")}; expected.append(arguments);
        QCOMPARE(actual, QJsonArray::fromStringList(expected));
        QVERIFY(!QFile::exists(path(dir, "must-not-exist")));
        QFile stop(file + ".stop"); QVERIFY(stop.open(QIODevice::WriteOnly)); stop.close();
        QTRY_VERIFY(status(window)->text().contains("Application finished"));
        QTRY_VERIFY(button(window)->isEnabled());
        QVERIFY(child<QAbstractButton>(window, "openLogs")->isEnabled());
        QVERIFY(window.close());
    }

    void relativeLeadingDashPathRemainsAPositionalFile()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString previous = QDir::currentPath(); QVERIFY(QDir::setCurrent(dir.path()));
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        const QString file = QStringLiteral("-ordinary");
        MacOSPicker window(helper, file); window.show();
        QTRY_VERIFY(status(window)->text().contains("Application finished"));
        QFile record(file + ".arguments.json"); QVERIFY(record.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(record.readAll()).array(),
                 QJsonArray::fromStringList({QStringLiteral("--"), file, QStringLiteral("--")}));
    }

    void initialApplicationStartsAfterSuccessfulPreflight()
    {
        QTemporaryDir dir; const QString file = path(dir, "ordinary");
        MacOSPicker window(helper, file); window.show();
        QTRY_VERIFY(status(window)->text().contains("Application finished"));
        QVERIFY(QFile::exists(file + ".arguments.json"));
    }

    void applicationErrorIsInlineAndPlain()
    {
        QTemporaryDir dir; MacOSPicker window(helper, path(dir, "failure")); window.show();
        QTRY_VERIFY(status(window)->text().contains("code 23"));
        QVERIFY(child<QPlainTextEdit>(window, "applicationOutput")->toPlainText().contains("<b>fixture diagnostic</b>"));
        QCOMPARE(status(window)->textFormat(), Qt::PlainText);
        QVERIFY(button(window)->isEnabled());
    }

    void utf8ChunksAreDecodedTogether()
    {
        QTemporaryDir dir; MacOSPicker window(helper, path(dir, "unicode")); window.show();
        QTRY_VERIFY(status(window)->text().contains("Application finished"));
        QCOMPARE(child<QPlainTextEdit>(window, "applicationOutput")->toPlainText(), QString::fromUtf8("Пример"));
    }

    void outputWithoutNewlinesIsBounded()
    {
        QTemporaryDir dir; MacOSPicker window(helper, path(dir, "output-limit")); window.show();
        QTRY_VERIFY(status(window)->text().contains("Application finished"));
        QTRY_VERIFY(button(window)->isEnabled());
        const int characters = child<QPlainTextEdit>(window, "applicationOutput")->document()->characterCount();
        QVERIFY(characters <= 200000); QVERIFY(characters > 100000);
    }

    void closingBeforeStatusCompletesNeverLaunchesInitialFile()
    {
        QTemporaryDir dir; const QString file = path(dir, "ordinary");
        MacOSPicker window(helper, file); window.show();
        QTest::qWait(20); QVERIFY(window.close()); QTest::qWait(180);
        QVERIFY(!QFile::exists(file + ".arguments.json"));
    }
};
QTEST_MAIN(PickerTest)
#include "pickertest.moc"
