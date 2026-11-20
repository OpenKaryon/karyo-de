// SPDX-License-Identifier: GPL-3.0-or-later
// The system behind Quick settings. Each control reports whether the system
// has it; Quick settings shows only those that are there.
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

// Default output of PipeWire, through WirePlumber's wpctl.
class AudioControl : public QObject
{
    Q_OBJECT
public:
    explicit AudioControl(QObject *parent = nullptr);
    bool isAvailable() const { return m_available; }
    int volume() const { return m_volume; }  // percent
    bool isMuted() const { return m_muted; }
    QString outputName() const { return m_outputName; }
    void setVolume(int percent);
    void toggleMute();

Q_SIGNALS:
    void changed();

private:
    void read();
    void run(const QStringList &args, const std::function<void(const QString &)> &done = {});

    bool m_available = false;
    int m_volume = 0;
    bool m_muted = false;
    QString m_outputName;
    int m_pendingVolume = -1;
    QTimer m_throttle;
};

// The first backlight, set through logind (no root needed).
class BrightnessControl : public QObject
{
    Q_OBJECT
public:
    explicit BrightnessControl(QObject *parent = nullptr);
    bool isAvailable() const { return !m_device.isEmpty() && m_max > 0; }
    int brightness() const;  // percent
    void setBrightness(int percent);

private:
    QString m_device;
    int m_max = 0;
    int m_value = 0;
};

// The first BlueZ adapter.
class BluetoothControl : public QObject
{
    Q_OBJECT
public:
    explicit BluetoothControl(QObject *parent = nullptr);
    bool isAvailable() const { return m_available; }
    bool isPowered() const { return m_powered; }
    void setPowered(bool powered);

Q_SIGNALS:
    void changed();

private:
    bool m_available = false;
    bool m_powered = false;
};

// power-profiles-daemon: power-saver, balanced, performance.
class PowerProfileControl : public QObject
{
    Q_OBJECT
public:
    explicit PowerProfileControl(QObject *parent = nullptr);
    bool isAvailable() const { return !m_profiles.isEmpty(); }
    QString profile() const { return m_profile; }
    // balanced -> performance (if offered) -> power-saver -> balanced.
    void cycle();
    static QString label(const QString &profile);

Q_SIGNALS:
    void changed();

private:
    void read();

    QString m_service;
    QString m_path;
    QString m_interface;
    QStringList m_profiles;
    QString m_profile;
};

// Night light: wlsunset holding a warm color temperature. It lives as long
// as the shell, and the choice is remembered in shell.conf.
class NightLight : public QObject
{
    Q_OBJECT
public:
    explicit NightLight(QObject *parent = nullptr);
    ~NightLight() override;
    bool isAvailable() const;
    bool isOn() const { return m_process != nullptr; }
    void setOn(bool on);

Q_SIGNALS:
    void changed();

private:
    void start();
    void stop();

    QPointer<QProcess> m_process;
};
