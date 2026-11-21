// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonDocument>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

class ProfileControl;

// One asynchronous public-CLI call. The producer owns it, never a transient
// panel/dialog: switching outputs must not kill a privileged account operation.
class ProfileCommand : public QObject
{
    Q_OBJECT
public:
    enum Operation
    {
        List,
        Switch,
        Add
    };
    enum Result
    {
        Success,
        Failed,
        Stopped,
        Retained,
        Uncertain,
        Busy
    };
    Q_ENUM(Result)
    QString message() const;
    QJsonDocument response() const { return m_response; }
    Result result() const { return m_result; }

Q_SIGNALS:
    void finished(ProfileCommand::Result result);

private:
    friend class ProfileControl;
    ProfileCommand(Operation operation, const QString &program, const QStringList &arguments,
        QByteArray input, int timeout, bool busy, QObject *parent);
    void consume();
    void finish(Result result);
    bool validResponse() const;
    void clearInput();
    Operation m_operation;
    QString m_program;
    QStringList m_arguments;
    QByteArray m_input;
    QByteArray m_output;
    qsizetype m_received = 0;
    QJsonDocument m_response;
    QProcess m_process;
    QTimer m_timer;
    Result m_result = Failed;
    bool m_done = false;
    bool m_interrupted = false;
};

class ProfileControl : public QObject
{
    Q_OBJECT
public:
    explicit ProfileControl(
        QObject *parent = nullptr, QString program = QStringLiteral("karyo-desktops"), int timeout = 180000);
    bool available() const;
    bool changing() const { return !m_change.isNull(); }
    ProfileCommand *list();
    ProfileCommand *switchTo(const QString &user);
    ProfileCommand *add(const QString &label, const QString &protection, QByteArray password);

private:
    ProfileCommand *run(
        ProfileCommand::Operation operation, const QStringList &arguments, QByteArray input = { });
    QString m_program;
    int m_timeout;
    QPointer<ProfileCommand> m_list;
    QPointer<ProfileCommand> m_change;
};
