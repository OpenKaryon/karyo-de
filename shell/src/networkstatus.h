// SPDX-License-Identifier: GPL-3.0-or-later
// Primary connection from NetworkManager: the tray's network glyph and the
// network tile of Quick settings.
#pragma once

#include <QObject>
#include <QString>

class NetworkStatus : public QObject
{
    Q_OBJECT
public:
    explicit NetworkStatus(QObject *parent = nullptr);

    // Lucide glyph: "wifi" for wireless or no connection, "network" for wired.
    QString glyph() const;
    bool isConnected() const { return m_connected; }
    bool isWired() const { return m_type == QLatin1String("802-3-ethernet"); }
    // Name of the primary connection ("LabNet", "Wired connection 1").
    QString connectionName() const { return m_name; }
    // A Wi-Fi device exists; its radio can be switched.
    bool hasWireless() const { return m_hasWireless; }
    bool isWirelessEnabled() const { return m_wirelessEnabled; }
    void setWirelessEnabled(bool enabled);

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void refresh();
    void refreshDevices();

private:
    QString m_type;
    QString m_name;
    bool m_connected = false;
    bool m_hasWireless = false;
    bool m_wirelessEnabled = false;
    bool m_physicalPending = false;
};
