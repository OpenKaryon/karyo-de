// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearancepublisher.h"
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStringList>
#include <array>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
namespace
{
struct Descriptor
{
    explicit Descriptor(int fd)
        : value(fd)
    {
    }
    ~Descriptor()
    {
        if (value >= 0)
            ::close(value);
    }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
    int value;
};
}
bool Karyo::Session::publishAppearance(const QString &theme, const QString &accent, const QString &directory)
{
    if (getuid() != geteuid() || !QStringList { "light", "dark", "auto" }.contains(theme)
        || !QRegularExpression(QStringLiteral("\\A[a-z0-9-]{1,24}\\z")).match(accent).hasMatch())
        return false;
    std::array<char, 65536> buffer;
    struct passwd record { }, *entry = nullptr;
    if (::getpwuid_r(geteuid(), &record, buffer.data(), buffer.size(), &entry) != 0 || !entry)
        return false;
    const auto login = QString::fromLocal8Bit(entry->pw_name);
    if (!QRegularExpression(QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_.-]{0,127}\\$?\\z"))
            .match(login)
            .hasMatch())
        return false;
    Descriptor dir(
        ::open(QFile::encodeName(directory).constData(), O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (dir.value < 0)
        return false;
    struct stat directoryInfo { };
    if (::fstat(dir.value, &directoryInfo) != 0
        || ((directoryInfo.st_mode & (S_IWGRP | S_IWOTH)) && !(directoryInfo.st_mode & S_ISVTX)))
        return false;
    const auto name = QFile::encodeName(login + QStringLiteral(".conf"));
    struct stat existing { };
    if (::fstatat(dir.value, name.constData(), &existing, AT_SYMLINK_NOFOLLOW) == 0)
    {
        if (!S_ISREG(existing.st_mode) || existing.st_uid != geteuid())
            return false;
    }
    else if (errno != ENOENT)
        return false;
    QByteArray temporary;
    int fd = -1;
    for (int attempt = 0; attempt < 8 && fd < 0; ++attempt)
    {
        temporary = ".karyo-appearance-" + QByteArray::number(geteuid()) + "-"
            + QByteArray::number(QRandomGenerator::system()->generate64(), 16) + ".tmp";
        fd = ::openat(
            dir.value, temporary.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST)
            return false;
    }
    if (fd < 0)
        return false;
    Descriptor file(fd);
    const auto payload = "[Appearance]\ntheme=" + theme.toUtf8() + "\naccent=" + accent.toUtf8() + "\n";
    qsizetype done = 0;
    while (done < payload.size())
    {
        const auto count = ::write(fd, payload.constData() + done, size_t(payload.size() - done));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        done += count;
    }
    const bool ready = done == payload.size() && ::fchmod(fd, 0644) == 0 && ::fsync(fd) == 0;
    const bool saved
        = ready && ::renameat(dir.value, temporary.constData(), dir.value, name.constData()) == 0;
    if (!saved)
        ::unlinkat(dir.value, temporary.constData(), 0);
    return saved;
}
