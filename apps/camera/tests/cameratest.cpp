// SPDX-License-Identifier: GPL-3.0-or-later
#include "camerawindow.h"
#include <QLabel>
#include <QtTest>

class CameraTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void unavailableDevicesRemainUsable()
    {
        QVERIFY2(QMediaDevices::videoInputs().isEmpty(), "Run only in the disposable no-camera fixture");
        CameraWindow window;
        window.show();
        bool message = false;
        for (auto *label : window.findChildren<QLabel *>())
            message |= label->text() == QStringLiteral("No camera detected.");
        QVERIFY(message);
        int unavailable = 0;
        for (auto *button : window.findChildren<Karyo::Ui::PillButton *>()) {
            if (button->text() == QStringLiteral("Take photo")
                || button->text() == QStringLiteral("Record video")
                || button->text() == QStringLiteral("Open folder")) {
                QVERIFY(!button->isEnabled());
                ++unavailable;
            }
        }
        QCOMPARE(unavailable, 3);
        QVERIFY(window.close());
    }
};
QTEST_MAIN(CameraTest)
#include "cameratest.moc"
