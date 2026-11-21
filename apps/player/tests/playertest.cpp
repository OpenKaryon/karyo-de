// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"
#include "playerwidgets.h"
#include "playerwindow.h"
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QDBusConnection>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QMediaPlayer>
#include <QLocale>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

class PlayerTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_home;
    QString wave(const QString &name)
    {
        const QString path = m_home.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        constexpr int rate = 16000, samples = rate * 2;
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4); out << quint32(36 + samples * 2);
        out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1);
        out << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
        out.writeRawData("data", 4); out << quint32(samples * 2);
        for (int n = 0; n < samples; ++n)
            out << qint16(10000 * std::sin(2 * 3.141592653589793 * 440 * n / rate));
        return path;
    }
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
    }
    void libraryFoldersAndLimit_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::newRow("C") << QStringLiteral("C");
        QTest::newRow("English") << QStringLiteral("en_US");
        QTest::newRow("Russian") << QStringLiteral("ru_RU");
    }
    void libraryFoldersAndLimit()
    {
        QFETCH(QString, locale);
        const QLocale previous;
        QLocale::setDefault(QLocale(locale));
        QDir root(m_home.path());
        QVERIFY(root.mkpath(QStringLiteral("Artist/Album")));
        for (const QString &name : {QStringLiteral("01 - First.wav"), QStringLiteral("10 Last.FLAC"),
                                    QStringLiteral("2 Middle.mp3"), QStringLiteral("notes.txt")}) {
            QFile file(root.filePath(QStringLiteral("Artist/Album/") + name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        auto items = Library::scan(root.path(), false);
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.first().title, QStringLiteral("First"));
        QCOMPARE(items.first().subtitle, QStringLiteral("Artist · Album"));
        QCOMPARE(items.at(1).title, QStringLiteral("Middle"));
        QCOMPARE(Library::scan(root.path(), false, 1).size(), 1);
        QVERIFY(Library::scan(root.path(), false, 0).isEmpty());
        QVERIFY(Library::scan(QString(), true).isEmpty());
        QVERIFY(Library::isVideo(QStringLiteral("film.MKV")));
        QVERIFY(!Library::isMedia(QStringLiteral("notes.txt")));
        QCOMPARE(Library::duration(3723000), QStringLiteral("1:02:03"));
        QLocale::setDefault(previous);
    }
    void seekDragIsNotOverwritten()
    {
        SeekBar bar;
        bar.resize(212, 20);
        bar.setRange(10000);
        bar.show();
        QSignalSpy moved(&bar, &SeekBar::moved);
        QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, QPoint(106, 10));
        QVERIFY(bar.isDragging());
        QCOMPARE(bar.value(), 5000);
        bar.setValue(1000);
        QCOMPARE(bar.value(), 5000);
        QTest::mouseMove(&bar, QPoint(206, 10));
        QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, QPoint(206, 10));
        QVERIFY(!bar.isDragging());
        QVERIFY(!moved.isEmpty());
        bar.setValue(12000);
        QCOMPARE(bar.value(), 10000);
    }
    void actualAudioDecode()
    {
        const QString path = wave(QStringLiteral("tone.wav"));
        QVERIFY(!path.isEmpty());
        PlayerWindow window;
        auto *player = window.findChild<QMediaPlayer *>();
        QVERIFY(player);
        player->setAudioOutput(nullptr); // The decoder check does not need an audio device.
        QAudioBufferOutput output;
        player->setAudioBufferOutput(&output);
        QSignalSpy decoded(&output, &QAudioBufferOutput::audioBufferReceived);
        window.openPaths({path});
        QTRY_VERIFY_WITH_TIMEOUT(!decoded.isEmpty(), 10000);
        const QAudioBuffer buffer = decoded.first().at(0).value<QAudioBuffer>();
        QVERIFY(buffer.isValid());
        QVERIFY(buffer.frameCount() > 0);
        QCOMPARE(buffer.format().sampleRate(), 16000);
        QCOMPARE(player->error(), QMediaPlayer::NoError);
        QCOMPARE(player->duration(), 2000);
        player->stop();
        player->setAudioBufferOutput(nullptr);
    }
    void secondProcessRoutesToPlayer()
    {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService(QStringLiteral("org.openkaryon.Player")));
        PlayerWindow window;
        auto *player = window.findChild<QMediaPlayer *>();
        QVERIFY(player);
        player->setAudioOutput(nullptr);
        PlayerService service(&window);
        QVERIFY(bus.registerObject(QStringLiteral("/org/openkaryon/Player"), &service,
                                   QDBusConnection::ExportScriptableSlots));
        const QString path = wave(QStringLiteral("second tone.wav"));
        QProcess child;
        child.start(QString::fromUtf8(KARYO_PLAYER_EXECUTABLE), {path});
        QVERIFY(child.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 10000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QTRY_COMPARE(player->source(), QUrl::fromLocalFile(path));
        player->stop();
        bus.unregisterObject(QStringLiteral("/org/openkaryon/Player"));
        QVERIFY(bus.unregisterService(QStringLiteral("org.openkaryon.Player")));
    }
};
QTEST_MAIN(PlayerTest)
#include "playertest.moc"
