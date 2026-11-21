// SPDX-License-Identifier: GPL-3.0-or-later
#include "archivebackend.h"
#include "archivewindow.h"
#include "archivepassworddialog.h"
#include "archive7zip.h"
#include <QFileDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>
#include <archive.h>
#include <archive_entry.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QThreadPool>
#include <QTranslator>
#include <QtTest>

using namespace ArchiveBackend;
class ArchiveTest : public QObject
{
    Q_OBJECT
    bool write(const QString &path, const QByteArray &data)
    {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
    QByteArray read(const QString &path)
    {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
    }
private Q_SLOTS:
    void passwordDialogAndRetry()
    {
        ArchivePasswordDialog form("<archive>.7z", false);
        auto *input = form.findChild<QLineEdit *>("archivePassword"); QVERIFY(input);
        QCOMPARE(input->echoMode(), QLineEdit::Password);
        auto *buttons = form.findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        input->setText(" secret "); QCOMPARE(form.password(), QString(" secret "));
        form.findChild<QCheckBox *>()->setChecked(true); QCOMPARE(input->echoMode(), QLineEdit::Normal);
        form.findChild<QCheckBox *>()->setChecked(false); QCOMPARE(input->echoMode(), QLineEdit::Password);
        QTemporaryDir root; QVERIFY(write(root.filePath("file"), "private")); std::atomic_bool cancel = false;
        const QString archive = root.filePath("private.7z");
        QVERIFY(create({root.filePath("file")}, archive, Format::SevenZip, cancel, {}, "secret").ok());
        ArchiveWindow window; int prompts = 0; bool incorrect = false; QString unexpected;
        QTimer driver; driver.setInterval(10);
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
            if (auto *password = qobject_cast<ArchivePasswordDialog *>(dialog)) {
                ++prompts;
                if (prompts == 2) incorrect = password->findChild<QLabel *>("archivePasswordHint")->text().contains("Incorrect");
                password->findChild<QLineEdit *>("archivePassword")->setText(prompts == 1 ? "wrong" : "secret");
                password->accept();
            } else { unexpected = dialog->windowTitle(); dialog->reject(); }
        });
        driver.start(); window.show(); window.openArchive(archive);
        QTRY_COMPARE_WITH_TIMEOUT(window.archivePath(), archive, 15000);
        QCOMPARE(prompts, 2); QVERIFY(incorrect); QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected));
        // Canceling a different encrypted archive keeps the already open archive.
        const QString second = root.filePath("second.7z"); QVERIFY(QFile::copy(archive, second));
        disconnect(&driver, nullptr, &window, nullptr);
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) { ++prompts; dialog->reject(); }
        });
        window.openArchive(second); QTRY_COMPARE_WITH_TIMEOUT(prompts, 3, 10000);
        QCOMPARE(window.archivePath(), archive);
    }
    void creationFormRules()
    {
        ArchiveWindow window; bool checked = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
            auto *sources = dialog->findChild<QListWidget *>("archiveSources");
            auto *format = dialog->findChild<QComboBox *>("archiveFormat");
            auto *password = dialog->findChild<QLineEdit *>("newArchivePassword");
            auto *confirm = dialog->findChild<QLineEdit *>("newArchivePasswordConfirm");
            auto *names = dialog->findChild<QCheckBox *>("encryptArchiveNames");
            auto *save = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
            QVERIFY(sources && format && password && confirm && names);
            sources->addItem("fixture"); password->setText("secret"); QVERIFY(!save->isEnabled());
            confirm->setText("secret"); QVERIFY(save->isEnabled()); QVERIFY(!names->isEnabled()); QVERIFY(!names->isChecked());
            format->setCurrentIndex(2); QVERIFY(names->isEnabled()); QVERIFY(names->isChecked());
            QTest::mouseClick(names, Qt::LeftButton, Qt::NoModifier, QPoint(8, names->height() / 2));
            QVERIFY(!names->isChecked());
            password->setText("changed"); confirm->setText("changed"); QVERIFY(!names->isChecked());
            format->setCurrentIndex(0); QVERIFY(!names->isEnabled()); QVERIFY(!names->isChecked());
            format->setCurrentIndex(2); QVERIFY(names->isEnabled()); QVERIFY(!names->isChecked());
            password->clear(); confirm->clear(); QVERIFY(names->isEnabled());
            QTest::mouseClick(names, Qt::LeftButton, Qt::NoModifier, QPoint(8, names->height() / 2));
            QVERIFY(names->isChecked());
            format->setCurrentIndex(1); QVERIFY(!password->isEnabled()); QVERIFY(!confirm->isEnabled()); QVERIFY(save->isEnabled());
            checked = true; dialog->reject();
        });
        window.findChild<QAbstractButton *>("newArchive")->click(); QVERIFY(checked);
    }


    void guiCreatesProtectedArchive_data()
    {
        QTest::addColumn<int>("format"); QTest::addColumn<bool>("encryptNames");
        QTest::newRow("zip") << 0 << false;
        QTest::newRow("7z-hidden-names") << 2 << true;
        QTest::newRow("7z-visible-names") << 2 << false;
    }
    void guiCreatesProtectedArchive()
    {
        QFETCH(int, format); QFETCH(bool, encryptNames);
        QTemporaryDir root; QVERIFY(write(root.filePath("file"), "private"));
        const QString output = root.filePath(format == 0 ? "created.zip" : "created.7z");
        ArchiveWindow window; QTimer driver; driver.setInterval(10); QString unexpected;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
            if (dialog->objectName() == "createArchiveDialog") {
                dialog->findChild<QListWidget *>("archiveSources")->addItem(root.filePath("file"));
                dialog->findChild<QComboBox *>("archiveFormat")->setCurrentIndex(format);
                dialog->findChild<QLineEdit *>("newArchivePassword")->setText("fixture-secret");
                dialog->findChild<QLineEdit *>("newArchivePasswordConfirm")->setText("fixture-secret");
                auto *names = dialog->findChild<QCheckBox *>("encryptArchiveNames");
                if (names->isChecked() != encryptNames)
                    QTest::mouseClick(names, Qt::LeftButton, Qt::NoModifier, QPoint(8, names->height() / 2));
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            } else if (auto *save = qobject_cast<QFileDialog *>(dialog)) {
                save->selectFile(output);
                if (auto *name = save->findChild<QLineEdit *>("fileNameEdit")) name->setText(output);
                QMetaObject::invokeMethod(save, "accept");
            } else { unexpected = dialog->windowTitle(); dialog->reject(); }
        });
        driver.start(); window.show(); window.findChild<QAbstractButton *>("newArchive")->click();
        QTRY_COMPARE_WITH_TIMEOUT(window.archivePath(), output, 15000);
        QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected));
        std::atomic_bool cancel = false;
        auto listing = list(output, {}, cancel);
        if (format == 2 && encryptNames) QVERIFY(listing.passwordRequired);
        else QVERIFY2(listing.ok(), qPrintable(listing.error));
        auto result = extract(output, root.path(), {}, {}, cancel); QVERIFY(result.passwordRequired);
        result = extract(output, root.path(), {}, "fixture-secret", cancel);
        QVERIFY2(result.ok(), qPrintable(result.error)); QCOMPARE(read(result.output + "/file"), QByteArray("private"));
    }
    void createFormAcceptsSelectedPaths()
    {
        ArchiveWindow window; bool checked = false;
        const QStringList paths{QStringLiteral("/tmp/папка с пробелами"), QStringLiteral("/tmp/-name;literal.txt")};
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog); auto *sources = dialog->findChild<QListWidget *>("archiveSources");
            QVERIFY(sources); QCOMPARE(sources->count(), paths.size());
            for (int n = 0; n < paths.size(); ++n) QCOMPARE(sources->item(n)->text(), paths.at(n));
            auto *level = dialog->findChild<QComboBox *>("archiveCompression");
            QVERIFY(level); QCOMPARE(level->currentData().toInt(), 5);
            QCOMPARE(level->count(), 4);
            QVERIFY(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->isEnabled());
            checked = true; dialog->reject();
        });
        window.createArchive(paths); QVERIFY(checked);
    }
    void guiBrowsesAndFilters_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void guiBrowsesAndFilters()
    {
        QFETCH(bool, dark);
        Karyo::Ui::Theme::setScheme(dark ? Karyo::Ui::ColorScheme::Dark : Karyo::Ui::ColorScheme::Light);
        QTemporaryDir root;
        QVERIFY(QDir().mkpath(root.filePath("project/docs")));
        QVERIFY(write(root.filePath("project/docs/readme.txt"), "hello"));
        QVERIFY(write(root.filePath("project/source.cpp"), "int main(){}"));
        std::atomic_bool cancel = false;
        const QString path = root.filePath("project.zip");
        QVERIFY(create({root.filePath("project")}, path, Format::Zip, cancel).ok());
        ArchiveWindow window; window.show(); window.openArchive(path);
        QTRY_VERIFY(!window.busy()); QCOMPARE(window.archivePath(), path);
        auto *tree = window.findChild<QTreeWidget *>("archiveFiles");
        auto *filter = window.findChild<QLineEdit *>("archiveFilter");
        QVERIFY(tree && filter); QCOMPARE(tree->topLevelItemCount(), 1);
        auto enter = [&](QTreeWidgetItem *item) { tree->itemDoubleClicked(item, 0); };
        enter(tree->topLevelItem(0)); QCOMPARE(tree->topLevelItemCount(), 2);
        filter->setText("source"); QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("source.cpp"));
        tree->topLevelItem(0)->setSelected(true);
        QVERIFY(window.findChild<QAbstractButton *>("extractSelected")->isEnabled());
        filter->clear(); QCOMPARE(tree->topLevelItemCount(), 2);
        if (!qEnvironmentVariableIsEmpty("KARYO_ARCHIVE_QA_DIR")) {
            QTest::qWait(30);
            window.grab().save(QDir(qEnvironmentVariable("KARYO_ARCHIVE_QA_DIR"))
                .filePath(dark ? "archive-ui-dark.png" : "archive-ui-light.png"));
        }
        window.findChild<QAbstractButton *>("archiveUp")->click();
        QCOMPARE(tree->topLevelItemCount(), 1);
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.filePath("config").toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    Karyo::Ui::applyStyle();
    QTranslator translator;
    if (qEnvironmentVariableIsSet("KARYO_ARCHIVE_QA_RU")
        && translator.load(QCoreApplication::applicationDirPath() + "/karyo-archive_ru.qm"))
        app.installTranslator(&translator);
    ArchiveTest test;
    // A split regression file must not silently become an empty green QtTest.
    for (const char *signature : {"passwordDialogAndRetry()", "creationFormRules()",
                                 "guiCreatesProtectedArchive()", "createFormAcceptsSelectedPaths()",
                                 "guiBrowsesAndFilters()"}) {
        if (test.metaObject()->indexOfSlot(signature) < 0) {
            qCritical("Missing archive GUI regression slot: %s", signature);
            return 2;
        }
    }
    const int result = QTest::qExec(&test, argc, argv);
    QThreadPool::globalInstance()->waitForDone();
    return result;
}
#include "windowtest.moc"
