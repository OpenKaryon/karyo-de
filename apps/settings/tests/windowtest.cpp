// SPDX-License-Identifier: GPL-3.0-or-later
#include "pages.h"
#include "settingswindow.h"
#include "widgets.h"
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPointer>
#include <QProcess>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <karyo/ui/navigation.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>
#include <memory>
namespace Ui = Karyo::Ui;
class SettingsWindowTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_path = qgetenv("PATH"), m_home = qgetenv("HOME");
    static void put(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), data.size());
    }
    static SettingsPage *current(SettingsWindow &window)
    {
        auto *stack = window.findChild<QStackedWidget *>("settingsPages");
        return stack->currentWidget()->findChild<SettingsPage *>();
    }
private Q_SLOTS:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        qputenv("HOME", m_root->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_root->filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", m_root->filePath("data").toUtf8());
        qputenv("PATH", m_root->filePath("bin").toUtf8() + ':' + m_path);
        for (const auto &name :
            QStringList { "apt-get", "wpctl", "nmcli", "locale", "dpkg-query", "wlr-randr" })
        {
            const QString path = m_root->filePath("bin/" + name);
            put(path,
                "#!/bin/sh\ncase \"${0##*/}\" in\nlocale) printf 'en_US.utf8\\nru_RU.utf8\\n' ;;\nwlr-randr) "
                "printf '[]' ;;\nwpctl) printf 'Volume: 0.4\\n' ;;\n*) exit 0 ;;\nesac\n");
            QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        }
    }
    void cleanup()
    {
        auto reaped = []
        {
            for (auto *process : QCoreApplication::instance()->findChildren<QProcess *>())
                if (process->state() != QProcess::NotRunning)
                    return false;
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(reaped(), 3000);
        qputenv("PATH", m_path);
        qputenv("HOME", m_home);
    }
    void publicRoutesNavigateAllSections()
    {
        SettingsWindow window;
        window.resize(1280, 800);
        window.show();
        auto *navigation = window.findChild<Ui::NavList *>("settingsNavigation");
        QVERIFY(navigation);
        const QStringList keys { "system", "devices", "network", "personalization", "apps", "accounts",
            "time", "privacy", "updates", "about" };
        for (int i = 0; i < keys.size(); ++i)
        {
            window.showSection(keys.at(i));
            QCOMPARE(navigation->currentIndex(), i);
            QVERIFY(current(window));
            QCOMPARE(window.findChild<QStackedWidget *>("settingsPages")->currentIndex(), i);
        }
        window.showSection("unknown");
        QCOMPARE(navigation->currentIndex(), 9);
    }
    void storageSubpageBackAndThemeRebuild()
    {
        SettingsWindow window;
        window.resize(1280, 800);
        window.show();
        window.showSection("storage");
        auto *back = window.findChild<QAbstractButton *>("settingsBack");
        QVERIFY(back->isEnabled());
        QCOMPARE(current(window)->title(), QString("Storage"));
        back->click();
        QVERIFY(!back->isEnabled());
        QCOMPARE(current(window)->title(), QString("System"));
        window.showSection("personalization");
        SettingRow *background = nullptr;
        for (auto *row : current(window)->findChildren<SettingRow *>())
            if (row->property("settingTitle").toString() == "Background")
                background = row;
        QVERIFY(background);
        QTest::mouseClick(background, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCOMPARE(current(window)->title(), QString("Background"));
        QVERIFY(back->isEnabled());
        back->click();
        QCOMPARE(current(window)->title(), QString("Personalization"));
        QPointer<Ui::NavList> oldNavigation = window.findChild<Ui::NavList *>("settingsNavigation");
        Ui::Theme::setScheme(Ui::Theme::current().scheme == Ui::ColorScheme::Dark ? Ui::ColorScheme::Light
                                                                                  : Ui::ColorScheme::Dark);
        QTRY_VERIFY(oldNavigation.isNull());
        QCOMPARE(window.findChild<Ui::NavList *>("settingsNavigation")->currentIndex(), 3);
        QCOMPARE(current(window)->title(), QString("Personalization"));
    }
    void searchFindsKeywordsAndEnterNavigates()
    {
        SettingsWindow window;
        window.resize(1280, 800);
        window.show();
        auto *search = window.findChild<QWidget *>("settingsSearch")->findChild<QLineEdit *>();
        QVERIFY(search);
        search->setText("timezone");
        auto *navigation = window.findChild<Ui::NavList *>("settingsNavigation");
        QVERIFY(navigation->isItemVisible(6));
        for (int i = 0; i < 10; ++i)
            if (i != 6)
                QVERIFY(!navigation->isItemVisible(i));
        search->returnPressed();
        QCOMPARE(navigation->currentIndex(), 6);
        QCOMPARE(current(window)->title(), QString("Time & language"));
        search->clear();
        for (int i = 0; i < 10; ++i)
            QVERIFY(navigation->isItemVisible(i));
    }
};
QTEST_MAIN(SettingsWindowTest)
#include "windowtest.moc"
