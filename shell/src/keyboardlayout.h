// SPDX-License-Identifier: GPL-3.0-or-later
// The seat's active keyboard layout (XKB group) for the taskbar's input
// language. labwc (0.8.2 and later) sends keyboard modifiers, the group
// among them, to every client, focused or not.
#pragma once

#include <QObject>

struct wl_keyboard;
struct wl_registry;
struct wl_seat;
struct openkaryon_keyboard_layout_v1;

class KeyboardLayout : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardLayout(QObject *parent = nullptr);
    ~KeyboardLayout() override;

    // Index into the session's layout list (XKB_DEFAULT_LAYOUT).
    int group() const { return m_group; }
    bool canCycle() const { return m_control != nullptr; }
    void cycle();

Q_SIGNALS:
    void groupChanged(int group);
    void availabilityChanged(bool available);

private:
    friend struct KeyboardLayoutListeners;
    void bindSeat(wl_registry *registry, quint32 name, quint32 version);
    void bindControl(wl_registry *registry, quint32 name);
    void removeGlobal(quint32 name);
    void setKeyboardPresent(bool present);
    void setGroup(int group);

    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    quint32 m_seatVersion = 0;
    wl_keyboard *m_keyboard = nullptr;
    openkaryon_keyboard_layout_v1 *m_control = nullptr;
    quint32 m_controlName = 0;
    int m_group = 0;
};
