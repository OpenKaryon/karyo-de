// SPDX-License-Identifier: GPL-3.0-or-later
#include "greetd.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QPointer>

#include <cstring>

Greetd::Greetd(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
{
    m_socket->setReadBufferSize(1024 * 1024 + 4);
    connect(m_socket, &QLocalSocket::readyRead, this, &Greetd::read);
    connect(m_socket, &QLocalSocket::disconnected, this,
        [this]
        {
            const QPointer<Greetd> guard(this);
            if (!m_buffer.isEmpty())
                fail(tr("The sign-in service sent an incomplete message."));
            if (guard)
                Q_EMIT disconnected();
        });
}

bool Greetd::connectToDaemon()
{
    const QString path = qEnvironmentVariable("GREETD_SOCK");
    if (path.isEmpty())
        return false;
    m_buffer.clear();
    m_failed = false;
    m_socket->connectToServer(path);
    return m_socket->waitForConnected(3000);
}

void Greetd::createSession(const QString &user)
{
    send({ { QStringLiteral("type"), QStringLiteral("create_session") },
        { QStringLiteral("username"), user } });
}

void Greetd::answer(const QString &response)
{
    send({ { QStringLiteral("type"), QStringLiteral("post_auth_message_response") },
        { QStringLiteral("response"), response } });
}

void Greetd::acknowledge()
{
    send({ { QStringLiteral("type"), QStringLiteral("post_auth_message_response") },
        { QStringLiteral("response"), QJsonValue::Null } });
}

void Greetd::startSession(const QStringList &command, const QStringList &environment)
{
    send({ { QStringLiteral("type"), QStringLiteral("start_session") },
        { QStringLiteral("cmd"), QJsonArray::fromStringList(command) },
        { QStringLiteral("env"), QJsonArray::fromStringList(environment) } });
}

void Greetd::cancelSession() { send({ { QStringLiteral("type"), QStringLiteral("cancel_session") } }); }

void Greetd::send(const QJsonObject &message)
{
    if (m_socket->state() != QLocalSocket::ConnectedState)
    {
        fail(tr("The sign-in service is not connected."));
        return;
    }
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.size() > 1024 * 1024)
    {
        fail(tr("The sign-in message is too large."));
        return;
    }
    const quint32 length = quint32(payload.size());
    char header[sizeof length];
    std::memcpy(header, &length, sizeof length);
    if (m_socket->write(header, sizeof header) != sizeof header || m_socket->write(payload) != payload.size())
    {
        fail(tr("Could not send the sign-in message."));
        return;
    }
    m_socket->flush();
}

void Greetd::fail(const QString &message)
{
    if (m_failed)
        return;
    m_failed = true;
    m_buffer.clear();
    const QPointer<Greetd> guard(this);
    Q_EMIT failed(message);
    if (guard)
        m_socket->abort();
}

void Greetd::read()
{
    if (m_failed)
        return;
    m_buffer += m_socket->readAll();
    while (m_buffer.size() >= qsizetype(sizeof(quint32)))
    {
        quint32 length = 0;
        std::memcpy(&length, m_buffer.constData(), sizeof length);
        if (length == 0 || length > 1024 * 1024)
        {
            fail(tr("The sign-in service sent an invalid message length."));
            return;
        }
        const qsizetype size = qsizetype(sizeof length) + qsizetype(length);
        if (m_buffer.size() < size)
            return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(m_buffer.mid(sizeof length, length), &error);
        const auto message = document.object();
        const auto type = message.value(QStringLiteral("type")).toString();
        const auto kind = message.value(QStringLiteral("auth_message_type")).toString();
        const auto errorType = message.value(QStringLiteral("error_type")).toString();
        const bool valid = type == QLatin1String("success")
            || (type == QLatin1String("auth_message")
                && QStringList { "secret", "visible", "info", "error" }.contains(kind)
                && message.value(QStringLiteral("auth_message")).isString())
            || (type == QLatin1String("error") && QStringList { "auth_error", "error" }.contains(errorType)
                && message.value(QStringLiteral("description")).isString());
        if (error.error != QJsonParseError::NoError || !document.isObject() || !valid)
        {
            fail(tr("The sign-in service sent an invalid message."));
            return;
        }
        m_buffer.remove(0, size);
        const QPointer<Greetd> guard(this);
        Q_EMIT response(message);
        if (!guard || m_failed)
            return;
    }
}
