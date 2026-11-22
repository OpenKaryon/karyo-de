// SPDX-License-Identifier: GPL-3.0-or-later
/* Private-display QA: read identities, optionally request close of one exact app ID. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "wlr-icons.h"
#include "ext-icons.h"

struct item { const char *protocol; char title[256], app[256]; };
static int wlr_seen, ext_seen, requested;
static const char *close_id;
static void title_w(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *s)
{ snprintf(((struct item *)data)->title, 256, "%s", s); }
static void app_w(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *s)
{ snprintf(((struct item *)data)->app, 256, "%s", s); }
static void output_w(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) {}
static void state_w(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *a) {}
static void done_w(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{ struct item *i=data; printf("wlr\t%s\t%s\n", i->title, i->app);
  if(close_id && !requested && !strcmp(close_id,i->app)) { zwlr_foreign_toplevel_handle_v1_close(h); requested=1; }
}
static void closed_w(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {}
static void parent_w(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct zwlr_foreign_toplevel_handle_v1 *p) {}
static const struct zwlr_foreign_toplevel_handle_v1_listener wlr_listener = {
    .title=title_w, .app_id=app_w, .output_enter=output_w, .output_leave=output_w,
    .state=state_w, .done=done_w, .closed=closed_w, .parent=parent_w
};
static void top_w(void *data, struct zwlr_foreign_toplevel_manager_v1 *m, struct zwlr_foreign_toplevel_handle_v1 *h)
{ struct item *i=calloc(1,sizeof(*i)); if(!i)exit(3); zwlr_foreign_toplevel_handle_v1_add_listener(h,&wlr_listener,i); }
static void finish_w(void *d, struct zwlr_foreign_toplevel_manager_v1 *m) {}
static const struct zwlr_foreign_toplevel_manager_v1_listener wlr_manager_listener = {.toplevel=top_w,.finished=finish_w};
static void title_e(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *s)
{ snprintf(((struct item *)data)->title,256,"%s",s); }
static void app_e(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *s)
{ snprintf(((struct item *)data)->app,256,"%s",s); }
static void identifier_e(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *s) {}
static void done_e(void *data, struct ext_foreign_toplevel_handle_v1 *h)
{ struct item *i=data; printf("ext\t%s\t%s\n",i->title,i->app); }
static void closed_e(void *d, struct ext_foreign_toplevel_handle_v1 *h) {}
static const struct ext_foreign_toplevel_handle_v1_listener ext_listener = {
    .title=title_e,.app_id=app_e,.identifier=identifier_e,.done=done_e,.closed=closed_e
};
static void top_e(void *data, struct ext_foreign_toplevel_list_v1 *m, struct ext_foreign_toplevel_handle_v1 *h)
{ struct item *i=calloc(1,sizeof(*i)); if(!i)exit(3); ext_foreign_toplevel_handle_v1_add_listener(h,&ext_listener,i); }
static void finish_e(void *d, struct ext_foreign_toplevel_list_v1 *m) {}
static const struct ext_foreign_toplevel_list_v1_listener ext_manager_listener = {.toplevel=top_e,.finished=finish_e};
static void global(void *data, struct wl_registry *r, uint32_t name, const char *interface, uint32_t version)
{
    if(!strcmp(interface,zwlr_foreign_toplevel_manager_v1_interface.name)) {
        struct zwlr_foreign_toplevel_manager_v1 *m=wl_registry_bind(r,name,&zwlr_foreign_toplevel_manager_v1_interface,version<3?version:3);
        zwlr_foreign_toplevel_manager_v1_add_listener(m,&wlr_manager_listener,NULL); wlr_seen=1;
    } else if(!strcmp(interface,ext_foreign_toplevel_list_v1_interface.name)) {
        struct ext_foreign_toplevel_list_v1 *m=wl_registry_bind(r,name,&ext_foreign_toplevel_list_v1_interface,1);
        ext_foreign_toplevel_list_v1_add_listener(m,&ext_manager_listener,NULL); ext_seen=1;
    }
}
static void removed(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener registry_listener = {.global=global,.global_remove=removed};
int main(int argc, char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--close") && argv[2][0])close_id=argv[2];
    else if(argc!=1)return 64;
    struct wl_display *display=wl_display_connect(NULL); if(!display)return 2;
    struct wl_registry *registry=wl_display_get_registry(display);
    wl_registry_add_listener(registry,&registry_listener,NULL);
    for(int i=0;i<4;i++)if(wl_display_roundtrip(display)<0)return 4;
    wl_display_disconnect(display);
    return !wlr_seen||!ext_seen?5:close_id&&!requested?6:0;
}
