// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "layout.h"
#include "lock.h"
#include "security.h"
#include "snapshot.h"
#include "xdg-shell.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

struct state
{
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct xdg_wm_base *wm;
    struct openkaryon_keyboard_layout_v1 *layout;
    struct openkaryon_window_snapshot_v1 *snapshot;
    struct wp_security_context_manager_v1 *security;
    struct ext_session_lock_manager_v1 *lock_manager;
    int windows, done, available, layouts, group;
    char titles[8][128];
};
struct window
{
    struct wl_surface *surface;
    struct xdg_surface *xdg;
    struct xdg_toplevel *top;
    struct wl_buffer *buffer;
    void *pixels;
    int configured;
};
static void sync_display(struct state *s) { assert(wl_display_roundtrip(s->display) >= 0); }
static void settle(struct state *s)
{
    for (int i = 0; i < 4; ++i)
    {
        sync_display(s);
        struct timespec pause = { .tv_nsec = 25000000 };
        nanosleep(&pause, NULL);
    }
}
static void ping(void *data, struct xdg_wm_base *wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
static const struct xdg_wm_base_listener wm_listener = { .ping = ping };
static void window_event(void *data, struct openkaryon_window_snapshot_v1 *proxy, int32_t x, int32_t y,
    int32_t width, int32_t height, const char *title)
{
    struct state *s = data;
    assert(width > 0 && height > 0 && s->windows < 8);
    snprintf(s->titles[s->windows++], 128, "%s", title);
}
static void done_event(void *data, struct openkaryon_window_snapshot_v1 *proxy, uint32_t available)
{
    struct state *s = data;
    s->done++;
    s->available = available;
}
static const struct openkaryon_window_snapshot_v1_listener snapshot_listener
    = { .window = window_event, .done = done_event };
static void keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size)
{
    struct state *s = data;
    assert(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size > 0);
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(map != MAP_FAILED);
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keys
        = xkb_keymap_new_from_string(context, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    assert(keys);
    s->layouts = xkb_keymap_num_layouts(keys);
    xkb_keymap_unref(keys);
    xkb_context_unref(context);
    munmap(map, size);
    close(fd);
}
static void enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface,
    struct wl_array *keys)
{
}
static void leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) { }
static void key(
    void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
}
static void modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed,
    uint32_t latched, uint32_t locked, uint32_t group)
{
    ((struct state *)data)->group = group;
}
static const struct wl_keyboard_listener keyboard_listener
    = { .keymap = keymap, .enter = enter, .leave = leave, .key = key, .modifiers = modifiers };
static void capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    struct state *s = data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s->keyboard)
    {
        s->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(s->keyboard, &keyboard_listener, s);
    }
}
static const struct wl_seat_listener seat_listener = { .capabilities = capabilities };
static void global(
    void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    struct state *s = data;
#define BIND(field, type)                                                                                    \
    if (!strcmp(interface, #type))                                                                           \
    {                                                                                                        \
        s->field = wl_registry_bind(registry, name, &type##_interface, 1);                                   \
    }
    BIND(compositor, wl_compositor)
    BIND(shm, wl_shm)
    BIND(seat, wl_seat)
    BIND(wm, xdg_wm_base)
    BIND(layout, openkaryon_keyboard_layout_v1)
    BIND(snapshot, openkaryon_window_snapshot_v1)
    BIND(security, wp_security_context_manager_v1)
    BIND(lock_manager, ext_session_lock_manager_v1)
#undef BIND
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) { }
static const struct wl_registry_listener registry_listener
    = { .global = global, .global_remove = global_remove };
static void discover(struct state *s)
{
    struct wl_registry *registry = wl_display_get_registry(s->display);
    wl_registry_add_listener(registry, &registry_listener, s);
    sync_display(s);
    wl_registry_destroy(registry);
    if (s->wm)
        xdg_wm_base_add_listener(s->wm, &wm_listener, s);
    if (s->seat)
        wl_seat_add_listener(s->seat, &seat_listener, s);
    if (s->snapshot)
        openkaryon_window_snapshot_v1_add_listener(s->snapshot, &snapshot_listener, s);
    sync_display(s);
}
static void configured(void *data, struct xdg_surface *surface, uint32_t serial)
{
    struct window *w = data;
    xdg_surface_ack_configure(surface, serial);
    w->configured = 1;
}
static const struct xdg_surface_listener surface_listener = { .configure = configured };
static void top_configured(
    void *data, struct xdg_toplevel *top, int32_t width, int32_t height, struct wl_array *states)
{
}
static void top_closed(void *data, struct xdg_toplevel *top) { }
static const struct xdg_toplevel_listener top_listener = { .configure = top_configured, .close = top_closed };
static void make_window(struct state *s, struct window *w, const char *title)
{
    w->surface = wl_compositor_create_surface(s->compositor);
    w->xdg = xdg_wm_base_get_xdg_surface(s->wm, w->surface);
    xdg_surface_add_listener(w->xdg, &surface_listener, w);
    w->top = xdg_surface_get_toplevel(w->xdg);
    xdg_toplevel_add_listener(w->top, &top_listener, w);
    xdg_toplevel_set_title(w->top, title);
    xdg_toplevel_set_app_id(w->top, "org.openkaryon.ProtocolQA");
    wl_surface_commit(w->surface);
    for (int i = 0; i < 20 && !w->configured; ++i)
        settle(s);
    assert(w->configured);
    char name[] = "/tmp/karyo-probe-shm-XXXXXX";
    int fd = mkstemp(name);
    assert(fd >= 0);
    unlink(name);
    const int bytes = 320 * 200 * 4;
    assert(ftruncate(fd, bytes) == 0);
    w->pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(w->pixels != MAP_FAILED);
    memset(w->pixels, 0xff, bytes);
    struct wl_shm_pool *pool = wl_shm_create_pool(s->shm, fd, bytes);
    w->buffer = wl_shm_pool_create_buffer(pool, 0, 320, 200, 320 * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_surface_attach(w->surface, w->buffer, 0, 0);
    wl_surface_damage(w->surface, 0, 0, 320, 200);
    wl_surface_commit(w->surface);
    settle(s);
}
static void snapshot(struct state *s, int count, int available, const char *first)
{
    s->windows = s->done = 0;
    openkaryon_window_snapshot_v1_snapshot(s->snapshot);
    sync_display(s);
    assert(s->done == 1 && s->available == available && s->windows == count);
    if (first)
        assert(!strcmp(s->titles[0], first));
}
static void check_sandbox(struct state *s)
{
    char path[108];
    snprintf(path, sizeof path, "%s/security.sock", getenv("XDG_RUNTIME_DIR"));
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    snprintf(address.sun_path, sizeof address.sun_path, "%s", path);
    assert(bind(listener, (struct sockaddr *)&address, sizeof address) == 0 && listen(listener, 1) == 0);
    int lifetime[2];
    assert(pipe(lifetime) == 0);
    struct wp_security_context_v1 *context
        = wp_security_context_manager_v1_create_listener(s->security, listener, lifetime[0]);
    wp_security_context_v1_set_sandbox_engine(context, "karyo-qa");
    wp_security_context_v1_set_app_id(context, "org.openkaryon.SandboxQA");
    wp_security_context_v1_set_instance_id(context, "disposable");
    wp_security_context_v1_commit(context);
    sync_display(s);
    close(listener);
    close(lifetime[0]);
    int child_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(connect(child_fd, (struct sockaddr *)&address, sizeof address) == 0);
    struct state child = { .display = wl_display_connect_to_fd(child_fd) };
    assert(child.display);
    discover(&child);
    assert(child.compositor && child.wm && !child.layout && !child.snapshot);
    assert(!child.security && !child.lock_manager);
    wl_display_disconnect(child.display);
    close(lifetime[1]);
    wp_security_context_v1_destroy(context);
    unlink(path);
    puts("PASS security-context clients cannot discover privileged desktop protocols");
}
static void locked_event(void *data, struct ext_session_lock_v1 *lock) { *(int *)data = 1; }
static void finished_event(void *data, struct ext_session_lock_v1 *lock) { assert(!"lock refused"); }
static const struct ext_session_lock_v1_listener lock_listener
    = { .locked = locked_event, .finished = finished_event };
int main(int argc, char **argv)
{
    alarm(25);
    int physical = argc == 2 && !strcmp(argv[1], "--keyboard");
    struct state s = { .display = wl_display_connect(NULL) };
    assert(s.display);
    discover(&s);
    assert(s.compositor && s.shm && s.seat && s.wm && s.layout && s.snapshot && s.security && s.lock_manager);
    snapshot(&s, 0, 1, NULL);
    openkaryon_keyboard_layout_v1_cycle(s.layout, s.seat);
    settle(&s); // Safe even when a headless compositor has no physical keyboard.
    struct window a = { 0 }, b = { 0 };
    make_window(&s, &a, "Karyo QA A");
    make_window(&s, &b, "Karyo QA B");
    snapshot(&s, 2, 1, "Karyo QA B");
    assert(!strcmp(s.titles[1], "Karyo QA A"));
    puts("PASS mapped toplevels appear in actual front-to-back stacking order");
    if (physical)
    {
        assert(s.layouts >= 2);
        int before = s.group;
        openkaryon_keyboard_layout_v1_cycle(s.layout, s.seat);
        settle(&s);
        assert(s.group == (before + 1) % s.layouts);
        openkaryon_keyboard_layout_v1_cycle(s.layout, s.seat);
        settle(&s);
        assert(s.group == (before + 2) % s.layouts);
        puts("PASS physical keyboard group changed through the desktop protocol");
    }
    xdg_toplevel_set_minimized(b.top);
    settle(&s);
    snapshot(&s, 1, 1, "Karyo QA A");
    puts("PASS minimized toplevels are excluded");
    check_sandbox(&s);
    int locked = 0;
    struct ext_session_lock_v1 *lock = ext_session_lock_manager_v1_lock(s.lock_manager);
    ext_session_lock_v1_add_listener(lock, &lock_listener, &locked);
    for (int i = 0; i < 20 && !locked; ++i)
        settle(&s);
    assert(locked);
    snapshot(&s, 0, 0, NULL);
    int before = s.group;
    openkaryon_keyboard_layout_v1_cycle(s.layout, s.seat);
    settle(&s);
    assert(s.group == before);
    ext_session_lock_v1_unlock_and_destroy(lock);
    settle(&s);
    snapshot(&s, 1, 1, "Karyo QA A");
    puts("PASS locked sessions hide windows and ignore layout changes; unlock restores snapshots");
    xdg_toplevel_destroy(b.top);
    xdg_surface_destroy(b.xdg);
    wl_surface_destroy(b.surface);
    xdg_toplevel_destroy(a.top);
    xdg_surface_destroy(a.xdg);
    wl_surface_destroy(a.surface);
    settle(&s);
    snapshot(&s, 0, 1, NULL);
    wl_display_disconnect(s.display);
    puts("PASS compositor protocol acceptance");
    return 0;
}
