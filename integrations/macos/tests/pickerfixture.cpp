// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic Linux helper for GUI lifecycle tests; never a Darling engine.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>
#include <cstdio>
#include <unistd.h>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.value(0) == QStringLiteral("--status")) {
        QThread::msleep(140);
        const QString mode = qEnvironmentVariable("KARYO_PICKER_TEST_STATUS");
        if (mode == QStringLiteral("bad")) { std::puts("not json"); return 0; }
        if (mode == QStringLiteral("large")) { const QByteArray bytes(70000, 'x'); std::fwrite(bytes.data(), 1, bytes.size(), stdout); return 0; }
        if (mode == QStringLiteral("timeout")) { QThread::sleep(12); return 0; }
        const bool installed = mode != QStringLiteral("missing");
        const QByteArray json = QJsonDocument(QJsonObject{{"schema", 1}, {"experimental", true}, {"installed", installed}}).toJson(QJsonDocument::Compact);
        std::fwrite(json.data(), 1, json.size(), stdout);
        return installed ? 0 : 1;
    }
    const bool inspecting = args.value(0) == QStringLiteral("--inspect");
    int appIndex = inspecting ? 1 : 0;
    if (args.value(appIndex) == QStringLiteral("--")) ++appIndex;
    const QString path = args.value(appIndex);
    const QString mode = QFileInfo(path).fileName();
    const QString logs = QFileInfo(path).absolutePath() + QStringLiteral("/logs");
    if (inspecting) {
        if (mode.contains(QStringLiteral("invalid-json"))) { std::puts("{not json"); return 0; }
        if (mode.contains(QStringLiteral("invalid-file"))) { std::fputs("<b>bad input</b>", stderr); return 1; }
        QJsonObject details{{"schema", 1}, {"experimental", true}, {"ownerUid", qint64(getuid())},
            {"persistent", !mode.contains(QStringLiteral("transient"))}, {"prefixCreated", false},
            {"name", "<b>fixture application</b>"}, {"application", path}, {"log", logs + "/run.log"},
            {"runtime", QJsonObject{{"installed", true}}}};
        const QByteArray json = QJsonDocument(details).toJson(QJsonDocument::Compact);
        std::fwrite(json.data(), 1, json.size(), stdout);
        return 0;
    }
    QDir().mkpath(logs);
    QFile log(logs + QStringLiteral("/run.log")); log.open(QIODevice::WriteOnly); log.write("fixture-only\n"); log.close();
    QFile record(path + QStringLiteral(".arguments.json")); record.open(QIODevice::WriteOnly);
    record.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson()); record.close();
    if (mode.contains(QStringLiteral("unicode"))) {
        const QByteArray text = QByteArray::fromHex("d09fd180d0b8d0bcd0b5d180");
        std::fwrite(text.data(), 1, 1, stdout); std::fflush(stdout); QThread::msleep(50);
        std::fwrite(text.data() + 1, 1, text.size() - 1, stdout);
    }
    if (mode.contains(QStringLiteral("output-limit"))) {
        const QByteArray text(250000, 'x'); std::fwrite(text.data(), 1, text.size(), stdout);
    }
    if (mode.contains(QStringLiteral("hold"))) {
        std::puts("fixture-running"); std::fflush(stdout);
        for (int count = 0; count < 400; ++count) {
            if (QFile::exists(path + QStringLiteral(".stop"))) return 0;
            QThread::msleep(10);
        }
        return 124;
    }
    if (mode.contains(QStringLiteral("failure"))) { std::fputs("<b>fixture diagnostic</b>", stderr); return 23; }
    return 0;
}
