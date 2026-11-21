// SPDX-License-Identifier: GPL-3.0-or-later
#include "archivewindow.h"
#include <karyo/ui/style.h>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QCommandLineParser>
#include <QTimer>
#include <QThreadPool>
#include <QLocale>
#include <QTranslator>
#include <QUrl>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("karyo-archive");
    QApplication::setOrganizationName("openkaryon");
    QApplication::setDesktopFileName("karyo-archive");
    Karyo::Ui::applyStyle();
    QTranslator translator;
    if (translator.load(QLocale(), "karyo-archive", "_", ":/i18n")) QApplication::installTranslator(&translator);
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("ArchiveWindow", "Browse or create archives."));
    parser.addHelpOption();
    const QCommandLineOption create(QStringLiteral("create"), QCoreApplication::translate("ArchiveWindow", "Create an archive from the selected files and folders."));
    parser.addOption(create);
    parser.addPositionalArgument(QStringLiteral("paths"), QCoreApplication::translate("ArchiveWindow", "Archives to open, or source paths with --create."));
    parser.process(app);
    QStringList paths;
    for (const QString &argument : parser.positionalArguments()) {
        if (argument.startsWith(QStringLiteral("file:"))) {
            const QUrl url(argument);
            if (url.isLocalFile()) paths.append(url.toLocalFile());
        } else paths.append(QFileInfo(argument).absoluteFilePath());
    }
    if (parser.isSet(create)) {
        auto *window = new ArchiveWindow;
        window->setAttribute(Qt::WA_DeleteOnClose); window->show();
        QTimer::singleShot(0, window, [window, paths] { window->createArchive(paths); });
        paths.clear();
    } else if (paths.isEmpty()) paths.append(QString());
    for (const QString &path : paths) {
        auto *window = new ArchiveWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();
        if (!path.isEmpty()) window->openArchive(path);
    }
    const int result = app.exec();
    QThreadPool::globalInstance()->waitForDone();
    return result;
}
