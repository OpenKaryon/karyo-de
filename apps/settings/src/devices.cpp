// SPDX-License-Identifier: GPL-3.0-or-later
// Bluetooth & devices (BlueZ, input devices) and Network & internet (nmcli).
#include "pages.h"
#include "widgets.h"

#include "blueztypes.h"

#include <QCheckBox>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDevice>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace
{

const QString Bluez = QStringLiteral("org.bluez");
using Karyo::Bluetooth::InterfaceMap;
using Karyo::Bluetooth::ObjectMap;

class DevicesPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit DevicesPage(QWidget *parent)
        : SettingsPage(tr("Bluetooth & devices"), parent)
    {
        qDBusRegisterMetaType<InterfaceMap>();
        qDBusRegisterMetaType<ObjectMap>();
        addKeywords({ QStringLiteral("bluetooth"), tr("Mouse"), tr("Keyboard"), tr("Touchpad") });
        m_bluetooth = new SettingRow(
            QStringLiteral("bluetooth"), tr("Bluetooth"), tr("Looking for an adapter…"), this);
        m_toggle = m_bluetooth->addToggle(false);
        m_toggle->setObjectName("bluetoothPower");
        m_toggle->setEnabled(false);
        connect(m_toggle, &QAbstractButton::toggled, this,
            [this](bool on)
            {
                if (m_adapter.isEmpty() || m_busy)
                    return;
                auto call = QDBusMessage::createMethodCall(Bluez, m_adapter,
                    QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
                call << QStringLiteral("org.bluez.Adapter1") << QStringLiteral("Powered")
                     << QVariant::fromValue(QDBusVariant(on));
                change(call);
            });
        column()->addWidget(m_bluetooth);
        m_error = new QLabel(this);
        m_error->setObjectName("bluetoothStatus");
        m_error->setWordWrap(true);
        m_error->setTextFormat(Qt::PlainText);
        column()->addWidget(m_error);
        m_deviceControls = new QWidget(this);
        m_devices = new QVBoxLayout(m_deviceControls);
        m_devices->setContentsMargins(0, 0, 0, 0);
        m_devices->setSpacing(10);
        column()->addWidget(m_deviceControls);

        column()->addWidget(sectionHeading(tr("Input devices")));
        for (const QInputDevice *device : QInputDevice::devices())
        {
            QString glyph = QStringLiteral("type");
            QString kind = tr("Keyboard");
            if (device->type() == QInputDevice::DeviceType::Mouse)
            {
                glyph = QStringLiteral("compass");
                kind = tr("Mouse");
            }
            else if (device->type() == QInputDevice::DeviceType::TouchPad)
            {
                glyph = QStringLiteral("compass");
                kind = tr("Touchpad");
            }
            else if (device->type() != QInputDevice::DeviceType::Keyboard)
            {
                continue;
            }
            const QString name = device->name().isEmpty() ? kind : device->name();
            column()->addWidget(new SettingRow(glyph, name, kind, this));
        }
        QTimer::singleShot(0, this, [this] { refresh(); });
    }

    void refresh() override { read(false); }

private:
    void read(bool preserveError)
    {
        if (m_busy)
            return;
        m_busy = true;
        m_adapter.clear();
        m_toggle->setEnabled(false);
        m_deviceControls->setEnabled(false);
        if (!preserveError)
            m_error->clear();
        QDBusConnection bus = QDBusConnection::systemBus();
        QDBusMessage call = QDBusMessage::createMethodCall(Bluez, QStringLiteral("/"),
            QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects"));
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, 5000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w)
            {
                w->deleteLater();
                m_busy = false;
                QDBusPendingReply<ObjectMap> reply = *w;
                if (reply.isError())
                {
                    m_bluetooth->setDescription(tr("Bluetooth is unavailable"));
                    m_error->setText(reply.error().message());
                    return;
                }
                m_deviceControls->setEnabled(true);
                while (QLayoutItem *item = m_devices->takeAt(0))
                {
                    delete item->widget();
                    delete item;
                }
                const ObjectMap objects = reply.value();
                for (auto it = objects.cbegin(); it != objects.cend(); ++it)
                {
                    const InterfaceMap &interfaces = it.value();
                    if (interfaces.contains(QStringLiteral("org.bluez.Adapter1")) && m_adapter.isEmpty())
                    {
                        m_adapter = it.key().path();
                        const QVariantMap adapter = interfaces.value(QStringLiteral("org.bluez.Adapter1"));
                        const QSignalBlocker block(m_toggle);
                        m_toggle->setEnabled(true);
                        m_toggle->setChecked(adapter.value(QStringLiteral("Powered")).toBool());
                        m_bluetooth->setValue(m_toggle->isChecked() ? tr("On") : tr("Off"));
                        m_bluetooth->setDescription(tr("Discoverable as \"%1\"")
                                .arg(adapter.value(QStringLiteral("Alias")).toString()));
                    }
                    if (!interfaces.contains(QStringLiteral("org.bluez.Device1")))
                        continue;
                    const QVariantMap device = interfaces.value(QStringLiteral("org.bluez.Device1"));
                    if (!device.value(QStringLiteral("Paired")).toBool())
                        continue;
                    const bool connected = device.value(QStringLiteral("Connected")).toBool();
                    auto *row = new SettingRow(QStringLiteral("bluetooth"),
                        device.value(QStringLiteral("Alias")).toString(),
                        connected ? tr("Connected") : tr("Paired"), this);
                    auto *button = new QPushButton(connected ? tr("Disconnect") : tr("Connect"));
                    const QString path = it.key().path();
                    connect(button, &QPushButton::clicked, this,
                        [this, path, connected]
                        {
                            change(QDBusMessage::createMethodCall(Bluez, path,
                                QStringLiteral("org.bluez.Device1"),
                                connected ? QStringLiteral("Disconnect") : QStringLiteral("Connect")));
                        });
                    row->setControl(button);
                    m_devices->addWidget(row);
                }
                if (m_adapter.isEmpty())
                    m_bluetooth->setDescription(tr("No Bluetooth adapter found"));
            });
    }

    void change(QDBusMessage call)
    {
        if (m_busy)
            return;
        m_busy = true;
        m_toggle->setEnabled(false);
        m_deviceControls->setEnabled(false);
        m_error->clear();
        call.setInteractiveAuthorizationAllowed(true);
        auto *watcher
            = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 30000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher]
            {
                const QDBusPendingReply<> reply = *watcher;
                watcher->deleteLater();
                m_busy = false;
                if (reply.isError())
                    m_error->setText(reply.error().message());
                read(true);
            });
    }
    bool m_busy = false;
    QLabel *m_error;
    QWidget *m_deviceControls;
    SettingRow *m_bluetooth;
    ToggleSwitch *m_toggle;
    QVBoxLayout *m_devices;
    QString m_adapter;
};

} // namespace
SettingsPage *createDevicesPage(QWidget *parent) { return new DevicesPage(parent); }
#include "devices.moc"
