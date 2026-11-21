// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-snip: Karyo Snipping Tool.
//   karyo-snip            the window: New, markup, Save
//   karyo-snip --region   the overlay (Print Screen, Super+Shift+S)
//   karyo-snip --screen   every screen at once (Ctrl+Print Screen)
//   karyo-snip PICTURE    marks up a picture
// --edit opens the editor without saving or copying the unedited image.
// Quick captures are copied, saved and announced; the notice can open them.
#include "capture.h"
#include "overlay.h"
#include "snipwindow.h"

#include <karyo/ui/style.h>

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QPointer>
#include <QMessageBox>
#include <QCommandLineParser>
#include <QLockFile>
#include <QStandardPaths>
#include <QTranslator>

#include <cstdio>
#include <memory>

namespace {

// After the overlay: copy, save, announce, and wait for the notice.
void deliver(const QImage &image, bool edit)
{
    const QString path = Capture::newPath();
    if (edit) {
        auto *window = new SnipWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(window, &QObject::destroyed, qApp, &QCoreApplication::quit);
        window->showCapture(image, path, false);
        window->show();
        return;
    }
    QString error;
    if (!Capture::save(image, path, &error)) {
        std::fprintf(stderr, "karyo-snip: %s\n", qPrintable(error));
        QMessageBox::warning(nullptr, QApplication::translate("SnipWindow", "Snipping Tool"), error);
    }
    Capture::copy(image);
    auto *notice = new ScreenshotNotice(path, qApp);
    auto window = std::make_shared<QPointer<SnipWindow>>();
    QObject::connect(notice, &ScreenshotNotice::markupRequested, qApp, [window, image, path] {
        if (!*window) {
            *window = new SnipWindow;
            (*window)->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(*window, &QObject::destroyed, qApp, &QCoreApplication::quit);
        }
        (*window)->showCapture(image, path);
        (*window)->show();
    });
    // The process ends with the notice unless the window is open.
    QObject::connect(notice, &ScreenshotNotice::finished, qApp, [window] {
        if (!*window)
            QCoreApplication::quit();
    });
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-snip"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    // Public Wayland identity; Karyo supplies client-side decorations.
    QApplication::setDesktopFileName(QStringLiteral("karyo-snip"));
    Karyo::Ui::applyStyle();

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-snip"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("SnipWindow", "Capture and annotate screenshots"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("region"), QStringLiteral("Select a rectangle")});
    parser.addOption({QStringLiteral("screen"), QStringLiteral("Capture all screens")});
    parser.addOption({QStringLiteral("window"), QStringLiteral("Click a window to capture")});
    parser.addOption({QStringLiteral("edit"), QStringLiteral("Open the editor after capturing")});
    parser.addPositionalArgument(QStringLiteral("picture"), QStringLiteral("Picture to annotate"), QStringLiteral("[picture]"));
    parser.process(app);
    const int captureModes = int(parser.isSet(QStringLiteral("region"))) + int(parser.isSet(QStringLiteral("screen")))
                           + int(parser.isSet(QStringLiteral("window")));
    if (captureModes > 1 || parser.positionalArguments().size() > 1
        || (captureModes && !parser.positionalArguments().isEmpty()))
        parser.showHelp(2);
    const QString mode = parser.positionalArguments().value(0);
    if (captureModes) {
        QLockFile captureLock(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
                              + QStringLiteral("/openkaryon-snip-capture.lock"));
        captureLock.setStaleLockTime(0);
        if (!captureLock.tryLock())
            return 0;
        const bool edit = parser.isSet(QStringLiteral("edit"));
        QApplication::setQuitOnLastWindowClosed(false);
        if (parser.isSet(QStringLiteral("screen"))) {
            QString error;
            const QImage image = Capture::grab(nullptr, &error);
            if (image.isNull()) {
                std::fprintf(stderr, "karyo-snip: %s\n", qPrintable(error));
                return 1;
            }
            captureLock.unlock();
            deliver(image, edit);
            return QApplication::exec();
        }
        Snipper snipper;
        QObject::connect(&snipper, &Snipper::captured, qApp, [&captureLock, edit](const QImage &image) {
            captureLock.unlock();
            deliver(image, edit);
        });
        QObject::connect(&snipper, &Snipper::cancelled, qApp, &QCoreApplication::quit);
        if (!snipper.start(parser.isSet(QStringLiteral("window")))) {
            std::fprintf(stderr, "karyo-snip: %s\n", qPrintable(snipper.error()));
            QMessageBox::warning(nullptr, QApplication::translate("SnipWindow", "Snipping Tool"), snipper.error());
            return 1;
        }
        return QApplication::exec();
    }

    SnipWindow window;
    if (!mode.isEmpty() && !window.openFile(mode))
        std::fprintf(stderr, "karyo-snip: %s is not a picture\n", qPrintable(mode));
    window.show();
    return QApplication::exec();
}
