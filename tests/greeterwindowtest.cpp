// SPDX-License-Identifier: GPL-3.0-or-later
#include "greeterwindow.h"
#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <karyo/ui/password.h>
#include <memory>

class GreeterWindowTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<QLocalServer> m_server;
    std::unique_ptr<Greetd> m_transport;
    QLocalSocket *m_peer = nullptr;
    QByteArray m_buffer, m_home = qgetenv("HOME"), m_config = qgetenv("XDG_CONFIG_HOME");
    static QByteArray frame(const QJsonObject &object)
    {
        const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
        const quint32 size = quint32(payload.size());
        QByteArray bytes(sizeof size, Qt::Uninitialized);
        std::memcpy(bytes.data(), &size, sizeof size);
        return bytes + payload;
    }
    void reply(const QJsonObject &object)
    {
        m_peer->write(frame(object));
        m_peer->flush();
    }
    void success() { reply({ { "type", "success" } }); }
    void prompt(const QString &kind, const QString &text)
    {
        reply({ { "type", "auth_message" }, { "auth_message_type", kind }, { "auth_message", text } });
    }
    QJsonObject request()
    {
        if (!QTest::qWaitFor(
                [this]
                {
                    m_buffer += m_peer->readAll();
                    if (m_buffer.size() < 4)
                        return false;
                    quint32 size;
                    std::memcpy(&size, m_buffer.constData(), sizeof size);
                    return m_buffer.size() >= qsizetype(size) + 4;
                },
                2000))
            return { };
        quint32 size;
        std::memcpy(&size, m_buffer.constData(), sizeof size);
        const auto object = QJsonDocument::fromJson(m_buffer.mid(4, size)).object();
        m_buffer.remove(0, 4 + qsizetype(size));
        return object;
    }
    std::unique_ptr<GreeterWindow> window()
    {
        auto result = std::make_unique<GreeterWindow>(m_transport.get(),
            QList<GreeterUser> { { "owner", "Owner Account", "/bin/sh", 1000 },
                { "other", "Other Account", "/bin/sh", 1001 } },
            QList<GreeterSession> { { "karyo", "Karyo", { "/usr/bin/karyo-session" }, "Karyo" },
                { "alternate", "Alternate", { "/usr/bin/other-session", "literal argument" }, "Other" } },
            GreeterSelection { }, m_root->filePath("config"));
        result->resize(1280, 720);
        result->show();
        QCoreApplication::processEvents();
        return result;
    }
    static QAbstractButton *button(QWidget *widget, const QString &text)
    {
        for (auto *button : widget->findChildren<QAbstractButton *>())
            if (button->text() == text)
                return button;
        return nullptr;
    }
private Q_SLOTS:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        qputenv("HOME", m_root->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_root->filePath("config").toUtf8());
        const auto path = m_root->filePath("socket");
        m_server = std::make_unique<QLocalServer>();
        QVERIFY(m_server->listen(path));
        qputenv("GREETD_SOCK", path.toUtf8());
        m_transport = std::make_unique<Greetd>();
        QVERIFY(m_transport->connectToDaemon());
        QTRY_VERIFY(m_server->hasPendingConnections());
        m_peer = m_server->nextPendingConnection();
        QVERIFY(m_peer);
        m_buffer.clear();
    }
    void cleanup()
    {
        m_transport.reset();
        m_server.reset();
        m_root.reset();
        qunsetenv("GREETD_SOCK");
        qputenv("HOME", m_home);
        qputenv("XDG_CONFIG_HOME", m_config);
    }
    void chooserIsFrozenAndPreferencesWaitForStartAcknowledgement()
    {
        auto view = window();
        auto *other = view->findChild<QAbstractButton *>("greeterOtherUser");
        auto *session = view->findChild<QAbstractButton *>("greeterSession");
        other->click();
        auto *choice = button(view.get(), "Other Account");
        QVERIFY(choice);
        choice->click();
        session->click();
        choice = button(view.get(), "Alternate");
        QVERIFY(choice);
        choice->click();
        auto *password = view->password();
        password->lineEdit()->setText("fixture-password");
        QSignalSpy ready(view.get(), &GreeterWindow::accepted);
        password->submitted();
        QCOMPARE(request().value("username").toString(), QString("other"));
        QVERIFY(password->text().isEmpty());
        QVERIFY(password->lineEdit()->isReadOnly());
        QVERIFY(!other->isEnabled());
        QVERIFY(!session->isEnabled());
        prompt("secret", "Password:");
        QCOMPARE(request().value("response").toString(), QString("fixture-password"));
        success();
        const auto start = request();
        QCOMPARE(start.value("cmd").toArray(), QJsonArray({ "/usr/bin/other-session", "literal argument" }));
        QCOMPARE(ready.size(), 0);
        QVERIFY(!QFile::exists(m_root->filePath("config/karyo/greeter.json")));
        success();
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toString(), QString("other"));
        QCOMPARE(ready.first().at(1).toString(), QString("alternate"));
        const auto saved = readGreeterSelection(m_root->filePath("config"), m_root->path());
        QVERIFY(saved.error.isEmpty());
        QCOMPARE(saved.user, QString("other"));
        QCOMPARE(saved.session, QString("alternate"));
    }
    void visibleAndSecretPromptsAndCancelRestorePasswordInput()
    {
        auto view = window();
        auto *password = view->password();
        password->lineEdit()->setText("must-not-leak");
        password->submitted();
        request();
        prompt("visible", "Token identifier:");
        QTRY_VERIFY(!password->lineEdit()->isReadOnly());
        QCOMPARE(password->lineEdit()->echoMode(), QLineEdit::Normal);
        QVERIFY(password->text().isEmpty());
        QCOMPARE(m_peer->bytesAvailable(), 0);
        password->lineEdit()->setText("fixture-token");
        password->submitted();
        QCOMPARE(request().value("response").toString(), QString("fixture-token"));
        prompt("secret", "One-time code:");
        QTRY_VERIFY(!password->lineEdit()->isReadOnly());
        QCOMPARE(password->lineEdit()->echoMode(), QLineEdit::Password);
        auto *cancel = view->findChild<QAbstractButton *>("greeterCancel");
        QVERIFY(cancel->isEnabled());
        cancel->click();
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
        success();
        auto *auth = view->findChild<GreeterAuthentication *>();
        QTRY_COMPARE(auth->state(), GreeterAuthentication::State::Idle);
        QVERIFY(!password->lineEdit()->isReadOnly());
        QVERIFY(password->text().isEmpty());
        QCOMPARE(password->lineEdit()->echoMode(), QLineEdit::Password);
        QVERIFY(view->findChild<QAbstractButton *>("greeterOtherUser")->isEnabled());
        for (auto *label : view->findChildren<QLabel *>())
            QVERIFY(label->text() != QString("Cancelling sign-in…") || !label->isVisible());
    }
    void disconnectedServiceReportsActionableReason()
    {
        auto view = window();
        QSignalSpy failure(view.get(), &GreeterWindow::connectionFailed);
        view->password()->lineEdit()->setText("must-not-be-logged");
        m_peer->disconnectFromServer();
        QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(failure.first().at(0).toString(), QString("The sign-in service connection was lost."));
        QVERIFY(view->password()->text().isEmpty());
        QVERIFY(view->password()->lineEdit()->isReadOnly());
    }
    void missingInventoryIsActionableAndCannotAuthenticate()
    {
        GreeterWindow view(m_transport.get(), { }, { }, { "", "", "<b>Configuration failed</b>" },
            m_root->filePath("config"));
        view.resize(1280, 720);
        view.show();
        QVERIFY(view.password()->lineEdit()->isReadOnly());
        bool found = false;
        for (auto *label : view.findChildren<QLabel *>())
            if (label->text() == "<b>Configuration failed</b>")
            {
                found = true;
                QCOMPARE(label->textFormat(), Qt::PlainText);
            }
        QVERIFY(found);
        view.password()->submitted();
        QTest::qWait(10);
        QCOMPARE(m_peer->bytesAvailable(), 0);
    }
};
QTEST_MAIN(GreeterWindowTest)
#include "greeterwindowtest.moc"
