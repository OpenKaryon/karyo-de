// SPDX-License-Identifier: GPL-3.0-or-later
#include "timers.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {

constexpr qint64 Minute = 60 * 1000;

const char *stateName(TimerModel::State state)
{
    switch (state) {
    case TimerModel::State::Running:
        return "running";
    case TimerModel::State::Paused:
        return "paused";
    case TimerModel::State::Done:
        return "done";
    case TimerModel::State::Ready:
        break;
    }
    return "ready";
}

TimerModel::State stateFromName(const QString &name)
{
    if (name == QLatin1String("running"))
        return TimerModel::State::Running;
    if (name == QLatin1String("paused"))
        return TimerModel::State::Paused;
    if (name == QLatin1String("done"))
        return TimerModel::State::Done;
    return TimerModel::State::Ready;
}

} // namespace

TimerModel::TimerModel(const QString &path, std::function<qint64()> now, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_now(now ? std::move(now) : [] { return QDateTime::currentMSecsSinceEpoch(); })
{
    load();
}

QString TimerModel::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/openkaryon/clock-timers.json");
}

void TimerModel::addSamples()
{
    m_timers.append({QUuid::createUuid().toString(QUuid::WithoutBraces), tr("Centrifuge"), 10 * Minute});
    m_timers.append({QUuid::createUuid().toString(QUuid::WithoutBraces), tr("Gel run"), 45 * Minute});
    m_timers.append({QUuid::createUuid().toString(QUuid::WithoutBraces), tr("Incubation"), 60 * Minute});
    touch();
}

int TimerModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_timers.size(); ++i) {
        if (m_timers.at(i).id == id)
            return i;
    }
    return -1;
}

qint64 TimerModel::remaining(int index) const
{
    const Timer &t = m_timers.at(index);
    switch (t.state) {
    case State::Ready:
        return t.duration;
    case State::Running:
        return qMax<qint64>(0, t.deadline - m_now());
    case State::Paused:
        return t.left;
    case State::Done:
        break;
    }
    return 0;
}

bool TimerModel::anyRunning() const
{
    for (const Timer &t : m_timers) {
        if (t.state == State::Running)
            return true;
    }
    return false;
}

int TimerModel::add(const QString &name, qint64 duration)
{
    m_timers.append({QUuid::createUuid().toString(QUuid::WithoutBraces), name, qMax<qint64>(1000, duration)});
    touch();
    return int(m_timers.size()) - 1;
}

void TimerModel::remove(int index)
{
    m_timers.removeAt(index);
    touch();
}

void TimerModel::edit(int index, const QString &name, qint64 duration)
{
    Timer &t = m_timers[index];
    t.name = name;
    if (duration != t.duration) {
        // A new length starts over.
        t.duration = qMax<qint64>(1000, duration);
        t.state = State::Ready;
    }
    touch();
}

void TimerModel::start(int index)
{
    Timer &t = m_timers[index];
    const qint64 now = m_now();
    if (t.state == State::Running)
        return;
    t.deadline = now + (t.state == State::Paused ? t.left : t.duration);
    t.state = State::Running;
    touch();
}

void TimerModel::pause(int index)
{
    Timer &t = m_timers[index];
    if (t.state != State::Running)
        return;
    t.left = qMax<qint64>(0, t.deadline - m_now());
    t.state = State::Paused;
    touch();
}

void TimerModel::reset(int index)
{
    Timer &t = m_timers[index];
    t.state = State::Ready;
    t.left = 0;
    touch();
}

void TimerModel::addTime(int index, qint64 ms)
{
    Timer &t = m_timers[index];
    switch (t.state) {
    case State::Running:
        t.deadline += ms;
        break;
    case State::Paused:
        t.left += ms;
        break;
    case State::Ready:
        t.duration += ms;
        break;
    case State::Done:
        // A minute more after the end: it runs that minute.
        t.deadline = m_now() + ms;
        t.state = State::Running;
        break;
    }
    touch();
}

void TimerModel::tick()
{
    const qint64 now = m_now();
    QStringList finished;
    for (Timer &t : m_timers) {
        if (t.state == State::Running && t.deadline <= now) {
            t.state = State::Done;
            t.finishedAt = t.deadline;
            finished.append(t.id);
        }
    }
    if (finished.isEmpty())
        return;
    touch();
    for (const QString &id : std::as_const(finished))
        Q_EMIT this->finished(id);
}

void TimerModel::touch()
{
    save();
    Q_EMIT changed();
}

void TimerModel::load()
{
    if (m_path.isEmpty())
        return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray list = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : list) {
        const QJsonObject o = value.toObject();
        Timer t;
        t.id = o.value(QStringLiteral("id")).toString();
        t.name = o.value(QStringLiteral("name")).toString();
        t.duration = qint64(o.value(QStringLiteral("duration")).toDouble());
        t.state = stateFromName(o.value(QStringLiteral("state")).toString());
        t.deadline = qint64(o.value(QStringLiteral("deadline")).toDouble());
        t.left = qint64(o.value(QStringLiteral("left")).toDouble());
        t.finishedAt = qint64(o.value(QStringLiteral("finishedAt")).toDouble());
        if (!t.id.isEmpty() && t.duration > 0)
            m_timers.append(t);
    }
}

void TimerModel::save() const
{
    if (m_path.isEmpty())
        return;
    QJsonArray list;
    for (const Timer &t : m_timers) {
        list.append(QJsonObject{{QStringLiteral("id"), t.id},
                                {QStringLiteral("name"), t.name},
                                {QStringLiteral("duration"), double(t.duration)},
                                {QStringLiteral("state"), QString::fromLatin1(stateName(t.state))},
                                {QStringLiteral("deadline"), double(t.deadline)},
                                {QStringLiteral("left"), double(t.left)},
                                {QStringLiteral("finishedAt"), double(t.finishedAt)}});
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(list).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
