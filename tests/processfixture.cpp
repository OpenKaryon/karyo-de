// SPDX-License-Identifier: GPL-3.0-or-later
#include <karyo/ui/desktopentry.h>
#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <unistd.h>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2)
        return 2;
    if (args.at(1) == QLatin1String("--spawn-and-exit")) {
        DesktopEntry entry;
        entry.exec = QStringLiteral("\"%1\" --delayed \"%2\"")
                         .arg(app.applicationFilePath(), args.last());
        return entry.launch() ? 0 : 3;
    }
    const bool delayed = args.at(1) == QLatin1String("--delayed");
    if (delayed)
        QThread::msleep(250);
    QFile file(args.last());
    if (!file.open(QIODevice::WriteOnly))
        return 4;
    file.write(delayed ? QByteArray("survived") : QByteArray::number(getppid()));
    return 0;
}
