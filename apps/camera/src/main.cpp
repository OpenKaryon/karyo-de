// SPDX-License-Identifier: GPL-3.0-or-later
#include "camerawindow.h"

#include <karyo/ui/style.h>

#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QTranslator>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("karyo-camera"));
    app.setOrganizationName(QStringLiteral("openkaryon"));
    app.setDesktopFileName(QStringLiteral("karyo-camera"));
    Karyo::Ui::applyStyle();
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("camera-photo")));

    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-camera"), QStringLiteral("_"),
                        QStringLiteral(":/i18n")))
        app.installTranslator(&translator);

    CameraWindow window;
    window.show();
    return app.exec();
}
