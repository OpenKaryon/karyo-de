// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRawFont>
#include <QtTest>
class FontPayloadTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void completeFonts_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("expected");
        QFile hashes(QStringLiteral(KARYO_FONT_DIR "/SHA256SUMS"));
        QVERIFY(hashes.open(QIODevice::ReadOnly));
        int count = 0;
        for (const auto &line : hashes.readAll().split('\n')) {
            if (line.trimmed().isEmpty())
                continue;
            const auto fields = line.simplified().split(' ');
            QCOMPARE(fields.size(), 2);
            QTest::newRow(fields.at(1).constData()) << QString::fromUtf8(fields.at(1)) << fields.at(0);
            ++count;
        }
        QCOMPARE(count, 16);
    }
    void completeFonts()
    {
        QFETCH(QString, name);
        QFETCH(QByteArray, expected);
        const QString path = QDir(QStringLiteral(KARYO_FONT_DIR)).filePath(name);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex(), expected);
        const QRawFont font(path, 16);
        QVERIFY(font.isValid());
        QCOMPARE(font.familyName(), name.startsWith("IBMPlexSans-") ? QString("IBM Plex Sans") : QString("IBM Plex Mono"));
        for (const char32_t character : U"AbcЁёЖжЯя0123")
            if (character)
                QVERIFY2(font.supportsCharacter(uint(character)), qPrintable(name));
    }
};
QTEST_MAIN(FontPayloadTest)
#include "fontpayloadtest.moc"
