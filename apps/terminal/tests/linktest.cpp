// SPDX-License-Identifier: GPL-3.0-or-later
#include "terminalwindow.h"
#include <qtermwidget.h>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QtTest>

class TerminalLinkTest : public QObject
{
    Q_OBJECT
    QList<QUrl> opened;
public Q_SLOTS:
    void open(const QUrl &url) { opened.append(url); }
private Q_SLOTS:
    void controlClickOnly()
    {
        QDesktopServices::setUrlHandler("https", this, "open");
        QDesktopServices::setUrlHandler("http", this, "open");
        TerminalWindow window;
        window.newTab(QDir::tempPath(), "/bin/sh", {"-c", "sleep 30"});
        window.show();
        auto *terminal = window.findChild<QTermWidget *>();
        QVERIFY(terminal);
        const QUrl url("https://example.org/path?q=1");
        terminal->urlActivated(url, false);
        QVERIFY(opened.isEmpty());
        QTest::keyPress(window.windowHandle(), Qt::Key_Control, Qt::ControlModifier);
        QCoreApplication::processEvents();
        QCOMPARE(QGuiApplication::keyboardModifiers(), Qt::ControlModifier);
        terminal->urlActivated(url, false);
        QCOMPARE(opened, QList<QUrl>{url});
        terminal->urlActivated(QUrl("file:///tmp/not-a-web-link"), false);
        QCOMPARE(opened.size(), 1);
        QTest::keyRelease(window.windowHandle(), Qt::Key_Control);
        terminal->urlActivated(url, false);
        QCOMPARE(opened.size(), 1);
        QDesktopServices::unsetUrlHandler("https");
        QDesktopServices::unsetUrlHandler("http");
    }
};
QTEST_MAIN(TerminalLinkTest)
#include "linktest.moc"
