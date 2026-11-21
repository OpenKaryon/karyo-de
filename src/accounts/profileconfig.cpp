// SPDX-License-Identifier: GPL-3.0-or-later
#include "profileconfig.h"
#include <QFile>
#include <QStringDecoder>
#include <QStringEncoder>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Karyo::Accounts::Internal
{
namespace
{
    constexpr qsizetype Limit = 64 * 1024;
    bool trustedDirectory(int fd)
    {
        struct stat info { };
        return ::fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == 0 && !(info.st_mode & 0022);
    }
}
ConfigText readRootConfig(const QString &path)
{
    if (!path.startsWith(QLatin1Char('/')) || path.contains(QChar::Null) || path.endsWith(QLatin1Char('/')))
        return { };
    const auto parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.contains(QStringLiteral(".")) || parts.contains(QStringLiteral("..")))
        return { };
    const int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    int directory = ::open("/", flags);
    if (directory < 0)
        return { };
    if (!trustedDirectory(directory))
    {
        ::close(directory);
        return { };
    }
    for (qsizetype i = 0; i + 1 < parts.size(); ++i)
    {
        const int next = ::openat(directory, QFile::encodeName(parts[i]).constData(), flags);
        const int error = errno;
        ::close(directory);
        if (next < 0)
            return { error == ENOENT ? ReadState::Missing : ReadState::Invalid, { } };
        directory = next;
        if (!trustedDirectory(directory))
        {
            ::close(directory);
            return { };
        }
    }
    const int fd = ::openat(directory, QFile::encodeName(parts.last()).constData(),
        O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    const int error = errno;
    ::close(directory);
    if (fd < 0)
        return { error == ENOENT ? ReadState::Missing : ReadState::Invalid, { } };
    struct stat info { };
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 || (info.st_mode & 0022)
        || info.st_size > Limit)
    {
        ::close(fd);
        return { };
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle))
    {
        ::close(fd);
        return { };
    }
    const auto data = file.read(Limit + 1);
    if (file.error() != QFileDevice::NoError || data.size() > Limit || data.contains('\0'))
        return { };
    // Stateless decoding rejects incomplete final UTF-8 sequences as well.
    QStringDecoder decoder(
        QStringDecoder::Utf8, QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    const QString text = decoder(data);
    if (decoder.hasError())
        return { };
    return { ReadState::Found, text };
}
ConfigText readConfig(const QString &current, const QString &legacy)
{
    const auto result = readRootConfig(current);
    return result.state == ReadState::Missing && !legacy.isEmpty() ? readRootConfig(legacy) : result;
}
bool validLogin(const QString &login)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_.-]*\\$?\\z"));
    return login.size() <= 32 && pattern.match(login).hasMatch();
}
bool boundedText(const QString &text)
{
    if (text.size() > Limit || text.contains(QChar::Null))
        return false;
    QStringEncoder encoder(QStringEncoder::Utf8, QStringConverter::Flag::Stateless);
    const QByteArray data = encoder(text);
    return !encoder.hasError() && data.size() <= Limit;
}
const QRegularExpression &lineBreaks()
{
    // Match Python str.splitlines(), including CRLF as one separator.
    static const QRegularExpression pattern(
        QStringLiteral("\\r\\n|[\\n\\r\\v\\f\\x{1c}-\\x{1e}\\x{85}\\x{2028}\\x{2029}]"));
    return pattern;
}
const QRegularExpression &whitespace()
{
    static const QRegularExpression pattern(
        QStringLiteral("[\\s\\x{1c}-\\x{1f}]+"), QRegularExpression::UseUnicodePropertiesOption);
    return pattern;
}
QString stripped(QString text)
{
    static const QRegularExpression edges(QStringLiteral("\\A[\\s\\x{1c}-\\x{1f}]+|[\\s\\x{1c}-\\x{1f}]+\\z"),
        QRegularExpression::UseUnicodePropertiesOption);
    return text.remove(edges);
}
} // namespace Karyo::Accounts::Internal
