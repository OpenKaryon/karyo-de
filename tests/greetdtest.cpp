// SPDX-License-Identifier: GPL-3.0-or-later
#include "greetd.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <memory>
class GreetdTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<QLocalServer> m_server;
    std::unique_ptr<Greetd> m_client;
    QLocalSocket *m_peer = nullptr;
    QByteArray m_requests;
    static QByteArray frame(const QByteArray &payload)
    {
        const quint32 length = quint32(payload.size());
        QByteArray data(sizeof length, Qt::Uninitialized);
        std::memcpy(data.data(), &length, sizeof length);
        return data + payload;
    }
    void send(const QByteArray &payload)
    {
        m_peer->write(frame(payload));
        m_peer->flush();
    }
    QJsonObject request()
    {
        const bool complete = QTest::qWaitFor(
            [this]
            {
                m_requests += m_peer->readAll();
                if (m_requests.size() < 4)
                    return false;
                quint32 length;
                std::memcpy(&length, m_requests.constData(), sizeof length);
                return m_requests.size() >= qsizetype(length) + 4;
            },
            2000);
        if (!complete)
            return { };
        quint32 length;
        std::memcpy(&length, m_requests.constData(), sizeof length);
        auto result = QJsonDocument::fromJson(m_requests.mid(4, length)).object();
        m_requests.remove(0, 4 + qsizetype(length));
        return result;
    }
private Q_SLOTS:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        m_server = std::make_unique<QLocalServer>();
        const QString path = m_root->filePath("socket");
        QVERIFY(m_server->listen(path));
        qputenv("GREETD_SOCK", path.toUtf8());
        m_client = std::make_unique<Greetd>();
        QVERIFY(m_client->connectToDaemon());
        QTRY_VERIFY(m_server->hasPendingConnections());
        m_peer = m_server->nextPendingConnection();
        QVERIFY(m_peer);
        m_requests.clear();
    }
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
        m_root.reset();
        qunsetenv("GREETD_SOCK");
    }
    void requestsPreserveLiteralFields()
    {
        m_client->createSession("fixture-user");
        auto object = request();
        QCOMPARE(object.value("type").toString(), QString("create_session"));
        QCOMPARE(object.value("username").toString(), QString("fixture-user"));
        const QString secret = QString::fromUtf8("fixture only: пароль '\"\\\n");
        m_client->answer(secret);
        object = request();
        QCOMPARE(object.value("type").toString(), QString("post_auth_message_response"));
        QCOMPARE(object.value("response").toString(), secret);
        m_client->acknowledge();
        QVERIFY(request().value("response").isNull());
        const QStringList argv { "/usr/bin/karyo-session", "literal argument; $(false)" };
        const QStringList env { "XDG_SESSION_TYPE=wayland", "LANG=ru_RU.UTF-8" };
        m_client->startSession(argv, env);
        object = request();
        QCOMPARE(object.value("cmd").toArray(), QJsonArray::fromStringList(argv));
        QCOMPARE(object.value("env").toArray(), QJsonArray::fromStringList(env));
        m_client->cancelSession();
        QCOMPARE(request().value("type").toString(), QString("cancel_session"));
    }
    void fragmentedAndCoalescedReplies()
    {
        QSignalSpy responses(m_client.get(), &Greetd::response);
        const auto data = frame(
            "{\"type\":\"auth_message\",\"auth_message_type\":\"secret\",\"auth_message\":\"Password:\"}");
        m_peer->write(data.left(2));
        m_peer->flush();
        QTest::qWait(10);
        QCOMPARE(responses.size(), 0);
        m_peer->write(data.mid(2, 8));
        m_peer->flush();
        QTest::qWait(10);
        QCOMPARE(responses.size(), 0);
        m_peer->write(data.mid(10) + frame("{\"type\":\"success\"}")
            + frame("{\"type\":\"error\",\"error_type\":\"auth_error\",\"description\":\"Try again\"}"));
        m_peer->flush();
        QTRY_COMPARE(responses.size(), 3);
        QCOMPARE(
            responses.at(0).at(0).toJsonObject().value("auth_message_type").toString(), QString("secret"));
        QCOMPARE(responses.at(1).at(0).toJsonObject().value("type").toString(), QString("success"));
        QCOMPARE(responses.at(2).at(0).toJsonObject().value("error_type").toString(), QString("auth_error"));
    }
    void malformedMessages_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::newRow("zero") << QByteArray(4, '\0');
        quint32 huge = 0xffffffff;
        QTest::newRow("overflow") << QByteArray(reinterpret_cast<const char *>(&huge), sizeof huge);
        QTest::newRow("invalid-json") << frame("fixture-secret");
        QTest::newRow("array") << frame("[]");
        QTest::newRow("unknown-type") << frame("{\"type\":\"unknown\"}");
        QTest::newRow("invalid-prompt")
            << frame("{\"type\":\"auth_message\",\"auth_message_type\":\"other\"}");
        QTest::newRow("invalid-error") << frame("{\"type\":\"error\",\"error_type\":\"auth_error\"}");
    }
    void malformedMessages()
    {
        QFETCH(QByteArray, data);
        QSignalSpy responses(m_client.get(), &Greetd::response), failures(m_client.get(), &Greetd::failed);
        m_peer->write(data);
        m_peer->flush();
        QTRY_COMPARE(failures.size(), 1);
        QCOMPARE(responses.size(), 0);
        QVERIFY(!failures.first().at(0).toString().contains("fixture-secret"));
        QTRY_COMPARE(m_peer->state(), QLocalSocket::UnconnectedState);
        m_client->answer("fixture-secret");
        QCOMPARE(failures.size(), 1);
    }
    void receiverMayDestroyTransport()
    {
        connect(m_client.get(), &Greetd::response, this, [this] { m_client.reset(); });
        m_peer->write(frame("{\"type\":\"success\"}") + frame("{\"type\":\"success\"}"));
        m_peer->flush();
        QTRY_VERIFY(!m_client);
    }
    void failureReceiverMayDestroyTransport()
    {
        connect(m_client.get(), &Greetd::failed, this, [this] { m_client.reset(); });
        m_peer->write(QByteArray(4, '\0'));
        m_peer->flush();
        QTRY_VERIFY(!m_client);
    }
    void disconnectedPartialMessageIsAnError()
    {
        QSignalSpy responses(m_client.get(), &Greetd::response), failures(m_client.get(), &Greetd::failed);
        m_peer->write(frame("{\"type\":\"success\"}").left(6));
        m_peer->flush();
        m_peer->disconnectFromServer();
        QTRY_COMPARE(failures.size(), 1);
        QCOMPARE(responses.size(), 0);
    }
};
QTEST_GUILESS_MAIN(GreetdTest)
#include "greetdtest.moc"
