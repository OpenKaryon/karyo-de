// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
class QObject;
namespace Karyo::Apps {
// Ask the active desktop to select a local item. Fall back to its parent folder
// if FileManager1 is unavailable. The optional context owns the pending request.
// On Wayland, forward a bounded, per-request activation token from the focused
// caller. An absent protocol/input serial preserves ordinary unfocused routing.
void showInFiles(const QString &path, QObject *context = nullptr);
// Open a folder through the active manager, falling back to the default handler.
void openFolder(const QString &path, QObject *context = nullptr);
}
