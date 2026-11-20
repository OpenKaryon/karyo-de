// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-photos [PICTURE]: Karyo Photos. With a picture it opens the
// viewer on it; without one, the gallery of the Pictures folder.
#include "photoswindow.h"

#include <karyo/ui/style.h>

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-photos"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    // Public Wayland identity; Karyo supplies client-side decoration rules.
    QApplication::setDesktopFileName(QStringLiteral("karyo-photos"));
    Karyo::Ui::applyStyle();

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-photos"), QStringLiteral("_"),
                        QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    PhotosWindow window;
    const QStringList args = QApplication::arguments().mid(1);
    if (!args.isEmpty())
        window.openFile(args.first());
    else
        window.showGallery();
    window.show();
    return QApplication::exec();
}
