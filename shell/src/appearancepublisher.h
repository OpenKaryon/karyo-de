// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace Karyo::Session
{
// Publish only public appearance values as the effective account, never $USER.
// Missing optional publication directories are harmless failures.
bool publishAppearance(const QString &theme, const QString &accent,
    const QString &directory = QStringLiteral("/var/lib/karyo/appearance"));
}
