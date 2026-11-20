// SPDX-License-Identifier: GPL-3.0-or-later
#include "signin.h"
#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>
#include <QApplication>
#include <QLockFile>
#include <QStandardPaths>
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc != 1) {
        std::fputs("Usage: karyo-lock\n", stderr);
        return 2;
    }
    qputenv("QT_QPA_PLATFORM", "wayland");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("karyo-lock"));
    app.setOrganizationName(QStringLiteral("karyo"));
    app.setDesktopFileName(QStringLiteral("karyo-lock"));
    QLockFile instance(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
                       + QStringLiteral("/karyo-lock.lock"));
    if (!instance.tryLock(0))
        return 1;
    Karyo::Ui::applyStyle();
    Karyo::Ui::Theme::followConfiguration();
    return runLock();
}
