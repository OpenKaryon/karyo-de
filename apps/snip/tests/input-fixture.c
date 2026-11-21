// SPDX-License-Identifier: GPL-3.0-or-later
/* Test-only wlroots physical keyboard and pointer fixture for screenshot shortcuts. Never installed in the distro.
 * A headless compositor has no hardware input. Interpose backend start and
 * announce a normal wlr_keyboard, then feed evdev codes through its real XKB
 * update path. This avoids a virtual keyboard's separate modifier/keymap state.
 */
#define _GNU_SOURCE
#define WLR_USE_UNSTABLE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>

static struct wl_event_loop *loop;
static struct wlr_backend *primary;
static struct wlr_keyboard keyboard;
static struct wlr_pointer pointer;
static const struct wlr_pointer_impl pointer_impl = {.name = "Karyo QA pointer"};
static void move(double x, double y) {
    struct wlr_pointer_motion_absolute_event event = {.pointer=&pointer,.time_msec=1,.x=x,.y=y};
    wl_signal_emit_mutable(&pointer.events.motion_absolute,&event);
    wl_signal_emit_mutable(&pointer.events.frame,&pointer);
}
static void button(int down) {
    struct wlr_pointer_button_event event = {.pointer=&pointer,.time_msec=1,.button=272,
        .state=down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED};
    wl_signal_emit_mutable(&pointer.events.button,&event);
    wl_signal_emit_mutable(&pointer.events.frame,&pointer);
}
static const struct wlr_keyboard_impl implementation = {.name = "Karyo QA keyboard"};

static void key(unsigned code, int down) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct wlr_keyboard_key_event event = {
        .time_msec = now.tv_sec * 1000 + now.tv_nsec / 1000000,
        .keycode = code, .update_state = true,
        .state = down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
    };
    wlr_keyboard_notify_key(&keyboard, &event);
}
static int command(int fd, unsigned mask, void *data) {
    char bytes[32];
    int length = read(fd, bytes, sizeof(bytes));
    for (int i = 0; i < length; ++i) {
        if (bytes[i] >= '1' && bytes[i] <= '4') {
            unsigned code=2+bytes[i]-'1';
            key(29,1); key(42,1); key(code,1); key(code,0); key(42,0); key(29,0);
        }
        if (bytes[i] == 'r') { move(.2,.3); button(1); move(.6,.7); button(0); }
        if (bytes[i] == 's') { key(29,1); key(31,1); key(31,0); key(29,0); }
        if (bytes[i] == 'e') { key(1,1); key(1,0); }
        if (bytes[i] == 't') { key(56, 1); key(42, 1); key(42, 0); key(56, 0); }
        if (bytes[i] == 'w') {
            const int codes[] = {34, 35, 48, 32, 20, 49};
            for (int j = 0; j < 6; ++j) { key(codes[j], 1); key(codes[j], 0); }
        }
    }
    return 0;
}
struct wlr_backend *wlr_backend_autocreate(struct wl_event_loop *event_loop,
        struct wlr_session **session) {
    struct wlr_backend *(*original)(struct wl_event_loop *, struct wlr_session **) =
        dlsym(RTLD_NEXT, "wlr_backend_autocreate");
    loop = event_loop;
    primary = original(event_loop, session);
    return primary;
}
bool wlr_backend_start(struct wlr_backend *backend) {
    bool (*original)(struct wlr_backend *) = dlsym(RTLD_NEXT, "wlr_backend_start");
    bool result = original(backend);
    if (result && backend == primary && getenv("KARYO_TEST_KEYBOARD_FIFO")) {
        wlr_keyboard_init(&keyboard, &implementation, "Karyo QA physical keyboard");
        wl_signal_emit_mutable(&backend->events.new_input, &keyboard.base);
        wlr_pointer_init(&pointer,&pointer_impl,"Karyo QA pointer");
        wl_signal_emit_mutable(&backend->events.new_input,&pointer.base);
        int fd = open(getenv("KARYO_TEST_KEYBOARD_FIFO"), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, command, NULL);
    }
    return result;
}
