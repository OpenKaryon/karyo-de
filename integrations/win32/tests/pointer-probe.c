// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include "pointer.h"
static struct zwlr_virtual_pointer_manager_v1 *manager;
static void global(void *data,struct wl_registry *registry,uint32_t name,const char *interface,uint32_t version)
{
    (void)data;(void)version;
    if(!strcmp(interface,zwlr_virtual_pointer_manager_v1_interface.name))
        manager=wl_registry_bind(registry,name,&zwlr_virtual_pointer_manager_v1_interface,1);
}
static void removed(void *data,struct wl_registry *registry,uint32_t name)
{(void)data;(void)registry;(void)name;}
static const struct wl_registry_listener listener={global,removed};
static uint32_t timestamp(void)
{
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint32_t)((uint64_t)now.tv_sec*1000+now.tv_nsec/1000000);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    struct wl_display *display=wl_display_connect(NULL);if(!display)return 1;
    struct wl_registry *registry=wl_display_get_registry(display);
    wl_registry_add_listener(registry,&listener,NULL);
    if(wl_display_roundtrip(display)<0 || !manager)return 2;
    struct zwlr_virtual_pointer_v1 *pointer=zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager,NULL);
    if(wl_display_roundtrip(display)<0)return 3;
    puts("READY");char line[128];
    while(fgets(line,sizeof(line),stdin)) {
        unsigned int x,y,width,height,pressed;
        if(sscanf(line,"move %u %u %u %u",&x,&y,&width,&height)==4 && width && height && x<=width && y<=height)
            zwlr_virtual_pointer_v1_motion_absolute(pointer,timestamp(),x,y,width,height);
        else if(sscanf(line,"button %u",&pressed)==1 && pressed<=1)
            zwlr_virtual_pointer_v1_button(pointer,timestamp(),272,pressed);
        else if(!strcmp(line,"quit\n"))break;
        else return 4;
        zwlr_virtual_pointer_v1_frame(pointer);
        if(wl_display_roundtrip(display)<0)return 5;
        puts("OK");
    }
    zwlr_virtual_pointer_v1_destroy(pointer);
    zwlr_virtual_pointer_manager_v1_destroy(manager);wl_registry_destroy(registry);
    wl_display_disconnect(display);return 0;
}
