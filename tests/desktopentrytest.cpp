// SPDX-License-Identifier: GPL-3.0-or-later
#include <karyo/ui/appstyle.h>
#include <karyo/ui/desktopentry.h>
#include <karyo/ui/icons.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class DesktopEntryTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_data;
private Q_SLOTS:
    void sessionMetadataUsesDesktopEntryListSyntax()
    {
        const QString path = m_data.filePath("session.desktop");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nName=Session\nExec=/bin/sh\n"
                   "TryExec=/usr/bin/example\\slauncher\nDesktopNames=Karyo;wlroots;\n");
        file.close();
        const auto entry = DesktopEntry::load(path, "session.desktop");
        QVERIFY(entry);
        QCOMPARE(entry->tryExec, QString("/usr/bin/example launcher"));
        QCOMPARE(entry->desktopNames, QStringList({"Karyo", "wlroots"}));
    }
    void publicAndLegacyApplicationsKeepVisualRoles()
    {
        for (const QString &name : {"files", "terminal", "editor", "photos", "player",
                                   "snip", "reader", "calculator", "clock", "appcenter",
                                   "monitor", "settings"}) {
            DesktopEntry entry;
            entry.id = QStringLiteral("karyo-%1.desktop").arg(name);
            const auto current = AppStyle::forEntry(entry);
            QVERIFY2(!current.glyph.isEmpty(), qPrintable(entry.id));
            entry.id = QStringLiteral("openkaryon-%1.desktop").arg(name);
            const auto legacy = AppStyle::forEntry(entry);
            QCOMPARE(legacy.glyph, current.glyph);
            QCOMPARE(legacy.tile, current.tile);
            QCOMPARE(legacy.ink, current.ink);
        }
    }
#ifdef KARYO_PROCESS_FIXTURE
    void launchKeepsLiveCaller_data()
    {
        QTest::addColumn<bool>("withFile");
        QTest::newRow("plain") << false;
        QTest::newRow("with-file") << true;
    }
    void launchKeepsLiveCaller()
    {
        QFETCH(bool, withFile);
        const QString path = m_data.filePath(withFile ? "parent with file.txt" : "parent plain.txt");
        DesktopEntry entry;
        entry.exec = QStringLiteral("\"%1\" %2").arg(QString::fromUtf8(KARYO_PROCESS_FIXTURE),
                       withFile ? QStringLiteral("%f") : QStringLiteral("\"%1\"").arg(path));
        qint64 pid = 0;
        QVERIFY(withFile ? entry.launchWith({path}, &pid) : entry.launch());
        if (withFile)
            QVERIFY(pid > 0);
        QTRY_VERIFY(QFile::exists(path));
        QFile result(path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll().toLongLong(), QCoreApplication::applicationPid());
    }
    void launchedApplicationSurvivesCallerExit()
    {
        const QString path = m_data.filePath("survived.txt");
        QCOMPARE(QProcess::execute(QString::fromUtf8(KARYO_PROCESS_FIXTURE),
                                  {QStringLiteral("--spawn-and-exit"), path}), 0);
        QTRY_VERIFY(QFile::exists(path));
        QFile result(path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("survived"));
    }
    void missingLaunchFails()
    {
        DesktopEntry entry;
        entry.exec = QStringLiteral("/no/such/karyo-fixture-program");
        QVERIFY(!entry.launch());
    }
#endif
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("XDG_DATA_HOME", m_data.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_data.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_data.filePath(QStringLiteral("system-config")).toUtf8());
        qputenv("XDG_DATA_DIRS", m_data.filePath(QStringLiteral("system")).toUtf8());
        QVERIFY(QDir().mkpath(m_data.filePath(QStringLiteral("applications"))));
        QImage icon(24, 24, QImage::Format_ARGB32);
        icon.fill(QColor("#285fc5"));
        QVERIFY(icon.save(m_data.filePath(QStringLiteral("chatgpt.png"))));
        auto entry = [this](const QString &id, const QString &fields) {
            QFile file(m_data.filePath(QStringLiteral("applications/") + id));
            if (!file.open(QIODevice::WriteOnly)) return false;
            const QByteArray data = (QStringLiteral("[Desktop Entry]\nType=Application\n") + fields).toUtf8();
            return file.write(data) == data.size();
        };
        QVERIFY(entry(QStringLiteral("chatgpt.desktop"),
                      QStringLiteral("Name=ChatGPT\nExec=chatgpt %U\nIcon=%1\n").arg(m_data.filePath(QStringLiteral("chatgpt.png")))));
        QVERIFY(entry(QStringLiteral("work-chat.desktop"),
                      QStringLiteral("Name=Work chat\nExec=work-chat\nStartupWMClass=chatgpt (/tmp/work-profile)\n")));
        QVERIFY(entry(QStringLiteral("org.example.wrapped.desktop"),
                      QStringLiteral("Name=Wrapped app\nExec=/opt/tools/npm-widget %U\nIcon=%1\n").arg(m_data.filePath(QStringLiteral("chatgpt.png")))));
        QVERIFY(entry(QStringLiteral("com.Mattermost.Desktop.desktop"),
                      QStringLiteral("Name=Mattermost\nExec=/opt/Mattermost/com.Mattermost.Desktop %U\n"
                                     "StartupWMClass=com.Mattermost.Desktop\nIcon=%1\n")
                          .arg(m_data.filePath(QStringLiteral("chatgpt.png")))));
        QVERIFY(entry(QStringLiteral("net.Example.Editor.desktop"),
                      QStringLiteral("Name=Editor\nExec=example-editor\n")));
        QVERIFY(entry(QStringLiteral("org.Example.Editor.desktop"),
                      QStringLiteral("Name=Other editor\nExec=other-editor\n")));
        QVERIFY(entry(QStringLiteral("net.Example.Player.desktop"),
                      QStringLiteral("Name=Player\nExec=example-player\n")));
        QVERIFY(entry(QStringLiteral("text-reader.desktop"),
                      QStringLiteral("Name=Text reader\nExec=text-reader\nMimeType=text/plain;\n")));
        QVERIFY(entry(QStringLiteral("player.desktop"),
                      QStringLiteral("Name=Profile player\nExec=profile-player\nStartupWMClass=Example.Player\n")));
    }

    void mimeDefaultsFollowCurrentDesktops()
    {
        const QString config = m_data.filePath(QStringLiteral("config"));
        QVERIFY(QDir().mkpath(config));
        auto defaults = [&config](const QString &desktop, const QByteArray &id) {
            QFile file(config + QLatin1Char('/') + desktop + QStringLiteral("-mimeapps.list"));
            if (!file.open(QIODevice::WriteOnly))
                return false;
            return file.write("[Default Applications]\ntext/plain=" + id + ";\n") > 0;
        };
        QVERIFY(defaults(QStringLiteral("other"), "work-chat.desktop"));
        QVERIFY(defaults(QStringLiteral("lxqt"), "chatgpt.desktop"));
        qputenv("XDG_CURRENT_DESKTOP", "Karyo:Other");
        const auto selected = DesktopEntry::defaultFor(QStringLiteral("text/plain"));
        QVERIFY(selected);
        QCOMPARE(selected->id, QStringLiteral("work-chat.desktop"));
        qputenv("XDG_CURRENT_DESKTOP", "Karyo");
        const auto generic = DesktopEntry::defaultFor(QStringLiteral("text/plain"));
        QVERIFY(generic);
        QCOMPARE(generic->id, QStringLiteral("text-reader.desktop"));
    }

    void distributionMimeDefaultsAreDesktopSpecificAndUserOverridable()
    {
        const QString folder = m_data.filePath("system/applications");
        QVERIFY(QDir().mkpath(folder));
        QFile defaults(folder + "/karyo-mimeapps.list");
        QVERIFY(defaults.open(QIODevice::WriteOnly));
        defaults.write("[Default Applications]\napplication/x-karyo-win32-fixture=work-chat.desktop;\n");
        defaults.close();
        qputenv("XDG_CURRENT_DESKTOP", "Karyo:Other");
        auto selected = DesktopEntry::defaultFor("application/x-karyo-win32-fixture");
        QVERIFY(selected);
        QCOMPARE(selected->id, QString("work-chat.desktop"));
        qputenv("XDG_CURRENT_DESKTOP", "Other");
        QVERIFY(!DesktopEntry::defaultFor("application/x-karyo-win32-fixture"));
        QFile user(m_data.filePath("config/mimeapps.list"));
        QVERIFY(user.open(QIODevice::WriteOnly));
        user.write("[Default Applications]\napplication/x-karyo-win32-fixture=chatgpt.desktop;\n");
        user.close();
        qputenv("XDG_CURRENT_DESKTOP", "Karyo");
        selected = DesktopEntry::defaultFor("application/x-karyo-win32-fixture");
        QVERIFY(selected);
        QCOMPARE(selected->id, QString("chatgpt.desktop"));
        QVERIFY(user.remove());
    }

    void newlyRegisteredApplicationsInvalidateTheIdentityCache()
    {
        QVERIFY(!DesktopEntry::forAppId("late-windows-fixture.exe"));
        const QString path = m_data.filePath("applications/karyo-win32-fixture.desktop");
        QFile entry(path);
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("[Desktop Entry]\nType=Application\nName=Late Windows app\nExec=late-app\n"
                    "StartupWMClass=late-windows-fixture.exe\nMimeType=application/x-karyo-late-fixture;\n");
        entry.close();
        const auto selected = DesktopEntry::forAppId("late-windows-fixture.exe");
        QVERIFY(selected);
        QCOMPARE(selected->id, QString("karyo-win32-fixture.desktop"));
        const auto handler = DesktopEntry::defaultFor("application/x-karyo-late-fixture");
        QVERIFY(handler);
        QCOMPARE(handler->id, selected->id);
        QVERIFY(entry.remove());
        QVERIFY(!DesktopEntry::forAppId("late-windows-fixture.exe"));
        QVERIFY(!DesktopEntry::defaultFor("application/x-karyo-late-fixture"));
    }

    void embeddedIcons()
    {
        QVERIFY(Karyo::Ui::hasIcon(QStringLiteral("folder")));
        QVERIFY(!Karyo::Ui::hasIcon(QStringLiteral("missing-icon")));
        const QPixmap icon = Karyo::Ui::icon(QStringLiteral("folder"), 24, Qt::red, 2);
        QCOMPARE(icon.size(), QSize(48, 48));
        QCOMPARE(icon.devicePixelRatio(), 2.0);
        const QImage image = icon.toImage();
        bool visible = false;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() > 200) {
                    QCOMPARE(pixel.red(), 255);
                    QCOMPARE(pixel.green(), 0);
                    visible = true;
                }
            }
        }
        QVERIFY(visible);
    }

    void matching_data()
    {
        QTest::addColumn<QString>("appId");
        QTest::addColumn<bool>("matches");
        QTest::newRow("plain") << QStringLiteral("chatgpt") << true;
        QTest::newRow("case") << QStringLiteral("ChatGPT") << true;
        QTest::newRow("profile") << QStringLiteral("chatgpt (/home/user/.config/Codex)") << true;
        QTest::newRow("other-user") << QStringLiteral("chatgpt (/home/alex/profiles/one)") << true;
        QTest::newRow("spaces-parentheses") << QStringLiteral("chatgpt (/home/alex/Profile (Work))") << true;
        QTest::newRow("desktop-profile") << QStringLiteral("chatgpt.desktop (/tmp/profile)") << true;
        QTest::newRow("namespace-profile") << QStringLiteral("org.example.chatgpt (/tmp/profile)") << true;
        QTest::newRow("label") << QStringLiteral("chatgpt (Work)") << false;
        QTest::newRow("relative") << QStringLiteral("chatgpt (profiles/Work)") << false;
        QTest::newRow("missing-close") << QStringLiteral("chatgpt (/tmp/profile") << false;
        QTest::newRow("trailing-text") << QStringLiteral("chatgpt (/tmp/profile) extra") << false;
        QTest::newRow("other-application") << QStringLiteral("chatgpt-helper (/tmp/profile)") << false;
        QTest::newRow("empty") << QStringLiteral("") << false;
    }

    void matching()
    {
        QFETCH(QString, appId);
        QFETCH(bool, matches);
        const auto entry = DesktopEntry::find(QStringLiteral("chatgpt.desktop"));
        QVERIFY(entry);
        QCOMPARE(entry->matchesAppId(appId), matches);
    }

    void profileResolvesDesktopIcon()
    {
        const auto entry = DesktopEntry::forAppId(QStringLiteral("chatgpt (/home/user/.config/Codex)"));
        QVERIFY(entry);
        QCOMPARE(entry->id, QStringLiteral("chatgpt.desktop"));
        const AppStyle style = AppStyle::forEntry(*entry);
        QVERIFY(!style.themeIcon.isNull());
        QVERIFY(style.monogram.isEmpty());
        const QImage pixels = style.themeIcon.pixmap(24, 24).toImage();
        QCOMPARE(pixels.pixelColor(12, 12), QColor("#285fc5"));
    }

    void exactProfileClassHasPriority()
    {
        const auto entry = DesktopEntry::forAppId(QStringLiteral("chatgpt (/tmp/work-profile)"));
        QVERIFY(entry);
        QCOMPARE(entry->id, QStringLiteral("work-chat.desktop"));
    }

    void executableAndStartupClassStillMatch()
    {
        const auto wrapped = DesktopEntry::forAppId(QStringLiteral("npm-widget (/tmp/profile with spaces)"));
        QVERIFY(wrapped);
        QCOMPARE(wrapped->id, QStringLiteral("org.example.wrapped.desktop"));
        DesktopEntry entry;
        entry.id = QStringLiteral("legacy.desktop");
        entry.startupWmClass = QStringLiteral("LegacyClass");
        QVERIFY(entry.matchesAppId(QStringLiteral("LegacyClass")));
        QVERIFY(entry.matchesAppId(QStringLiteral("LegacyClass (/tmp/profile)")));
    }

    void matchesQualifiedDesktopIdByUniqueSuffix()
    {
        const auto entry = DesktopEntry::forAppId(QStringLiteral("Mattermost.Desktop"));
        QVERIFY(entry);
        QCOMPARE(entry->id, QStringLiteral("com.Mattermost.Desktop.desktop"));
        QVERIFY(!AppStyle::forEntry(*entry).themeIcon.pixmap(24, 24).isNull());
        const auto lower = DesktopEntry::forAppId(QStringLiteral("mattermost.desktop"));
        QVERIFY(lower);
        QCOMPARE(lower->id, entry->id);
        QVERIFY(!DesktopEntry::forAppId(QStringLiteral("most.Desktop")));
        QVERIFY(!DesktopEntry::forAppId(QStringLiteral("Example.Editor")));

        const auto player = DesktopEntry::forAppId(QStringLiteral("Example.Player"));
        QVERIFY(player);
        QCOMPARE(player->id, QStringLiteral("player.desktop"));
    }
};

QTEST_MAIN(DesktopEntryTest)
#include "desktopentrytest.moc"
