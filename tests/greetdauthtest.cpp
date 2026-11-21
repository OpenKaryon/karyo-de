// SPDX-License-Identifier: GPL-3.0-or-later
#include "authentication.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <memory>

class GreetdAuthenticationTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<QLocalServer> m_server;
    std::unique_ptr<Greetd> m_transport;
    std::unique_ptr<GreeterAuthentication> m_auth;
    QLocalSocket *m_peer = nullptr;
    QByteArray m_buffer;
    using State = GreeterAuthentication::State;
    static QByteArray frame(const QJsonObject &object)
    {
        const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
        const quint32 size = quint32(payload.size());
        QByteArray header(sizeof size, Qt::Uninitialized);
        std::memcpy(header.data(), &size, sizeof size);
        return header + payload;
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
    void error()
    {
        reply({ { "type", "error" }, { "error_type", "auth_error" },
            { "description", "Public fixture denial" } });
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
        const auto result = QJsonDocument::fromJson(m_buffer.mid(4, size)).object();
        m_buffer.remove(0, 4 + qsizetype(size));
        return result;
    }
    bool begin(const QString &secret = "fixture-password")
    {
        return m_auth->begin("fixture", { "/usr/bin/karyo-session", "literal argument" },
            { "XDG_SESSION_TYPE=wayland", "XDG_CURRENT_DESKTOP=Karyo" }, secret);
    }
private Q_SLOTS:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        m_server = std::make_unique<QLocalServer>();
        const auto path = m_root->filePath("socket");
        QVERIFY(m_server->listen(path));
        qputenv("GREETD_SOCK", path.toUtf8());
        m_transport = std::make_unique<Greetd>();
        QVERIFY(m_transport->connectToDaemon());
        QTRY_VERIFY(m_server->hasPendingConnections());
        m_peer = m_server->nextPendingConnection();
        QVERIFY(m_peer);
        m_auth = std::make_unique<GreeterAuthentication>(m_transport.get());
        m_buffer.clear();
    }
    void cleanup()
    {
        m_auth.reset();
        m_transport.reset();
        m_server.reset();
        m_root.reset();
        qunsetenv("GREETD_SOCK");
    }
    void onlyAuthenticatedChosenSessionStarts()
    {
        QSignalSpy ready(m_auth.get(), &GreeterAuthentication::sessionReady);
        QVERIFY(begin());
        auto object = request();
        QCOMPARE(object.value("type").toString(), QString("create_session"));
        QCOMPARE(object.value("username").toString(), QString("fixture"));
        QVERIFY(!m_auth->begin("other", { "/bin/false" }, { }, "other-secret"));
        prompt("secret", "Password:");
        object = request();
        QCOMPARE(object.value("response").toString(), QString("fixture-password"));
        QCOMPARE(m_auth->state(), State::Authenticating);
        QCOMPARE(ready.size(), 0);
        success();
        object = request();
        QCOMPARE(object.value("type").toString(), QString("start_session"));
        QCOMPARE(object.value("cmd").toArray(), QJsonArray({ "/usr/bin/karyo-session", "literal argument" }));
        QCOMPARE(object.value("env").toArray(),
            QJsonArray({ "XDG_SESSION_TYPE=wayland", "XDG_CURRENT_DESKTOP=Karyo" }));
        QCOMPARE(m_auth->state(), State::Starting);
        QCOMPARE(ready.size(), 0);
        QVERIFY(!m_auth->cancel());
        success();
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(m_auth->state(), State::Finished);
        QCOMPARE(ready.first().first().toString(), QString("fixture"));
        QVERIFY(!begin());
    }
    void visibleAndRepeatedSecretQuestions()
    {
        QSignalSpy prompts(m_auth.get(), &GreeterAuthentication::promptRequested);
        QVERIFY(begin("must-not-leak-to-visible-prompt"));
        QCOMPARE(request().value("type").toString(), QString("create_session"));
        prompt("info", "Welcome");
        QVERIFY(request().value("response").isNull());
        prompt("visible", "Token identifier:");
        QTRY_COMPARE(prompts.size(), 1);
        QCOMPARE(m_auth->state(), State::Prompt);
        QCOMPARE(prompts.first().at(1).toBool(), false);
        QTest::qWait(10);
        QCOMPARE(m_peer->bytesAvailable(), 0);
        QVERIFY(!m_auth->answer(QString(QChar::Null)));
        QCOMPARE(m_auth->state(), State::Prompt);
        QVERIFY(m_auth->answer("fixture-token"));
        QCOMPARE(request().value("response").toString(), QString("fixture-token"));
        prompt("secret", "One-time code:");
        QTRY_COMPARE(prompts.size(), 2);
        QVERIFY(prompts.last().at(1).toBool());
        QVERIFY(m_auth->answer("123456"));
        QCOMPARE(request().value("response").toString(), QString("123456"));
        prompt("error", "Additional information");
        QVERIFY(request().value("response").isNull());
        prompt("secret", "New password:");
        QTRY_COMPARE(prompts.size(), 3);
        QVERIFY(m_auth->answer(""));
        QCOMPARE(request().value("response").toString(), QString());
    }
    void wrongPasswordCancelsBeforeRetry()
    {
        QSignalSpy messages(m_auth.get(), &GreeterAuthentication::message);
        QSignalSpy ready(m_auth.get(), &GreeterAuthentication::sessionReady);
        QVERIFY(begin());
        request();
        prompt("secret", "Password:");
        request();
        error();
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
        QCOMPARE(m_auth->state(), State::Cancelling);
        QCOMPARE(messages.size(), 1);
        QVERIFY(messages.first().at(1).toBool());
        QVERIFY(!begin("another"));
        success();
        QTRY_COMPARE(m_auth->state(), State::Idle);
        QVERIFY(begin("next-attempt"));
        request();
        prompt("secret", "Password:");
        QCOMPARE(request().value("response").toString(), QString("next-attempt"));
        QCOMPARE(ready.size(), 0);
    }
    void deniedWorkerCleanupRetriesOnceBeforeReuse()
    {
        QSignalSpy ready(m_auth.get(), &GreeterAuthentication::sessionReady);
        QVERIFY(begin("first-secret"));
        request();
        prompt("secret", "Password:");
        request();
        error();
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
        reply({ { "type", "error" }, { "error_type", "error" },
            { "description", "unable to send message: Connection refused (os error 111)" } });
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
        QCOMPARE(m_auth->state(), State::Cancelling);
        QVERIFY(!begin("must-not-start"));
        success();
        QTRY_COMPARE(m_auth->state(), State::Idle);
        QCOMPARE(ready.size(), 0);
        QVERIFY(begin("new-secret"));
        QCOMPARE(request().value("type").toString(), QString("create_session"));
        prompt("secret", "Password:");
        QCOMPARE(request().value("response").toString(), QString("new-secret"));
    }
    void deniedCleanupCannotRetryIndefinitely()
    {
        QVERIFY(begin());
        request();
        error();
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
        error();
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
        error();
        QTRY_COMPARE(m_auth->state(), State::Failed);
        QVERIFY(!begin());
        QCOMPARE(m_peer->bytesAvailable(), 0);
    }
    void cancelQueuedDuringAuthenticationNeverStarts()
    {
        QVERIFY(begin());
        request();
        QVERIFY(m_auth->cancel());
        QCOMPARE(m_auth->state(), State::Authenticating);
        success(); // A successful in-flight authentication must still cancel.
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
        QCOMPARE(m_auth->state(), State::Cancelling);
        success();
        QTRY_COMPARE(m_auth->state(), State::Idle);
        QVERIFY(begin());
        request();
        prompt("visible", "Code:");
        QTRY_COMPARE(m_auth->state(), State::Prompt);
        QVERIFY(m_auth->cancel());
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
    }
    void cancelledProtocolFailureIsFatal()
    {
        QVERIFY(begin());
        request();
        m_auth->cancel();
        prompt("secret", "Password:");
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
        error();
        QTRY_COMPARE(m_auth->state(), State::Failed);
        QVERIFY(!begin());
        QVERIFY(!m_auth->answer("not-sent"));
    }
    void disconnectAndUnsolicitedSuccessNeverAuthenticate()
    {
        QSignalSpy ready(m_auth.get(), &GreeterAuthentication::sessionReady);
        success();
        QTRY_COMPARE(m_auth->state(), State::Failed);
        QCOMPARE(ready.size(), 0);
        QVERIFY(!begin());
    }
    void disconnectDuringAuthenticationIsFatal()
    {
        QSignalSpy ready(m_auth.get(), &GreeterAuthentication::sessionReady);
        QVERIFY(begin());
        request();
        m_peer->disconnectFromServer();
        QTRY_COMPARE(m_auth->state(), State::Failed);
        QCOMPARE(ready.size(), 0);
        QVERIFY(!begin());
    }
    void disconnectedBeforeControllerCreationIsFatal()
    {
        m_auth.reset();
        m_peer->disconnectFromServer();
        QTRY_VERIFY(!m_transport->isConnected());
        m_auth = std::make_unique<GreeterAuthentication>(m_transport.get());
        QVERIFY(!begin());
        QCOMPARE(m_auth->state(), State::Failed);
    }
    void invalidSessionDoesNotContactDaemon()
    {
        QVERIFY(!m_auth->begin("", { "/usr/bin/karyo-session" }, { }, "fixture"));
        QVERIFY(!m_auth->begin("fixture", { "/usr/bin/karyo-session" }, { }, QString(QChar::Null)));
        QVERIFY(!m_auth->begin("fixture", { }, { }, "fixture"));
        QVERIFY(!m_auth->begin("fixture", { QString(QChar::Null) }, { }, "fixture"));
        QCOMPARE(m_auth->state(), State::Idle);
        QTest::qWait(10);
        QCOMPARE(m_peer->bytesAvailable(), 0);
    }
};
QTEST_GUILESS_MAIN(GreetdAuthenticationTest)
#include "greetdauthtest.moc"
