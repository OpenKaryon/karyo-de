// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktopaccounts.h"
#include "desktopsettings.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
using Karyo::Accounts::DesktopAccounts;
using Karyo::Accounts::DesktopSettings;
namespace
{
void check(bool ok, const char *message)
{
    if (!ok)
        qFatal("%s", message);
}
void put(const QString &path, const QByteArray &data)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "open fixture");
    check(file.write(data) == data.size(), "write complete fixture");
}
void corpus()
{
    QFile source(QStringLiteral(KARYO_PROFILE_CORPUS));
    check(source.open(QIODevice::ReadOnly), "shared policy corpus");
    const auto data = QJsonDocument::fromJson(source.readAll()).object();
    check(!data.isEmpty(), "valid corpus JSON");
    for (const auto &item : data.value("ownership").toArray())
    {
        const auto test = item.toObject();
        const auto value = DesktopAccounts::parse(test.value("text").toString());
        check(value.valid() == test.value("valid").toBool(), "ownership corpus validity");
        const auto owners = test.value("owners").toObject();
        for (auto owner = owners.begin(); owner != owners.end(); ++owner)
            check(
                value.ownerOf(owner.key()) == owner.value().toString(), "identical family across languages");
    }
    for (const auto &item : data.value("settings").toArray())
    {
        const auto test = item.toObject();
        const auto value = DesktopSettings::parse(test.value("text").toString());
        check(value.valid() == test.value("valid").toBool(), "settings corpus validity");
        if (value.valid())
        {
            check(value.passwordMode() == test.value("password").toString(), "identical password mode");
            check(value.vpn() == test.value("vpn").toString(), "identical VPN mode");
            check(value.workstation() == test.value("workstation").toBool(), "identical workstation mode");
        }
        else
            check(value.protectedEntry(), "invalid policy never suggests password-free entry");
    }
    const QString unpaired = QStringLiteral("#") + QChar(0xd800);
    check(!DesktopAccounts::parse(unpaired).valid() && !DesktopSettings::parse(unpaired).valid(),
        "unpaired UTF16 cannot become valid UTF8 policy");
    check(DesktopAccounts::parse(QString(65536, QLatin1Char('#'))).valid(), "exact byte limit");
    check(!DesktopAccounts::parse(QString(65537, QLatin1Char('#'))).valid(), "oversized model rejected");
    check(!DesktopAccounts::parse(QStringLiteral("#") + QString(32768, QChar(0x416))).valid(),
        "UTF8 byte limit not character count");
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && qstrcmp(argv[1], "--read-only-fixture") == 0)
    {
        const auto map = DesktopAccounts::load(QString::fromLocal8Bit(argv[2]));
        check(map.valid() && map.ownerOf("work") == QLatin1String("owner"),
            "ordinary process reads real root-owned metadata");
        qInfo("PASS ordinary root-owned metadata read");
        return 0;
    }
    if (argc == 3 && qstrcmp(argv[1], "--expect-invalid-fixture") == 0)
    {
        check(!DesktopAccounts::load(QString::fromLocal8Bit(argv[2])).valid(),
            "unsafe ownership fixture denied");
        qInfo("PASS unsafe metadata denied");
        return 0;
    }
    check(argc == 1, "test arguments");
    corpus();
    const bool root = ::geteuid() == 0;
    QTemporaryDir temporary(root ? QStringLiteral("/run/karyo-account-test-XXXXXX")
                                 : QDir::tempPath() + "/karyo-account-test-XXXXXX");
    check(temporary.isValid(), "private test directory");
    const auto current = temporary.filePath("current");
    const auto legacy = temporary.filePath("legacy");
    check(QDir().mkpath(current + "/desktops.d") && QDir().mkpath(legacy + "/desktops.d"),
        "fixture directories");
    const auto path = current + "/desktops.conf";
    const auto fallback = legacy + "/desktops.conf";
    put(path, "owner work\n");
    if (!root)
    {
        check(!DesktopAccounts::load(path).valid(), "ordinary-owned config is never authoritative");
        qInfo("PASS corpus and non-root denial; native root filesystem cases run separately");
        return 0;
    }
    check(DesktopAccounts::load(path).ownerOf("work") == QLatin1String("owner"), "trusted root file");
    put(fallback, "other work\n");
    QFile::remove(path);
    check(DesktopAccounts::load(path, fallback).ownerOf("work") == QLatin1String("other"),
        "missing current permits legacy");
    check(DesktopAccounts::load(temporary.filePath("missing")).valid(), "missing unconfigured mapping");
    check(::symlink("missing", QFile::encodeName(path).constData()) == 0, "dangling symlink fixture");
    check(!DesktopAccounts::load(path, fallback).valid(), "dangling current never falls back");
    QFile::remove(path);
    check(::symlink(QFile::encodeName(fallback).constData(), QFile::encodeName(path).constData()) == 0,
        "valid symlink fixture");
    check(!DesktopAccounts::load(path, fallback).valid(), "symlink current never accepted");
    QFile::remove(path);
    check(::mkfifo(QFile::encodeName(path).constData(), 0600) == 0, "FIFO fixture");
    QElapsedTimer timer;
    timer.start();
    check(!DesktopAccounts::load(path, fallback).valid(), "FIFO rejected");
    check(timer.elapsed() < 1000, "FIFO does not block");
    QFile::remove(path);
    for (const auto &data : { QByteArray("owner work\nother work\n"), QByteArray("owner work\n#\0", 13),
             QByteArray("owner work\n#") + QByteArray::fromHex("c3"),
             QByteArray("owner work\n#") + QByteArray::fromHex("ff"),
             QByteArray::fromHex("efbbbf") + QByteArray("owner work\n"), QByteArray(65537, '#') })
    {
        put(path, data);
        check(!DesktopAccounts::load(path, fallback).valid(), "malformed current never falls back");
    }
    put(path, "");
    check(DesktopAccounts::load(path, fallback).valid()
            && !DesktopAccounts::load(path, fallback).isChild("work"),
        "empty current authoritative");
    put(path, "owner work\n");
    check(::chmod(QFile::encodeName(path).constData(), 0664) == 0, "unsafe file permissions");
    check(!DesktopAccounts::load(path, fallback).valid(), "writable metadata rejected");
    check(::chmod(QFile::encodeName(path).constData(), 0644) == 0, "restore file permissions");
    check(::chmod(QFile::encodeName(current).constData(), 0775) == 0, "unsafe parent");
    check(!DesktopAccounts::load(path, fallback).valid(), "unsafe parent rejected");
    check(::chmod(QFile::encodeName(current).constData(), 0755) == 0, "restore parent");
    const auto redirect = temporary.filePath("redirect");
    check(::symlink(QFile::encodeName(current).constData(), QFile::encodeName(redirect).constData()) == 0,
        "parent symlink");
    check(!DesktopAccounts::load(redirect + "/desktops.conf", fallback).valid(), "symlink parent rejected");
    check(!DesktopAccounts::load("relative", fallback).valid(), "relative path rejected");
    check(!DesktopAccounts::load(":/metadata", fallback).valid(),
        "Qt resource path is not filesystem authority");
    check(!DesktopAccounts::load(current + "/../current/desktops.conf", fallback).valid(),
        "traversal rejected");
    check(!DesktopAccounts::load(current, fallback).valid(), "directory rejected as regular metadata");
    put(legacy + "/desktops.d/work.conf", "PASSWORD=home\nVPN=netbird\n");
    auto setting = DesktopSettings::load("work", current, legacy);
    check(setting.valid() && setting.passwordMode() == QLatin1String("home") && setting.protectedEntry(),
        "trusted settings legacy fallback");
    put(current + "/desktops.d/work.conf", "PASSWORD=separate\n");
    setting = DesktopSettings::load("work", current, legacy);
    check(setting.valid() && setting.passwordMode() == QLatin1String("separate"),
        "canonical settings override legacy");
    put(current + "/desktops.d/work.conf", "PASSWORD=unknown\n");
    setting = DesktopSettings::load("work", current, legacy);
    check(!setting.valid() && setting.protectedEntry(), "invalid settings fail closed without fallback");
    check(
        !DesktopSettings::load("../work", current, legacy).valid(), "settings login cannot escape metadata");
    check(DesktopSettings::load("fresh", current, legacy).valid()
            && !DesktopSettings::load("fresh", current, legacy).protectedEntry(),
        "unconfigured desktop has no extra entry password");
    const passwd *self = ::getpwuid(::getuid());
    check(self != nullptr, "actual current account");
    const auto selfPath = current + "/desktops.d/" + QString::fromLocal8Bit(self->pw_name) + ".conf";
    put(current + "/desktops.d/forged.conf", "PASSWORD=none\n");
    const auto originalUser = qgetenv("USER");
    const auto originalLogname = qgetenv("LOGNAME");
    const auto originalPassword = qgetenv("OPENKARYON_DESKTOP_PASSWORD");
    qputenv("USER", "forged");
    qputenv("LOGNAME", "forged");
    qputenv("OPENKARYON_DESKTOP_PASSWORD", "");
    put(selfPath, "PASSWORD=separate\n");
    check(DesktopSettings::current(current, legacy).protectedEntry(),
        "mandatory protection uses real UID despite empty environment override");
    put(selfPath, "PASSWORD=unknown\n");
    check(!DesktopSettings::current(current, legacy).valid()
            && DesktopSettings::current(current, legacy).protectedEntry(),
        "invalid current-user settings fail closed");
    put(selfPath, "PASSWORD=none\n");
    qputenv("OPENKARYON_DESKTOP_PASSWORD", "separate");
    check(!DesktopSettings::current(current, legacy).protectedEntry(),
        "user environment cannot add or replace root protection policy");
    for (const auto &pair : { qMakePair("USER", originalUser), qMakePair("LOGNAME", originalLogname),
             qMakePair("OPENKARYON_DESKTOP_PASSWORD", originalPassword) })
    {
        if (pair.second.isNull())
            qunsetenv(pair.first);
        else
            qputenv(pair.first, pair.second);
    }
    qInfo("PASS trusted account metadata, shared policy corpus and fail-closed protection");
}
