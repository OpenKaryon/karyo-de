// SPDX-License-Identifier: GPL-3.0-or-later
#include <karyo/ui/window.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/password.h>
#include <karyo/ui/menu.h>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QtTest>

using namespace Karyo::Ui;

class WidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void passwordSubmissionAndBusyState()
    {
        PasswordField field;
        QSignalSpy submitted(&field, &PasswordField::submitted);
        QCOMPARE(field.lineEdit()->echoMode(), QLineEdit::Password);
        field.lineEdit()->setText(QStringLiteral("test password"));
        QTest::keyClick(field.lineEdit(), Qt::Key_Return);
        QCOMPARE(submitted.count(), 1);
        field.setBusy(true);
        QVERIFY(field.lineEdit()->isReadOnly());
        QTest::keyClick(field.lineEdit(), Qt::Key_Return);
        QCOMPARE(submitted.count(), 1);
        field.setBusy(false);
        QVERIFY(!field.lineEdit()->isReadOnly());
        field.showError();
        field.clear();
        QVERIFY(field.text().isEmpty());
    }

    void tabsKeepTheirSelection()
    {
        TitleTabs tabs;
        tabs.addTab(QStringLiteral("folder"), QStringLiteral("Home"));
        tabs.addTab(QStringLiteral("file"), QStringLiteral("Work"));
        tabs.setCurrentIndex(1);
        tabs.removeTab(0);
        QCOMPARE(tabs.count(), 1);
        QCOMPARE(tabs.currentIndex(), 0);
        tabs.removeTab(99);
        QCOMPARE(tabs.count(), 1);
        tabs.removeTab(0);
        QCOMPARE(tabs.currentIndex(), -1);
    }

    void navigationRespectsHiddenRows()
    {
        NavList list;
        const int home = list.addItem(QStringLiteral("home"), QStringLiteral("Home"));
        const int work = list.addItem(QStringLiteral("folder"), QStringLiteral("Work"));
        QSignalSpy activated(&list, &NavList::activated);
        list.setItemVisible(home, false);
        list.resize(list.sizeHint());
        QVERIFY(!list.isItemVisible(home));
        QTest::mouseClick(&list, Qt::LeftButton, Qt::NoModifier, QPoint(60, 12));
        QCOMPARE(list.currentIndex(), work);
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().toInt(), work);
        list.clear();
        QCOMPARE(list.currentIndex(), -1);
    }

    void windowCornersFollowWindowState()
    {
        KaryoWindow window;
        window.resize(400, 240);
        window.show();
        QVERIFY(window.cornerRadius() > 0);
        const QImage image = window.grab().toImage();
        QVERIFY(image.pixelColor(0, 0).alpha() < 32);
        QVERIFY(image.pixelColor(0, image.height() - 1).alpha() < 32);
        window.showMaximized();
        QTRY_VERIFY(window.isMaximized());
        QCOMPARE(window.cornerRadius(), 0.0);
        window.showNormal();
        QTRY_VERIFY(!window.isMaximized());
        QVERIFY(window.cornerRadius() > 0);
    }

    void searchAndMenuKeepTheirContent()
    {
        SearchField search(QStringLiteral("Find files"));
        QCOMPARE(search.lineEdit()->placeholderText(), QStringLiteral("Find files"));
        QMenu menu;
        menu.addAction(QStringLiteral("Open"));
        styleMenu(&menu);
        QCOMPARE(menu.actions().size(), 1);
        QVERIFY(!menu.styleSheet().isEmpty());
    }
};
QTEST_MAIN(WidgetTest)
#include "widgettest.moc"
