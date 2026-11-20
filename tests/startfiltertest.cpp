// SPDX-License-Identifier: GPL-3.0-or-later
#include "start/startmenu.h"
#include "start/startwidgets.h"
#include <karyo/ui/style.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class StartFilterTest : public QObject
{
    Q_OBJECT
    QList<SearchResult> rows(ResultList *list)
    {
        QList<SearchResult> out;
        list->moveCurrent(-10000);
        // A fresh result set always selects its first row.
        for (int i = 0; i < 1000 && list->current(); ++i) {
            const SearchResult *current = list->current();
            out.append(*current);
            list->moveCurrent(1);
            if (list->current() == current)
                break;
        }
        return out;
    }

private Q_SLOTS:
    void scopesWithAndWithoutQuery()
    {
        QSettings settings(QSettings::NativeFormat, QSettings::UserScope, "openkaryon", "shell");
        settings.setValue("Start/layout", "search");
        settings.sync();
        Karyo::Ui::applyStyle();
        AppIndex index;
        StartMenu menu(&index, QApplication::primaryScreen(), {});
        menu.setAttribute(Qt::WA_DeleteOnClose, false);
        menu.show();
        auto *edit = menu.findChild<QLineEdit *>();
        auto *results = menu.findChild<ResultList *>();
        auto *preview = menu.findChild<PreviewCard *>();
        const auto chips = menu.findChildren<ChipButton *>();
        QVERIFY(edit && results && preview);
        QCOMPARE(chips.size(), 5);
        auto choose = [&](int scope) {
            QTest::mouseClick(chips.at(scope), Qt::LeftButton);
            QVERIFY(chips.at(scope)->isChecked());
            int checked = 0;
            for (auto *chip : chips) checked += chip->isChecked();
            QCOMPARE(checked, 1);
        };
        const SearchResult::Kind kinds[] = {SearchResult::App, SearchResult::Command,
                                            SearchResult::File, SearchResult::Setting};
        for (const QString &query : {QString(), QStringLiteral("quasar")}) {
            edit->setText(query);
            for (int scope : {1, 2, 3, 4, 2, 1, 4, 3}) {
                choose(scope);
                QTRY_VERIFY(!results->isEmpty());
                const auto found = rows(results);
                QVERIFY(!found.isEmpty());
                bool fixture = false;
                for (const auto &result : found) {
                    QCOMPARE(result.kind, kinds[scope - 1]);
                    fixture |= result.title.contains("quasar", Qt::CaseInsensitive);
                }
                QVERIFY(fixture);
                if (scope == 4) {
                    bool mainSettings = false;
                    for (const auto &result : found)
                        mainSettings |= result.app.entry.id == QLatin1String("openkaryon-settings.desktop");
                    QVERIFY(mainSettings);
                }
                QVERIFY(preview->isVisible());
                QTest::qWait(15);
                if (!qEnvironmentVariableIsEmpty("KARYO_START_QA_DIR"))
                    menu.grab().save(QDir(qEnvironmentVariable("KARYO_START_QA_DIR"))
                        .filePath(QStringLiteral("scope-%1-%2.png").arg(scope).arg(query.isEmpty() ? "empty" : "query")));
            }
            choose(0);
            QTRY_VERIFY(!results->isEmpty());
        }
        // Clearing text retains the chosen scope and returns its default list.
        choose(2);
        edit->clear();
        QVERIFY(chips.at(2)->isChecked());
        QVERIFY(results->current());
        QCOMPARE(results->current()->kind, SearchResult::Command);
        // Empty Files has a visible explanation and clears the old preview.
        QFile::remove(qEnvironmentVariable("XDG_DATA_HOME") + "/recently-used.xbel");
        choose(3);
        QVERIFY(results->isEmpty());
        QVERIFY(!preview->isVisible());
        QVERIFY(results->current() == nullptr);
        // The same filter remains usable after having no recent files.
        edit->setText("quasar");
        QTRY_VERIFY(!results->isEmpty());
        QCOMPARE(results->current()->kind, SearchResult::File);
        // Switching scope must discard asynchronous file results.
        choose(4);
        QTest::qWait(100);
        for (const auto &result : rows(results))
            QCOMPARE(result.kind, SearchResult::Setting);
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir home;
    if (!home.isValid()) return 1;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", home.filePath("data").toUtf8());
    qputenv("XDG_DATA_DIRS", home.filePath("system").toUtf8());
    qputenv("PATH", home.filePath("bin").toUtf8());
    QDir().mkpath(home.filePath("data/applications"));
    QDir().mkpath(home.filePath("bin"));
    auto write = [](const QString &path, const QByteArray &data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    if (!write(home.filePath("data/applications/quasar.desktop"),
        "[Desktop Entry]\nType=Application\nName=Quasar Application\nExec=quasar-run\nCategories=Utility;\n")
        || !write(home.filePath("data/applications/quasar-settings.desktop"),
        "[Desktop Entry]\nType=Application\nName=Quasar Display\nExec=quasar-run\nCategories=Settings;\n")
        || !write(home.filePath("data/applications/openkaryon-settings.desktop"),
        "[Desktop Entry]\nType=Application\nName=Quasar Settings\nExec=quasar-run\nCategories=Settings;System;\n")
        || !write(home.filePath("bin/quasar-run"), "#!/bin/sh\nexit 0\n")
        || !QFile::setPermissions(home.filePath("bin/quasar-run"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)
        || !write(home.filePath("quasar-notes.txt"), "fixture\n")
        || !write(home.filePath("data/recently-used.xbel"),
            QStringLiteral("<xbel><bookmark href=\"file://%1\" visited=\"2026-11-05T12:00:00Z\"/></xbel>")
                .arg(home.filePath("quasar-notes.txt")).toUtf8())) return 2;
    QApplication app(argc, argv);
    StartFilterTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "startfiltertest.moc"
