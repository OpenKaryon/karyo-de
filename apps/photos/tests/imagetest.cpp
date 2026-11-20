// SPDX-License-Identifier: GPL-3.0-or-later
#include "imageview.h"
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

class ImageTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void rotationPublishesReadablePixels()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("sample.png");
        QImage input(20, 30, QImage::Format_RGB32);
        input.fill(Qt::red);
        input.setPixelColor(0, 0, Qt::blue);
        QVERIFY(input.save(path));
        ImageView view;
        view.resize(400, 300);
        QVERIFY(view.open(path));
        QCOMPARE(view.pictureSize(), QSize(20, 30));
        view.rotate(1);
        QString error;
        QVERIFY2(view.saveRotation(&error), qPrintable(error));
        const QImage saved(path);
        QCOMPARE(saved.size(), QSize(30, 20));
        QCOMPARE(saved.pixelColor(29, 0), QColor(Qt::blue));
        QCOMPARE(view.rotation(), 0);
        QVERIFY(view.isFitted());
    }
    void unsupportedWriterPreservesOriginal()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("read-only-format.gif");
        const QByteArray original = QByteArray::fromHex(
            "47494638396101000100800000000000ffffff21f90401000000002c00000000010001000002024401003b");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        ImageView view;
        QVERIFY(view.open(path));
        view.rotate(1);
        QString error;
        QVERIFY(!view.saveRotation(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
        QVERIFY(QImageReader(path).canRead());
    }
};
QTEST_MAIN(ImageTest)
#include "imagetest.moc"
