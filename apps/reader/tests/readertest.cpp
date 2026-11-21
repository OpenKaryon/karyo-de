// SPDX-License-Identifier: GPL-3.0-or-later
#include "documentpage.h"
#include "readerwindow.h"

#include <QApplication>
#include <QDBusConnection>
#include <QFile>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class ReaderTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_home;
    QString makeDocument(const QString &name)
    {
        const QString path = m_home.filePath(name);
        QPdfWriter writer(path);
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.setFont(QFont(QStringLiteral("DejaVu Sans"), 20));
        painter.drawText(100, 100, QStringLiteral("Karyo searchable first page"));
        writer.newPage();
        painter.drawText(100, 100, QStringLiteral("Second page with another Karyo match"));
        painter.end();
        return path;
    }
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
        QCoreApplication::setOrganizationName(QStringLiteral("KaryoTest"));
        QCoreApplication::setApplicationName(QStringLiteral("reader-test"));
    }
    void navigationSearchAndPosition()
    {
        const QString path = makeDocument(QStringLiteral("two pages.pdf"));
        {
            DocumentPage page;
            page.resize(800, 600);
            QVERIFY2(page.open(path), qPrintable(page.error()));
            QCOMPARE(page.pageCount(), 2);
            auto *document = page.findChild<QPdfDocument *>();
            QVERIFY(document);
            const QImage image = document->render(0, QSize(600, 800));
            QVERIFY(!image.isNull());
            QVERIFY(document->getAllText(0).text().contains(QStringLiteral("Karyo")));
            page.show();
            QCoreApplication::processEvents();
            page.search(QStringLiteral("Karyo"));
            QTRY_COMPARE_WITH_TIMEOUT(page.matchCount(), 2, 10000);
            page.nextMatch(false);
            QCOMPARE(page.currentMatch(), 0);
            page.nextMatch(false);
            QCOMPARE(page.currentMatch(), 1);
            page.search(QStringLiteral("Second"));
            QTRY_COMPARE_WITH_TIMEOUT(page.matchCount(), 1, 10000);
            page.search(QString());
            QTRY_COMPARE(page.matchCount(), 0);
            page.zoomTo(1.75);
            page.goToPage(1);
            QTRY_COMPARE(page.currentPage(), 1);
            page.setSidebar(0);
            page.savePosition();
        }
        DocumentPage restored;
        QVERIFY(restored.open(path));
        restored.resize(800, 600);
        restored.show();
        QTRY_COMPARE(restored.currentPage(), 1);
        QCOMPARE(restored.fit(), DocumentPage::Fit::Custom);
        QCOMPARE(restored.zoom(), 1.75);
        QCOMPARE(restored.sidebar(), 0);
        restored.goToPage(99);
        QCOMPARE(restored.currentPage(), 1);
        restored.goToPage(-5);
        QCOMPARE(restored.currentPage(), 0);
    }
    void rejectsMissingAndNonPdf()
    {
        DocumentPage missing;
        QVERIFY(!missing.open(m_home.filePath(QStringLiteral("absent.pdf"))));
        QVERIFY(!missing.error().isEmpty());
        const QString path = m_home.filePath(QStringLiteral("invalid.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not a PDF");
        file.close();
        DocumentPage invalid;
        QVERIFY(!invalid.open(path));
        QVERIFY(!invalid.error().isEmpty());
    }
    void secondProcessReusesTabs()
    {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        QVERIFY(bus.registerService(QStringLiteral("org.openkaryon.Reader")));
        ReaderWindow window;
        ReaderService service(&window);
        QVERIFY(bus.registerObject(QStringLiteral("/org/openkaryon/Reader"), &service,
                                   QDBusConnection::ExportScriptableSlots));
        const QString first = makeDocument(QStringLiteral("first.pdf"));
        const QString second = makeDocument(QStringLiteral("second.pdf"));
        window.openFiles({first});
        QCOMPARE(window.findChildren<DocumentPage *>().size(), 1);
        QProcess child;
        child.start(QString::fromUtf8(KARYO_READER_EXECUTABLE), {first, second});
        QVERIFY(child.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 10000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QCOMPARE(window.findChildren<DocumentPage *>().size(), 2);
        QStringList opened;
        for (auto *page : window.findChildren<DocumentPage *>())
            opened.append(page->path());
        QVERIFY(opened.contains(first));
        QVERIFY(opened.contains(second));
        bus.unregisterObject(QStringLiteral("/org/openkaryon/Reader"));
        QVERIFY(bus.unregisterService(QStringLiteral("org.openkaryon.Reader")));
    }
};
QTEST_MAIN(ReaderTest)
#include "readertest.moc"
