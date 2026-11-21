// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
class QObject;
namespace Karyo::Apps {
// Ask the active desktop to select a local item. Fall back to its parent folder
// if FileManager1 is unavailable. The optional context owns the pending request.
void showInFiles(const QString &path, QObject *context = nullptr);
}
