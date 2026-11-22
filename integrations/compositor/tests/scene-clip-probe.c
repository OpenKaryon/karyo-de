// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <drm_fourcc.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/addon.h>

#define CHECK(test, message) do { if (!(test)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, message); exit(2); \
} } while (0)

enum { WIDTH = 400, HEIGHT = 320, BODY_WIDTH = 100, BODY_HEIGHT = 80 };
static void (*set_clip)(struct wlr_scene_node *, const pixman_region32_t *);
static struct wl_display *display;
static struct wlr_output *output;
static struct wlr_scene *scene;
static struct wlr_scene_output *scene_output;
static struct wlr_scene_rect *background;
static struct wlr_scene_tree *outer, *inner;
static struct wlr_scene_buffer *body, *fullscreen;
static const char *directory;
static double scale;
static int transform;
static int origin_x = 30, origin_y = 30;
static bool cut_corners, center_hole, parent_clip, empty_clip, body_enabled = true, full_mode;
static bool sampled, last_direct;
static unsigned destroyed_buffers, destroyed_addons, phases;
static struct wl_listener sample_listener;

struct pixels {
    struct wlr_buffer base;
    uint32_t *data;
};

static void pixels_destroy(struct wlr_buffer *buffer)
{
    struct pixels *pixels = wl_container_of(buffer, pixels, base);
    free(pixels->data);
    free(pixels);
    ++destroyed_buffers;
}

static bool pixels_access(struct wlr_buffer *buffer, uint32_t flags,
        void **data, uint32_t *format, size_t *stride)
{
    (void)flags;
    struct pixels *pixels = wl_container_of(buffer, pixels, base);
    *data = pixels->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)buffer->width * 4;
    return true;
}

static void pixels_end(struct wlr_buffer *buffer) { (void)buffer; }
static const struct wlr_buffer_impl pixels_impl = {
    .destroy = pixels_destroy,
    .begin_data_ptr_access = pixels_access,
    .end_data_ptr_access = pixels_end,
};

static struct wlr_scene_buffer *green_buffer(struct wlr_scene_tree *parent,
        int width, int height)
{
    struct pixels *pixels = calloc(1, sizeof(*pixels));
    CHECK(pixels, "Allocate own pixel producer");
    pixels->data = malloc((size_t)width * height * sizeof(uint32_t));
    CHECK(pixels->data, "Allocate own pixel storage");
    for (int i = 0; i < width * height; ++i) pixels->data[i] = 0xff00ff00;
    wlr_buffer_init(&pixels->base, &pixels_impl, width, height);
    struct wlr_scene_buffer *buffer = wlr_scene_buffer_create(parent, &pixels->base);
    CHECK(buffer, "Create real scene buffer");
    // Keep the synthetic solid-color oracle exact at integer output scales.
    wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_NEAREST);
    pixman_region32_t opaque;
    pixman_region32_init_rect(&opaque, 0, 0, width, height);
    wlr_scene_buffer_set_opaque_region(buffer, &opaque);
    pixman_region32_fini(&opaque);
    wlr_buffer_drop(&pixels->base);
    return buffer;
}

static void sampled_output(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct wlr_scene_output_sample_event *event = data;
    CHECK(event->output == scene_output, "Sample belongs to private output");
    sampled = true;
    last_direct = event->direct_scanout;
}

static void watch_buffer(struct wlr_scene_buffer *buffer)
{
    sample_listener.notify = sampled_output;
    wl_signal_add(&buffer->events.output_sample, &sample_listener);
}

static bool expected_green(int x, int y)
{
    if (full_mode) {
        if (cut_corners && (x < 8 || x >= WIDTH-8) && (y < 8 || y >= HEIGHT-8)) return false;
        return true;
    }
    x -= origin_x;
    y -= origin_y;
    if (!body_enabled || empty_clip || x < 0 || y < 0 || x >= BODY_WIDTH || y >= BODY_HEIGHT) return false;
    if (cut_corners && (x < 6 || x >= BODY_WIDTH-6) && (y < 6 || y >= BODY_HEIGHT-6)) return false;
    if (center_hole && x >= 40 && x < 60 && y >= 30 && y < 50) return false;
    // The outer clip is local to outer, offset (10,10) from the body origin.
    if (parent_clip && (x >= 70 || y >= 60)) return false;
    return true;
}

static void save_frame(const char *name, const unsigned char *rgb)
{
    char path[1024];
    int count = snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    CHECK(count > 0 && (size_t)count < sizeof(path), "Output path fits");
    FILE *file = fopen(path, "wb");
    CHECK(file, "Open own output frame");
    CHECK(fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT) > 0, "Write PPM header");
    CHECK(fwrite(rgb, 3, WIDTH * HEIGHT, file) == WIDTH * HEIGHT, "Write output pixels");
    CHECK(fclose(file) == 0, "Close output frame");
}

static void phase(const char *name, int expected_direct, bool save)
{
    // Dispatch real headless presentation events before submitting another frame.
    CHECK(wl_event_loop_dispatch(wl_display_get_event_loop(display), 30) >= 0, "Dispatch headless output");
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    sampled = last_direct = false;
    CHECK(wlr_scene_output_build_state(scene_output, &state, NULL), "Build actual scene output state");
    CHECK(state.buffer, "Actual rendering produced a buffer");
    void *data;
    uint32_t format;
    size_t stride;
    CHECK(wlr_buffer_begin_data_ptr_access(state.buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ,
        &data, &format, &stride), "Read actual CPU renderer output");
    CHECK((format == DRM_FORMAT_XRGB8888 || format == DRM_FORMAT_ARGB8888) &&
        state.buffer->width == WIDTH && state.buffer->height == HEIGHT, "Known native output format/dimensions");
    unsigned char *rgb = malloc((size_t)WIDTH * HEIGHT * 3);
    CHECK(rgb, "Allocate output evidence");
    unsigned pixel_errors = 0, input_errors = 0;
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            double lx = (x + 0.5) / scale, ly = (y + 0.5) / scale;
            if (transform == WL_OUTPUT_TRANSFORM_90) {
                lx = (HEIGHT - y - 0.5) / scale;
                ly = (x + 0.5) / scale;
            }
            bool green = expected_green((int)floor(lx), (int)floor(ly));
            uint32_t wanted = green ? 0x00ff00 : full_mode ? 0 : 0x0000ff;
            uint32_t actual = ((uint32_t *)((unsigned char *)data + y * stride))[x] & 0xffffff;
            size_t offset = ((size_t)y * WIDTH + x) * 3;
            rgb[offset] = actual >> 16;
            rgb[offset+1] = actual >> 8;
            rgb[offset+2] = actual;
            if (actual != wanted) {
                if (pixel_errors < 2) fprintf(stderr, "%s pixel %d,%d: got %06x expected %06x\n", name, x, y, actual, wanted);
                ++pixel_errors;
            }
            struct wlr_scene_node *hit = wlr_scene_node_at(&scene->tree.node, lx, ly, NULL, NULL);
            struct wlr_scene_node *wanted_hit = green ? &(full_mode ? fullscreen : body)->node : &background->node;
            if (hit != wanted_hit) ++input_errors;
        }
    }
    if (save || pixel_errors || input_errors) save_frame(name, rgb);
    free(rgb);
    wlr_buffer_end_data_ptr_access(state.buffer);
    bool direct_ok = expected_direct < 0 || (sampled && last_direct == (expected_direct != 0));
    printf("{\"stage\":\"%s\",\"pixel_errors\":%u,\"input_errors\":%u,\"sampled\":%s,\"direct_scanout\":%s}\n",
        name, pixel_errors, input_errors, sampled ? "true" : "false", last_direct ? "true" : "false");
    fflush(stdout);
    CHECK(!pixel_errors && !input_errors && direct_ok, "Real render/input/scanout result differs");
    CHECK(wlr_output_commit_state(output, &state), "Commit actual headless output state");
    wlr_output_state_finish(&state);
    ++phases;
}

static void corners(struct wlr_scene_node *node, int width, int height, int radius)
{
    pixman_region32_t region, hole;
    pixman_region32_init_rect(&region, 0, 0, width, height);
    pixman_region32_init_rect(&hole, 0, 0, radius, radius);
    pixman_region32_union_rect(&hole, &hole, width-radius, height-radius, radius, radius);
    pixman_region32_union_rect(&hole, &hole, width-radius, 0, radius, radius);
    pixman_region32_union_rect(&hole, &hole, 0, height-radius, radius, radius);
    pixman_region32_subtract(&region, &region, &hole);
    set_clip(node, &region);
    // Mutation after the call must not mutate the library's private copy.
    pixman_region32_clear(&region);
    pixman_region32_fini(&region);
    pixman_region32_fini(&hole);
}

static void own_addon_destroy(struct wlr_addon *addon)
{
    wlr_addon_finish(addon);
    ++destroyed_addons;
}

static const struct wlr_addon_interface own_addon_impl = {
    .name = "karyo-scene-clip-qa", .destroy = own_addon_destroy,
};

static void abi(void)
{
    printf("{\"abi\":{\"node\":%zu,\"tree\":%zu,\"buffer\":%zu,\"rect\":%zu,"
        "\"parent\":%zu,\"link\":%zu,\"enabled\":%zu,\"x\":%zu,\"y\":%zu,"
        "\"events\":%zu,\"data\":%zu,\"addons\":%zu}}\n",
        sizeof(struct wlr_scene_node), sizeof(struct wlr_scene_tree),
        sizeof(struct wlr_scene_buffer), sizeof(struct wlr_scene_rect),
        offsetof(struct wlr_scene_node, parent), offsetof(struct wlr_scene_node, link),
        offsetof(struct wlr_scene_node, enabled), offsetof(struct wlr_scene_node, x),
        offsetof(struct wlr_scene_node, y), offsetof(struct wlr_scene_node, events),
        offsetof(struct wlr_scene_node, data), offsetof(struct wlr_scene_node, addons));
}

int main(int argc, char **argv)
{
    CHECK(argc == 5, "Arguments: output-directory scale transform baseline|positive");
    CHECK(getuid() != 0 && getuid() == geteuid(), "Run as an ordinary QA user");
    directory = argv[1];
    scale = strtod(argv[2], NULL);
    transform = atoi(argv[3]);
    bool baseline = strcmp(argv[4], "baseline") == 0;
    CHECK(scale == 1 || scale == 2, "Known integer output scale");
    CHECK(transform == WL_OUTPUT_TRANSFORM_NORMAL || transform == WL_OUTPUT_TRANSFORM_90, "Known output transform");
    set_clip = (void (*)(struct wlr_scene_node *, const pixman_region32_t *))dlsym(RTLD_DEFAULT, "wlr_scene_node_set_clip_region");
    CHECK(baseline ? !set_clip : set_clip != NULL, "Expected native clip API presence");
    abi();
    display = wl_display_create();
    CHECK(display, "Create private Wayland display");
    struct wlr_backend *backend = wlr_headless_backend_create(wl_display_get_event_loop(display));
    CHECK(backend, "Create real headless backend");
    struct wlr_renderer *renderer = wlr_pixman_renderer_create();
    CHECK(renderer, "Create actual Pixman renderer");
    struct wlr_allocator *allocator = wlr_allocator_autocreate(backend, renderer);
    CHECK(allocator, "Create actual native allocator");
    output = wlr_headless_add_output(backend, WIDTH, HEIGHT);
    CHECK(output && wlr_output_init_render(output, allocator, renderer), "Initialize private render output");
    scene = wlr_scene_create();
    CHECK(scene, "Create native scene");
    scene_output = wlr_scene_output_create(scene, output);
    CHECK(scene_output && wlr_backend_start(backend), "Start private backend");
    struct wlr_output_state initial;
    wlr_output_state_init(&initial);
    wlr_output_state_set_enabled(&initial, true);
    wlr_output_state_set_custom_mode(&initial, WIDTH, HEIGHT, 60000);
    wlr_output_state_set_scale(&initial, scale);
    wlr_output_state_set_transform(&initial, transform);
    wlr_output_state_set_render_format(&initial, DRM_FORMAT_XRGB8888);
    CHECK(wlr_output_commit_state(output, &initial), "Enable private output");
    wlr_output_state_finish(&initial);
    int width, height;
    wlr_output_effective_resolution(output, &width, &height);
    background = wlr_scene_rect_create(&scene->tree, width, height, (float[4]){0,0,1,1});
    outer = wlr_scene_tree_create(&scene->tree);
    inner = wlr_scene_tree_create(outer);
    CHECK(background && outer && inner, "Create nested native scene");
    wlr_scene_node_set_position(&outer->node, 20, 20);
    wlr_scene_node_set_position(&inner->node, 10, 10);
    body = green_buffer(inner, BODY_WIDTH, BODY_HEIGHT);
    watch_buffer(body);
    struct wlr_addon addon;
    wlr_addon_init(&addon, &inner->node.addons, &addon, &own_addon_impl);
    for (int i = 0; i < 6; ++i) phase("warmup", -1, false);
    phase("unclipped", -1, true);
    if (!baseline) {
        corners(&inner->node, BODY_WIDTH, BODY_HEIGHT, 6);
        cut_corners = true;
        phase("copied-child-clip", -1, true);
        pixman_region32_t region, hole;
        pixman_region32_init_rect(&region, 0, 0, BODY_WIDTH, BODY_HEIGHT);
        pixman_region32_init_rect(&hole, 40, 30, 20, 20);
        pixman_region32_subtract(&region, &region, &hole);
        set_clip(&inner->node, &region);
        pixman_region32_fini(&hole);
        pixman_region32_fini(&region);
        cut_corners = false; center_hole = true;
        phase("complex-hole", -1, true);
        corners(&inner->node, BODY_WIDTH, BODY_HEIGHT, 6);
        cut_corners = true; center_hole = false;
        phase("restore-corners", -1, true);
        pixman_region32_init_rect(&region, 10, 10, 70, 60);
        set_clip(&outer->node, &region);
        pixman_region32_fini(&region);
        parent_clip = true;
        phase("nested-parent-clip", -1, true);
        wlr_scene_node_set_position(&outer->node, 26, 24);
        origin_x = 36; origin_y = 34;
        phase("moved-nested-clips", -1, true);
        set_clip(&outer->node, NULL);
        parent_clip = false;
        phase("clear-parent", -1, true);
        struct wlr_scene_tree *alternative = wlr_scene_tree_create(&scene->tree);
        CHECK(alternative, "Create alternate native parent");
        wlr_scene_node_set_position(&alternative->node, 40, 50);
        pixman_region32_init_rect(&region, 10, 10, 70, 60);
        set_clip(&alternative->node, &region);
        pixman_region32_fini(&region);
        wlr_scene_node_reparent(&inner->node, alternative);
        origin_x = 50; origin_y = 60; parent_clip = true;
        phase("reparented-clip", -1, true);
        wlr_scene_node_reparent(&inner->node, outer);
        origin_x = 36; origin_y = 34; parent_clip = false;
        phase("reparented-back", -1, true);
        wlr_scene_node_destroy(&alternative->node);
        pixman_region32_init(&region);
        set_clip(&inner->node, &region);
        pixman_region32_fini(&region);
        empty_clip = true;
        phase("empty-child", -1, true);
        set_clip(&inner->node, NULL);
        empty_clip = cut_corners = false;
        phase("clear-child", -1, true);
    }
    if (!baseline) {
        corners(&inner->node, BODY_WIDTH, BODY_HEIGHT, 6);
        cut_corners = true;
        phase("mask-before-hide", -1, true);
    }
    wlr_scene_node_set_enabled(&inner->node, false);
    body_enabled = false;
    phase("disabled-child", -1, true);
    wlr_scene_node_set_enabled(&inner->node, true);
    body_enabled = true;
    phase("enabled-child", -1, true);
    wl_list_remove(&sample_listener.link);
    if (!baseline) corners(&inner->node, BODY_WIDTH, BODY_HEIGHT, 6);
    wlr_scene_node_destroy(&outer->node);
    CHECK(destroyed_addons == 1 && destroyed_buffers == 1, "Masked subtree destroys both private and independent resources");
    body_enabled = false;
    phase("destroyed-child", -1, true);
    if (scale == 1 && transform == WL_OUTPUT_TRANSFORM_NORMAL) {
        wlr_scene_rect_set_color(background, (float[4]){0,0,0,1});
        fullscreen = green_buffer(&scene->tree, WIDTH, HEIGHT);
        watch_buffer(fullscreen);
        full_mode = true;
        cut_corners = false;
        phase("scanout-unclipped", 1, true);
        if (!baseline) {
            corners(&fullscreen->node, WIDTH, HEIGHT, 8);
            cut_corners = true;
            phase("scanout-clipped", 0, true);
            set_clip(&fullscreen->node, NULL);
            cut_corners = false;
            phase("scanout-cleared", 1, true);
        }
        wl_list_remove(&sample_listener.link);
    }
    wlr_scene_node_destroy(&scene->tree.node);
    wlr_backend_destroy(backend);
    wlr_allocator_destroy(allocator);
    wlr_renderer_destroy(renderer);
    wl_display_destroy(display);
    CHECK(destroyed_buffers == (full_mode ? 2u : 1u), "All own pixel storage released");
    printf("{\"accepted\":true,\"baseline\":%s,\"phases\":%u,\"destroyed_addons\":%u,\"destroyed_buffers\":%u}\n",
        baseline ? "true" : "false", phases, destroyed_addons, destroyed_buffers);
    return 0;
}
