// SPDX-License-Identifier: GPL-3.0-or-later
// Quick settings (KaryonPanels.dc.html, variant quick): tiles for network,
// Bluetooth, night light, Do not disturb and the power profile, brightness
// and volume sliders, and the audio output. It opens from the Wi-Fi and
// volume pill; controls the system lacks are left out.
#pragma once

#include "flyout.h"

class AudioControl;
class BluetoothControl;
class BrightnessControl;
class NetworkStatus;
class NightLight;
class NotificationServer;
class PowerProfileControl;
class QGridLayout;
class QLabel;
class QSlider;

namespace Karyo::Ui {
class IconButton;
}

class QuickSettings : public Flyout
{
    Q_OBJECT
public:
    QuickSettings(NetworkStatus *network, NotificationServer *notifications, NightLight *nightLight,
                  QScreen *screen);
    bool showsNetwork() const { return m_showNetwork; }
    void showNetwork(bool show);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void rebuildTiles();
    void updateAudio();
    void fitHeight();

    NetworkStatus *m_network;
    NotificationServer *m_notifications;
    NightLight *m_nightLight;
    AudioControl *m_audio;
    BrightnessControl *m_brightness;
    BluetoothControl *m_bluetooth;
    PowerProfileControl *m_power;

    QWidget *m_card;
    QWidget *m_overview = nullptr;
    QWidget *m_networkPage = nullptr;
    bool m_showNetwork = false;
    QGridLayout *m_grid;
    QList<QWidget *> m_tiles;
    QWidget *m_volumeRow = nullptr;
    QSlider *m_volume = nullptr;
    Karyo::Ui::IconButton *m_mute = nullptr;
    QLabel *m_output = nullptr;
};
