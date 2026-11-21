// SPDX-License-Identifier: GPL-3.0-or-later
#include "filemanager.h"
#include "xdg-activation-client.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <qpa/qplatformwindow_p.h>

namespace
{

class FileManagerRequest : public QObject
{
public:
    FileManagerRequest(const QString &path, bool folder, QObject *owner)
        : QObject(owner)
        , m_absolute(path)
        , m_folder(folder)
    {
        m_timeout.setSingleShot(true);
        connect(&m_timeout, &QTimer::timeout, this, [this] { send({ }); });
    }

    ~FileManagerRequest() override
    {
        if (m_token)
            xdg_activation_token_v1_destroy(m_token);
        if (m_manager)
            xdg_activation_v1_destroy(m_manager);
        if (m_sync)
            wl_callback_destroy(m_sync);
        if (m_registry)
            wl_registry_destroy(m_registry);
    }

    void start()
    {
#if QT_CONFIG(wayland)
        m_window = QGuiApplication::focusWindow();
        auto *application = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        auto *native
            = m_window ? m_window->nativeInterface<QNativeInterface::Private::QWaylandWindow>() : nullptr;
        if (application && native && application->lastInputSeat() && application->lastInputSerial())
        {
            // Every invocation owns its token object. A window-wide Qt token
            // signal cannot distinguish concurrent FileManager1 requests.
            m_seat = application->lastInputSeat();
            m_serial = application->lastInputSerial();
            m_registry = wl_display_get_registry(application->display());
            wl_registry_add_listener(m_registry, &registryListener, this);
            m_sync = wl_display_sync(application->display());
            wl_callback_add_listener(m_sync, &syncListener, this);
            connect(m_window, &QObject::destroyed, this, &QObject::deleteLater);
            m_timeout.start(1000);
            return;
        }
#endif
        send({ });
    }

private:
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t)
    {
        auto *self = static_cast<FileManagerRequest *>(data);
        if (!self->m_manager && qstrcmp(interface, xdg_activation_v1_interface.name) == 0)
        {
            self->m_manager = static_cast<xdg_activation_v1 *>(
                wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1));
            self->m_managerName = name;
        }
    }
    static void removed(void *data, wl_registry *, uint32_t name)
    {
        auto *self = static_cast<FileManagerRequest *>(data);
        if (name == self->m_managerName)
            self->send({ });
    }
    static void registryDone(void *data, wl_callback *callback, uint32_t)
    {
        auto *self = static_cast<FileManagerRequest *>(data);
        wl_callback_destroy(callback);
        self->m_sync = nullptr;
        if (self->m_sent)
            return;
#if QT_CONFIG(wayland)
        if (!self->m_window)
        {
            self->deleteLater();
            return;
        }
        auto *native = self->m_window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        if (self->m_manager && native && native->surface())
        {
            self->m_token = xdg_activation_v1_get_activation_token(self->m_manager);
            xdg_activation_token_v1_add_listener(self->m_token, &tokenListener, self);
            xdg_activation_token_v1_set_serial(self->m_token, self->m_serial, self->m_seat);
            xdg_activation_token_v1_set_surface(self->m_token, native->surface());
            xdg_activation_token_v1_commit(self->m_token);
            return;
        }
#endif
        self->send({ });
    }
    static void tokenDone(void *data, xdg_activation_token_v1 *, const char *token)
    {
        static_cast<FileManagerRequest *>(data)->send(QString::fromUtf8(token));
    }
    static const wl_registry_listener registryListener;
    static const wl_callback_listener syncListener;
    static const xdg_activation_token_v1_listener tokenListener;

    void send(const QString &token)
    {
        if (m_sent)
            return;
        m_sent = true;
        m_timeout.stop();
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
            QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1"),
            m_folder ? QStringLiteral("ShowFolders") : QStringLiteral("ShowItems"));
        message << QStringList { QUrl::fromLocalFile(m_absolute).toString(QUrl::FullyEncoded) } << token;
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message),
            parent() ? parent() : QCoreApplication::instance());
        const QString fallback = m_folder ? m_absolute : QFileInfo(m_absolute).absolutePath();
        connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
            [watcher, fallback]
            {
                QDBusPendingReply<> reply = *watcher;
                if (reply.isError())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(fallback));
                watcher->deleteLater();
            });
        deleteLater();
    }

    wl_registry *m_registry = nullptr;
    wl_callback *m_sync = nullptr;
    xdg_activation_v1 *m_manager = nullptr;
    xdg_activation_token_v1 *m_token = nullptr;
    wl_seat *m_seat = nullptr;
    quint32 m_managerName = 0;
    quint32 m_serial = 0;
    QPointer<QWindow> m_window;
    QString m_absolute;
    bool m_folder;
    bool m_sent = false;
    QTimer m_timeout { this };
};

const wl_registry_listener FileManagerRequest::registryListener = { global, removed };
const wl_callback_listener FileManagerRequest::syncListener = { registryDone };
const xdg_activation_token_v1_listener FileManagerRequest::tokenListener = { tokenDone };

void request(const QString &path, bool folder, QObject *context)
{
    if (path.isEmpty())
        return;
    auto *pending = new FileManagerRequest(
        QFileInfo(path).absoluteFilePath(), folder, context ? context : QCoreApplication::instance());
    pending->start();
}

} // namespace

void Karyo::Apps::showInFiles(const QString &path, QObject *context) { request(path, false, context); }
void Karyo::Apps::openFolder(const QString &path, QObject *context) { request(path, true, context); }
