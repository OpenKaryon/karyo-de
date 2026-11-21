// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include "pages.h"
#include "widgets.h"
#include <karyo/ui/theme.h>
#include <QAbstractButton>
#include <QLabel>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class PersonalizationTest : public QObject {
    Q_OBJECT
    static SettingRow *row(SettingsPage *page, const QString &title) {
        for (auto *candidate : page->findChildren<SettingRow *>())
            if (candidate->property("settingTitle").toString() == title) return candidate;
        return nullptr;
    }
    static QAbstractButton *button(SettingsPage *page, const QString &text) {
        for (auto *candidate : page->findChildren<QAbstractButton *>())
            if (candidate->text() == text) return candidate;
        return nullptr;
    }
private Q_SLOTS:
    void changesPersistAndSubpagesRemainAvailable() {
        QTemporaryDir root; qputenv("XDG_CONFIG_HOME", root.path().toUtf8());
        std::unique_ptr<SettingsPage> page(createPersonalizationPage(nullptr));
        page->resize(1250, 1000); page->show();
        QVERIFY(button(page.get(), "Dark")); button(page.get(), "Dark")->click();
        QCOMPARE(Config::desktop().value("Appearance/theme").toString(), QString("dark"));
        button(page.get(), "Light")->click();
        QCOMPARE(Config::desktop().value("Appearance/theme").toString(), QString("light"));
        auto *language = row(page.get(), "Show input language in tray"); QVERIFY(language);
        auto *toggle = language->findChild<ToggleSwitch *>(); QVERIFY(toggle); toggle->click();
        QCOMPARE(Config::shell().value("Taskbar/showLanguage").toBool(), false);
        auto *layout = row(page.get(), "Start layout"); QVERIFY(layout);
        auto *segments = layout->findChild<Segmented *>(); QVERIFY(segments);
        segments->setCurrentIndex(2);
        QCOMPARE(Config::shell().value("Start/layout").toString(), QString("search"));
        SettingsPage *opened = nullptr;
        connect(page.get(), &SettingsPage::subPageRequested, this, [&](SettingsPage *child) { opened = child; });
        row(page.get(), "Taskbar")->clicked(); QVERIFY(opened);
        std::unique_ptr<SettingsPage> taskbar(opened); opened = nullptr;
        QVERIFY(taskbar->keywords().contains("Taskbar"));
        row(page.get(), "Background")->clicked(); QVERIFY(opened);
        std::unique_ptr<SettingsPage> background(opened); opened = nullptr;
        auto *backgroundMode = row(background.get(), "Personalize your background"); QVERIFY(backgroundMode);
        auto *mode = backgroundMode->findChild<Segmented *>(); QVERIFY(mode);
        mode->setCurrentIndex(1);
        QCOMPARE(Config::desktop().value("Background/mode").toString(), QString("solid"));
        row(page.get(), "Fonts")->clicked(); QVERIFY(opened);
        std::unique_ptr<SettingsPage> fonts(opened);
        QVERIFY(!fonts->findChildren<QLabel *>().isEmpty());
        QVERIFY(Config::desktop().fileName().startsWith(root.path()));
    }
};
QTEST_MAIN(PersonalizationTest)
#include "personalizationtest.moc"
