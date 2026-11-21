// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "archivebackend.h"
namespace ArchiveBackend::SevenZip {
bool needed(const Result &result);
Result list(const QString &path, const QString &password, std::atomic_bool &cancel);
Result extract(const QString &path, const QString &parent, const QStringList &selected,
               const QString &password, std::atomic_bool &cancel, const Progress &progress);
Result create(const QStringList &sources, const QString &output, const QString &password,
              bool encryptNames, std::atomic_bool &cancel, const Progress &progress, int compressionLevel = -1);
}
