// SPDX-License-Identifier: GPL-3.0-or-later
// Manual Wayland acceptance client: click from a different process than Files.
#include "filemanager.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDebug>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

class RecordingManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
    QStringList paths;
    QStringList tokens;
public Q_SLOTS:
    Q_SCRIPTABLE void ShowFolders(const QStringList &uris, const QString &token)
    {
        paths.append(uris);
        tokens.append(token);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-filemanager-fixture"));
    QApplication::setDesktopFileName(QStringLiteral("org.karyo.FileManagerFixture"));
    const bool verify = app.arguments().contains(QStringLiteral("--verify-tokens"));
    if (app.arguments().size() != (verify ? 3 : 2))
        return 2;
    const QString target = app.arguments().at(1);
    RecordingManager manager;
    if (verify)
    {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), &manager,
                QDBusConnection::ExportScriptableSlots)
            || !bus.registerService(QStringLiteral("org.freedesktop.FileManager1")))
            return 3; // Run this mode inside its own dbus-run-session.
        QTimer::singleShot(20000, &app, [&] { app.exit(4); });
    }
    QWidget window;
    window.setWindowTitle(QStringLiteral("File manager activation fixture"));
    auto *layout = new QVBoxLayout(&window);
    layout->addWidget(new QLabel(verify ? QStringLiteral("Check concurrent activation and cancelled request")
                                        : QStringLiteral("Open an existing background Files folder"),
        &window));
    auto *button = new QPushButton(QStringLiteral("Open folder"), &window);
    layout->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, &window,
        [&]
        {
            qInfo() << "User requested folder from separate GUI client";
            Karyo::Apps::openFolder(target, &window);
            if (!verify)
                return;
            button->setEnabled(false);
            Karyo::Apps::openFolder(target + QStringLiteral("/second"), &window);
            auto *cancelled = new QObject;
            Karyo::Apps::openFolder(target + QStringLiteral("/cancelled"), cancelled);
            delete cancelled; // Must cancel the asynchronous request, including its timeout.
            QTimer::singleShot(1500, &app,
                [&]
                {
                    const QSet<QString> expected {
                        QUrl::fromLocalFile(target).toString(QUrl::FullyEncoded),
                        QUrl::fromLocalFile(target + QStringLiteral("/second")).toString(QUrl::FullyEncoded)
                    };
                    const QSet<QString> actual(manager.paths.cbegin(), manager.paths.cend());
                    if (actual != expected || manager.paths.size() != 2 || manager.tokens.size() != 2
                        || manager.tokens.at(0).isEmpty() || manager.tokens.at(1).isEmpty()
                        || manager.tokens.at(0) == manager.tokens.at(1))
                    {
                        qCritical() << "FAIL request count, identity, unique tokens or cancellation";
                        app.exit(5);
                        return;
                    }
                    qInfo()
                        << "PASS two concurrent requests have unique tokens; destroyed context sends nothing";
                    app.exit(0);
                });
        });
    window.resize(500, 180);
    window.show();
    return app.exec();
}
#include "filemanagerfixture.moc"
