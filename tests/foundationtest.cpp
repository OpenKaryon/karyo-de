// SPDX-License-Identifier: GPL-3.0-or-later
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

using namespace Karyo::Ui;

class FoundationTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings settings(QSettings::NativeFormat, QSettings::UserScope,
                           QStringLiteral("openkaryon"), QStringLiteral("desktop"));
        settings.clear();
    }

    void colors()
    {
        QCOMPARE(neutral(100), QColor(0xf9, 0xf4, 0xed));
        QCOMPARE(neutral(-200), neutral(100));
        QCOMPARE(neutral(1200), neutral(900));
        QCOMPARE(mix(Qt::red, Qt::blue, 100), QColor(Qt::red));
        QCOMPARE(mix(Qt::red, Qt::blue, 0), QColor(Qt::blue));
        QCOMPARE(alpha(Qt::black, 0).alpha(), 0);
        QCOMPARE(alpha(Qt::black, 100).alpha(), 255);
    }

    void schemesAndSignals()
    {
        const Theme light = Theme::make(ColorScheme::Light);
        const Theme dark = Theme::make(ColorScheme::Dark);
        QVERIFY(light.win.lightness() > dark.win.lightness());
        Theme::setScheme(ColorScheme::Light);
        QSignalSpy changes(ThemeNotifier::instance(), &ThemeNotifier::changed);
        Theme::setScheme(ColorScheme::Dark);
        QCOMPARE(changes.count(), 1);
        QCOMPARE(Theme::follow(light.win), dark.win);
        Theme::setScheme(ColorScheme::Dark);
        QCOMPARE(changes.count(), 1);
    }

    void compatibleSettings()
    {
        QSettings settings(QSettings::NativeFormat, QSettings::UserScope,
                           QStringLiteral("openkaryon"), QStringLiteral("desktop"));
        settings.setValue(QStringLiteral("Appearance/theme"), QStringLiteral("dark"));
        settings.setValue(QStringLiteral("Appearance/accent"), QStringLiteral("sage"));
        settings.sync();
        QCOMPARE(Theme::configuredScheme(), ColorScheme::Dark);
        Theme::followConfiguration();
        QCOMPARE(Theme::current().accent, QColor(0x7a, 0x8a, 0x5e));
        settings.setValue(QStringLiteral("Appearance/theme"), QStringLiteral("light"));
        settings.sync();
        QTRY_COMPARE(Theme::current().scheme, ColorScheme::Light);
    }

    void fontPreferences()
    {
        QCOMPARE(bodyFont(13).families().first(), QStringLiteral("IBM Plex Sans"));
        QCOMPARE(monoFont(14).families().first(), QStringLiteral("IBM Plex Mono"));
        QCOMPARE(headingFont(20).pixelSize(), 20);
    }

    void paintingPreservesCaller()
    {
        QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setPen(Qt::green);
            painter.setBrush(Qt::blue);
            painter.translate(2, 3);
            const auto transform = painter.transform();
            fillRounded(painter, QRectF(10, 10, 60, 40), 5, Qt::red);
            QCOMPARE(painter.pen().color(), QColor(Qt::green));
            QCOMPARE(painter.brush().color(), QColor(Qt::blue));
            QCOMPARE(painter.transform(), transform);
            drawLargeShadow(painter, QRectF(20, 20, 30, 30), 4);
            QCOMPARE(painter.transform(), transform);
        }
        QCOMPARE(image.pixelColor(30, 30), QColor(Qt::red));
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
    }
};
QTEST_MAIN(FoundationTest)
#include "foundationtest.moc"
