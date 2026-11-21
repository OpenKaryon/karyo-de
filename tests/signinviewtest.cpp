// SPDX-License-Identifier: GPL-3.0-or-later
#include "signinview.h"
#include <karyo/ui/password.h>
#include <QtTest>
#include <QLabel>

class SignInViewTest : public QObject
{
    Q_OBJECT
private slots:
    void externalTextIsLiteral()
    {
        SignInView view;
        const QString name = "<b>Fixture User</b>", detail = "<i>fixture</i>";
        const QString message = "<a href='https://example.invalid'>Literal PAM prompt</a>";
        view.setUser(name, detail);
        view.showMessage(message);
        int found = 0;
        for (auto *label : view.findChildren<QLabel *>())
            if (QStringList {name, detail, message}.contains(label->text())) {
                QCOMPARE(label->textFormat(), Qt::PlainText);
                ++found;
            }
        QCOMPARE(found, 3);
    }
    void passwordErrorKeepsFormGeometry_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("720p") << QSize(1280, 720);
        QTest::newRow("1080p") << QSize(1920, 1080);
    }
    void passwordErrorKeepsFormGeometry()
    {
        QFETCH(QSize, size);
        SignInView view;
        view.setUser(QStringLiteral("Fixture User"), QStringLiteral("fixture"));
        view.resize(size);
        view.show();
        QCoreApplication::processEvents();
        auto *password = view.password();
        QWidget *block = password->parentWidget();
        const int top = password->y();
        QVERIFY(top >= 84);
        view.showMessage(QStringLiteral("That password did not work. Try again."));
        QCoreApplication::processEvents();
        QCOMPARE(password->y(), top);
        QVERIFY(block->geometry().bottom() < view.height() - 30);
        view.showMessage({});
        QCoreApplication::processEvents();
        QCOMPARE(password->y(), top);
    }
};
QTEST_MAIN(SignInViewTest)
#include "signinviewtest.moc"
