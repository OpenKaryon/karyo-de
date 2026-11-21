// SPDX-License-Identifier: GPL-3.0-or-later
#include "capture.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class CaptureTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_root;
    QString m_command;
    void script(const QByteArray &body)
    {
        QFile file(m_command);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n" + body + "\n");
        file.close();
        QVERIFY(QFile::setPermissions(m_command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }
private Q_SLOTS:
    void clipboardSuccessFailureAndTimeout()
    {
        QVERIFY(m_root.isValid());
        m_command = m_root.filePath(QStringLiteral("wl-copy"));
        const QString copied = m_root.filePath(QStringLiteral("clipboard.png"));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", m_root.path().toUtf8() + ":" + oldPath);
        qputenv("KARYO_QA_CAPTURE_PATH", copied.toUtf8());
        QImage sentinel(1, 1, QImage::Format_RGB32); sentinel.fill(Qt::white);
        QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::blue);
        QGuiApplication::clipboard()->setImage(sentinel);
        script("/bin/cat > \"$KARYO_QA_CAPTURE_PATH\"\nexit 0");
        Capture::copy(image);
        QCOMPARE(QImage(copied).convertToFormat(image.format()), image);
        QCOMPARE(QGuiApplication::clipboard()->image(), sentinel);
        script("exit 7");
        Capture::copy(image);
        QCOMPARE(QGuiApplication::clipboard()->image(), image);
        QGuiApplication::clipboard()->setImage(sentinel);
        script("exec /bin/sleep 10");
        Capture::copy(image);
        QCOMPARE(QGuiApplication::clipboard()->image(), image);
        qputenv("PATH", oldPath);
    }
    void atomicSaveAndFailure()
    {
        const QString path = m_root.filePath(QStringLiteral("saved.png"));
        QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::red);
        QString error;
        QVERIFY2(Capture::save(image, path, &error), qPrintable(error));
        QCOMPARE(QImage(path).convertToFormat(image.format()), image);
        const QString directory = m_root.filePath(QStringLiteral("not-a-file.png"));
        QVERIFY(QDir().mkdir(directory));
        QVERIFY(!Capture::save(image, directory, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo(directory).isDir());
        QCOMPARE(QImage(path).convertToFormat(image.format()), image);
    }
};
QTEST_MAIN(CaptureTest)
#include "capturetest.moc"
