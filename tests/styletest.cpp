// SPDX-License-Identifier: GPL-3.0-or-later
#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>
#include <QDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QStyleFactory>
#include <QStyleOptionSlider>
#include <QVBoxLayout>
#include <QtTest>

class StyleTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(QStyleFactory::keys().contains(QStringLiteral("Karyo"), Qt::CaseInsensitive));
        QVERIFY(QApplication::setStyle(QStringLiteral("Karyo")));
    }

    void factoryAndPalette()
    {
        QCOMPARE(qApp->style()->objectName().toLower(), QStringLiteral("karyo"));
        QCOMPARE(qApp->palette().color(QPalette::Window), Karyo::Ui::Theme::current().win);
    }

    void sliderHandleFitsItsBounds()
    {
        QSlider slider(Qt::Horizontal);
        QStyleOptionSlider option;
        option.initFrom(&slider);
        option.rect = QRect(0, 0, 180, 32);
        option.orientation = Qt::Horizontal;
        option.minimum = 0;
        option.maximum = 100;
        option.sliderPosition = 50;
        const QRect handle = slider.style()->subControlRect(
            QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider);
        QVERIFY(handle.isValid());
        QVERIFY(option.rect.contains(handle));
    }

    void clientFrameIsExplicitAndSkipsOwnWindows()
    {
        QDialog dialog;
        dialog.resize(320, 200);
        dialog.show();
        QCoreApplication::processEvents();
        const bool enabled = qEnvironmentVariableIntValue("KARYO_CLIENT_FRAME") > 0;
        QCOMPARE(dialog.property("_karyo_client_frame").toInt() == 2, enabled);
        Karyo::Ui::KaryoWindow own;
        own.show();
        QCoreApplication::processEvents();
        QVERIFY(!own.property("_karyo_client_frame").isValid());
        QMenu menu;
        QVERIFY(!menu.property("_karyo_client_frame").isValid());
    }

    void standardControlsRender()
    {
        for (auto scheme : {Karyo::Ui::ColorScheme::Light, Karyo::Ui::ColorScheme::Dark}) {
            Karyo::Ui::Theme::setScheme(scheme);
            QWidget content;
            auto *layout = new QVBoxLayout(&content);
            layout->addWidget(new QPushButton(QStringLiteral("Connect")));
            layout->addWidget(new QLineEdit(QStringLiteral("Example")));
            layout->addWidget(new QSlider(Qt::Horizontal));
            content.resize(320, 200);
            content.show();
            QCoreApplication::processEvents();
            QVERIFY(!content.grab().isNull());
        }
    }
};
QTEST_MAIN(StyleTest)
#include "styletest.moc"
