// SPDX-License-Identifier: GPL-3.0-or-later
#include "keyboardlayout.h"
#include "wayland-openkaryon-keyboard-layout-v1-client-protocol.h"

#include <QGuiApplication>

#include <wayland-client.h>

#include <unistd.h>

// The objects live on the default queue, which Qt dispatches on the GUI thread.
struct KeyboardLayoutListeners
{
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface,
                       uint32_t version)
    {
        if (qstrcmp(interface, wl_seat_interface.name) == 0)
            static_cast<KeyboardLayout *>(data)->bindSeat(registry, name, version);
        else if (qstrcmp(interface, openkaryon_keyboard_layout_v1_interface.name) == 0)
            static_cast<KeyboardLayout *>(data)->bindControl(registry, name);
    }
    static void globalRemove(void *data, wl_registry *, uint32_t name)
    {
        static_cast<KeyboardLayout *>(data)->removeGlobal(name);
    }

    static void capabilities(void *data, wl_seat *, uint32_t capabilities)
    {
        static_cast<KeyboardLayout *>(data)->setKeyboardPresent(capabilities & WL_SEAT_CAPABILITY_KEYBOARD);
    }
    static void name(void *, wl_seat *, const char *) {}

    // The keymap itself is not needed: the group indexes the session's layouts.
    static void keymap(void *, wl_keyboard *, uint32_t, int32_t fd, uint32_t) { close(fd); }
    static void enter(void *, wl_keyboard *, uint32_t, wl_surface *, wl_array *) {}
    static void leave(void *, wl_keyboard *, uint32_t, wl_surface *) {}
    static void key(void *, wl_keyboard *, uint32_t, uint32_t, uint32_t, uint32_t) {}
    static void modifiers(void *data, wl_keyboard *, uint32_t, uint32_t, uint32_t, uint32_t,
                          uint32_t group)
    {
        static_cast<KeyboardLayout *>(data)->setGroup(int(group));
    }
    static void repeatInfo(void *, wl_keyboard *, int32_t, int32_t) {}

    static const wl_registry_listener registryListener;
    static const wl_seat_listener seatListener;
    static const wl_keyboard_listener keyboardListener;
};

const wl_registry_listener KeyboardLayoutListeners::registryListener = {global, globalRemove};
const wl_seat_listener KeyboardLayoutListeners::seatListener = {capabilities, name};
const wl_keyboard_listener KeyboardLayoutListeners::keyboardListener = {
    keymap, enter, leave, key, modifiers, repeatInfo};

KeyboardLayout::KeyboardLayout(QObject *parent)
    : QObject(parent)
{
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!app || !app->display())
        return;
    m_registry = wl_display_get_registry(app->display());
    wl_registry_add_listener(m_registry, &KeyboardLayoutListeners::registryListener, this);
}

KeyboardLayout::~KeyboardLayout()
{
    if (m_control)
        openkaryon_keyboard_layout_v1_destroy(m_control);
    setKeyboardPresent(false);
    if (m_seat) {
        if (m_seatVersion >= WL_SEAT_RELEASE_SINCE_VERSION)
            wl_seat_release(m_seat);
        else
            wl_seat_destroy(m_seat);
    }
    if (m_registry)
        wl_registry_destroy(m_registry);
}

void KeyboardLayout::bindControl(wl_registry *registry, quint32 name)
{
    if (m_control)
        return;
    m_controlName = name;
    m_control = static_cast<openkaryon_keyboard_layout_v1 *>(
        wl_registry_bind(registry, name, &openkaryon_keyboard_layout_v1_interface, 1));
    Q_EMIT availabilityChanged(true);
}

void KeyboardLayout::removeGlobal(quint32 name)
{
    if (m_control && name == m_controlName) {
        openkaryon_keyboard_layout_v1_destroy(m_control);
        m_control = nullptr;
        m_controlName = 0;
        Q_EMIT availabilityChanged(false);
    }
}

void KeyboardLayout::cycle()
{
    if (m_control && m_seat)
        openkaryon_keyboard_layout_v1_cycle(m_control, m_seat);
}

void KeyboardLayout::bindSeat(wl_registry *registry, quint32 name, quint32 version)
{
    if (m_seat)
        return;
    m_seatVersion = qMin(version, 5u);
    m_seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, m_seatVersion));
    wl_seat_add_listener(m_seat, &KeyboardLayoutListeners::seatListener, this);
}

void KeyboardLayout::setKeyboardPresent(bool present)
{
    if (present && !m_keyboard) {
        m_keyboard = wl_seat_get_keyboard(m_seat);
        wl_keyboard_add_listener(m_keyboard, &KeyboardLayoutListeners::keyboardListener, this);
    } else if (!present && m_keyboard) {
        if (m_seatVersion >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
            wl_keyboard_release(m_keyboard);
        else
            wl_keyboard_destroy(m_keyboard);
        m_keyboard = nullptr;
    }
}

void KeyboardLayout::setGroup(int group)
{
    if (group == m_group)
        return;
    m_group = group;
    Q_EMIT groupChanged(group);
}
