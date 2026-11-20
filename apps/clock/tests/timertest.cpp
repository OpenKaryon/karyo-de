// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the timers on a fake clock: start, pause, more time, finishing,
// and a running timer that finishes while Clock is closed.
#include "timers.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int failures = 0;
qint64 now = 1'000'000;

void expect(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("timers.json"));
    auto clock = [] { return now; };

    {
        TimerModel model(path, clock);
        QStringList finished;
        QObject::connect(&model, &TimerModel::finished, [&](const QString &id) { finished.append(id); });
        const int spin = model.add(QStringLiteral("Spin"), 10'000);
        expect(model.remaining(spin) == 10'000, "ready shows its duration");
        model.start(spin);
        now += 4'000;
        expect(model.remaining(spin) == 6'000, "running counts down");
        model.pause(spin);
        now += 60'000;
        expect(model.remaining(spin) == 6'000, "paused holds");
        model.start(spin);
        model.addTime(spin, 60'000);
        expect(model.remaining(spin) == 66'000, "a minute more");
        now += 66'000;
        model.tick();
        expect(model.timers().at(spin).state == TimerModel::State::Done, "done at zero");
        expect(finished == QStringList{model.timers().at(spin).id}, "finished once");
        model.tick();
        expect(finished.size() == 1, "no second finish");
        model.addTime(spin, 60'000);
        expect(model.timers().at(spin).state == TimerModel::State::Running && model.remaining(spin) == 60'000,
               "a minute more after the end runs again");
        model.reset(spin);
        expect(model.remaining(spin) == 10'000, "reset");

        const int gel = model.add(QStringLiteral("Gel"), 45 * 60'000);
        model.start(gel);
        model.edit(spin, QStringLiteral("Centrifuge"), 10'000);
        expect(model.timers().at(spin).name == QLatin1String("Centrifuge"), "rename keeps the timer");
    }

    // Closed for an hour: the gel timer ran out meanwhile.
    now += 60 * 60'000;
    {
        TimerModel model(path, clock);
        expect(model.timers().size() == 2, "timers are kept");
        const int gel = 1;
        expect(model.timers().at(gel).state == TimerModel::State::Running, "still running after a restart");
        int finished = 0;
        QObject::connect(&model, &TimerModel::finished, [&] { ++finished; });
        model.tick();
        expect(finished == 1 && model.timers().at(gel).state == TimerModel::State::Done,
               "it finishes on the first tick");
        model.remove(0);
        expect(model.timers().size() == 1 && model.timers().at(0).name == QLatin1String("Gel"), "remove");
    }

    if (failures == 0)
        std::printf("all passed\n");
    return failures == 0 ? 0 : 1;
}
