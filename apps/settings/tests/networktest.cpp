// SPDX-License-Identifier: GPL-3.0-or-later
#include "pages.h"
#include "widgets.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

class NetworkTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_path = qgetenv("PATH");
    static void put(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), data.size());
    }
    QPushButton *button(SettingsPage *page, const QString &title)
    {
        for (auto *row : page->findChildren<SettingRow *>())
            if (row->property("settingTitle").toString() == title)
                return row->findChild<QPushButton *>();
        return nullptr;
    }
    QList<QStringList> calls()
    {
        QFile f(m_root->filePath("calls"));
        if (!f.open(QIODevice::ReadOnly))
            return { };
        QList<QStringList> result;
        for (const auto &line : f.readAll().split('\n'))
            if (!line.isEmpty())
                result.append(QJsonDocument::fromJson(line).toVariant().toStringList());
        return result;
    }
private Q_SLOTS:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        qputenv("QA_NM", m_root->path().toUtf8());
        qputenv("PATH", m_root->filePath("bin").toUtf8() + ':' + m_path);
        put(m_root->filePath("radio"), "enabled");
        const QString fixture = m_root->filePath("bin/nmcli");
        QFile fixtureSource(QFINDTESTDATA("fixtures/nmcli.py"));
        QVERIFY(fixtureSource.open(QIODevice::ReadOnly));
        put(fixture, fixtureSource.readAll());
        QVERIFY(QFile::setPermissions(fixture, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }
    void cleanup()
    {
        qputenv("PATH", m_path);
        qunsetenv("QA_NM");
    }
    void savedConnectionFailureAndRetryUseUuid()
    {
        std::unique_ptr<SettingsPage> page(createNetworkPage(nullptr));
        auto *power = page->findChild<ToggleSwitch *>("wifiPower");
        QTRY_VERIFY(power->isEnabled());
        auto *connect = button(page.get(), "Office:East");
        QVERIFY(connect);
        put(m_root->filePath("deny"), "yes");
        connect->click();
        QVERIFY(!power->isEnabled());
        page->refresh(); // Must not start parallel refresh during activation.
        auto *status = page->findChild<QLabel *>("networkStatus");
        QTRY_VERIFY(status->text().contains("Connection denied"));
        QTRY_VERIFY(power->isEnabled());
        const QStringList expected { "--wait", "45", "connection", "up", "uuid",
            "12345678-1234-1234-1234-123456789012" };
        QCOMPARE(calls().count(expected), 1);
        QVERIFY(QFile::remove(m_root->filePath("deny")));
        button(page.get(), "Office:East")->click();
        QTRY_VERIFY(status->text().contains("Connection updated"));
        QTRY_VERIFY(power->isEnabled());
        QCOMPARE(calls().count(expected), 2);
    }
    void radioDenialRestoresAuthoritativeState()
    {
        std::unique_ptr<SettingsPage> page(createNetworkPage(nullptr));
        auto *power = page->findChild<ToggleSwitch *>("wifiPower");
        QTRY_VERIFY(power->isEnabled());
        QVERIFY(power->isChecked());
        put(m_root->filePath("deny"), "yes");
        power->click();
        QTRY_VERIFY(page->findChild<QLabel *>("networkStatus")->text().contains("Radio denied"));
        QTRY_VERIFY(power->isEnabled());
        QVERIFY(power->isChecked());
        QVERIFY(QFile::remove(m_root->filePath("deny")));
        power->click();
        QTRY_VERIFY(power->isEnabled());
        QVERIFY(!power->isChecked());
    }
    void newSecuredNetworkValidatesPasswordBeforeActivation()
    {
        std::unique_ptr<SettingsPage> page(createNetworkPage(nullptr));
        QTRY_VERIFY(page->findChild<ToggleSwitch *>("wifiPower")->isEnabled());
        QTRY_VERIFY(button(page.get(), "Guest"));
        bool validated = false;
        QTimer::singleShot(0, page.get(),
            [&]
            {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                    return;
                auto *field = dialog->findChild<QLineEdit *>();
                auto *box = dialog->findChild<QDialogButtonBox *>();
                auto *show = dialog->findChild<QCheckBox *>();
                if (field && box && show)
                {
                    field->setText("short");
                    const bool shortRejected = !box->button(QDialogButtonBox::Ok)->isEnabled();
                    field->setText("validpass");
                    show->setChecked(true);
                    const bool visible = field->echoMode() == QLineEdit::Normal;
                    show->setChecked(false);
                    validated = shortRejected && box->button(QDialogButtonBox::Ok)->isEnabled() && visible
                        && field->echoMode() == QLineEdit::Password;
                    box->button(QDialogButtonBox::Cancel)->click();
                }
                else
                    dialog->reject();
            });
        button(page.get(), "Guest")->click();
        QVERIFY(validated);
        for (const auto &call : calls())
            QVERIFY(!call.contains("validpass"));
    }
    void malformedRadioStateCannotBecomeAnActionableFalseState()
    {
        put(m_root->filePath("radio"), "unexpected");
        std::unique_ptr<SettingsPage> page(createNetworkPage(nullptr));
        QTRY_VERIFY(page->findChild<QLabel *>("networkStatus")->text().contains("invalid Wi-Fi state"));
        QVERIFY(!page->findChild<ToggleSwitch *>("wifiPower")->isEnabled());
    }
    void unavailableServiceShowsError()
    {
        put(m_root->filePath("offline"), "yes");
        std::unique_ptr<SettingsPage> page(createNetworkPage(nullptr));
        QTRY_VERIFY(page->findChild<QLabel *>("networkStatus")->text().contains("unavailable"));
        QVERIFY(!page->findChild<ToggleSwitch *>("wifiPower")->isEnabled());
    }
};
QTEST_MAIN(NetworkTest)
#include "networktest.moc"
