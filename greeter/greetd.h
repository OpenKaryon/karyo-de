// SPDX-License-Identifier: GPL-3.0-or-later
// The greetd IPC (greetd-ipc(7)): JSON messages with a native-endian
// 32-bit length over the socket in $GREETD_SOCK.
#pragma once

#include <QJsonObject>
#include <QObject>

class QLocalSocket;

class Greetd : public QObject
{
    Q_OBJECT
public:
    explicit Greetd(QObject *parent = nullptr);

    bool connectToDaemon();
    bool isConnected() const;
    void createSession(const QString &user);
    void answer(const QString &response);
    // Acknowledges an info or error message without a response.
    void acknowledge();
    void startSession(const QStringList &command, const QStringList &environment);
    void cancelSession();

Q_SIGNALS:
    // {"type": "success" | "error" | "auth_message", ...}
    void response(const QJsonObject &message);
    void disconnected();
    void failed(const QString &message);

private:
    void send(const QJsonObject &message);
    void read();
    void fail(const QString &message);

    QLocalSocket *m_socket;
    QByteArray m_buffer;
    bool m_failed = false;
};
