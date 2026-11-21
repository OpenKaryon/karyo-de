// SPDX-License-Identifier: GPL-3.0-or-later
#include "profilecontrol.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <utility>

namespace
{
constexpr qsizetype ResponseLimit = 256 * 1024;

bool accountName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_.-]*\\$?\\z"));
    return name.size() <= 32 && pattern.match(name).hasMatch();
}

bool virtualTerminal(const QJsonValue &value)
{
    return value.isDouble() && value.toDouble() == value.toInt() && value.toInt() >= 1 && value.toInt() <= 63;
}
}

ProfileCommand::ProfileCommand(Operation operation, const QString &program, const QStringList &arguments,
    QByteArray input, int timeout, bool busy, QObject *parent)
    : QObject(parent)
    , m_operation(operation)
    , m_program(program)
    , m_arguments(arguments)
    , m_input(std::move(input))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this,
        [this]
        {
            m_interrupted = true;
            clearInput();
            m_process.kill();
        });
    connect(&m_process, &QProcess::started, this,
        [this]
        {
            if (!m_input.isEmpty())
                m_process.write(m_input);
            clearInput();
            m_process.closeWriteChannel();
        });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &ProfileCommand::consume);
    connect(&m_process, &QProcess::readyReadStandardError, this, &ProfileCommand::consume);
    connect(&m_process, &QProcess::errorOccurred, this,
        [this](QProcess::ProcessError error)
        {
            if (error == QProcess::FailedToStart)
                finish(Failed);
        });
    connect(&m_process, &QProcess::finished, this,
        [this](int code, QProcess::ExitStatus status)
        {
            consume();
            if (m_interrupted || status != QProcess::NormalExit)
                finish(m_operation == List ? Failed : Uncertain);
            else if (code == 0)
            {
                m_response = QJsonDocument::fromJson(m_output);
                finish(validResponse() ? Success : (m_operation == List ? Failed : Uncertain));
            }
            else if (code == 3 && m_operation == Switch)
                finish(Stopped);
            else if (code == 2 && m_operation == Add)
                finish(Retained);
            else
                finish(Failed);
        });
    // Let the caller connect its result before even an immediate start failure.
    QTimer::singleShot(0, this,
        [this, timeout, busy]
        {
            if (busy)
            {
                finish(Busy);
                return;
            }
            m_timer.start(timeout);
            m_process.start(m_program, m_arguments);
        });
}

void ProfileCommand::clearInput()
{
    m_input.fill('\0');
    m_input.clear();
}

void ProfileCommand::consume()
{
    const auto output = m_process.readAllStandardOutput();
    const auto error = m_process.readAllStandardError();
    m_received += output.size() + error.size();
    if (m_received > ResponseLimit)
    {
        m_interrupted = true;
        m_output.clear();
        m_process.kill();
    }
    else if (!m_interrupted)
        m_output.append(output);
    // Never retain or display arbitrary native diagnostics or password input.
}

bool ProfileCommand::validResponse() const
{
    if (m_operation != List)
    {
        const auto object = m_response.object();
        const auto name = object.value(QStringLiteral("user")).toString();
        if (!m_response.isObject() || !accountName(name))
            return false;
        return m_operation == Add
            || (name == m_arguments.value(1) && virtualTerminal(object.value(QStringLiteral("vt")))
                && object.value(QStringLiteral("changed")).isBool());
    }
    if (!m_response.isArray() || m_response.array().isEmpty())
        return false;
    QSet<QString> names;
    int current = 0;
    for (const auto &value : m_response.array())
    {
        const auto object = value.toObject();
        const auto name = object.value(QStringLiteral("user")).toString();
        const auto label = object.value(QStringLiteral("label")).toString();
        if (!value.isObject() || !accountName(name) || names.contains(name) || label.trimmed().isEmpty()
            || label.size() > 160 || label.contains(QChar::Null))
            return false;
        for (const auto *key : { "running", "active", "current" })
        {
            if (!object.value(QLatin1String(key)).isBool())
                return false;
        }
        const bool running = object.value(QStringLiteral("running")).toBool();
        if (running ? !virtualTerminal(object.value(QStringLiteral("vt")))
                    : !object.value(QStringLiteral("vt")).isNull())
            return false;
        names.insert(name);
        current += object.value(QStringLiteral("current")).toBool();
    }
    return current == 1;
}

void ProfileCommand::finish(Result result)
{
    if (m_done)
        return;
    m_done = true;
    m_timer.stop();
    clearInput();
    m_result = result;
    Q_EMIT finished(result);
    deleteLater();
}

QString ProfileCommand::message() const
{
    switch (m_result)
    {
    case Success:
        return { };
    case Stopped:
        return tr("This desktop is not running yet.");
    case Retained:
        return tr("Desktop setup needs administrator review. Account data was retained.");
    case Uncertain:
        return tr("The operation did not finish. Check the desktop list before trying again.");
    case Busy:
        return tr("Another desktop operation is still running.");
    case Failed:
        return m_operation == List ? tr("Could not load desktops. Check the profile service and try again.")
                                   : tr("The desktop operation failed or was not authorized.");
    }
    return { };
}

ProfileControl::ProfileControl(QObject *parent, QString program, int timeout)
    : QObject(parent)
    , m_program(std::move(program))
    , m_timeout(timeout)
{
}

bool ProfileControl::available() const { return !QStandardPaths::findExecutable(m_program).isEmpty(); }

ProfileCommand *ProfileControl::run(
    ProfileCommand::Operation operation, const QStringList &arguments, QByteArray input)
{
    auto *job = new ProfileCommand(operation, m_program, arguments, std::move(input),
        operation == ProfileCommand::List ? qMin(m_timeout, 8000) : m_timeout,
        operation != ProfileCommand::List && changing(), this);
    if (operation != ProfileCommand::List && !changing())
    {
        m_change = job;
        connect(job, &ProfileCommand::finished, this, [this] { m_change.clear(); });
    }
    return job;
}

ProfileCommand *ProfileControl::list()
{
    if (m_list)
        return m_list;
    m_list = run(ProfileCommand::List, { QStringLiteral("list"), QStringLiteral("--json") });
    connect(m_list, &ProfileCommand::finished, this, [this] { m_list.clear(); });
    return m_list;
}

ProfileCommand *ProfileControl::switchTo(const QString &user)
{
    return run(ProfileCommand::Switch, { QStringLiteral("switch"), user });
}

ProfileCommand *ProfileControl::add(const QString &label, const QString &protection, QByteArray password)
{
    QStringList args { QStringLiteral("add"), QStringLiteral("--label=") + label,
        QStringLiteral("--protection=") + protection };
    if (protection == QLatin1String("separate"))
    {
        args.append(QStringLiteral("--password-stdin"));
        password.append('\n');
    }
    else
        password.clear();
    return run(ProfileCommand::Add, args, std::move(password));
}
