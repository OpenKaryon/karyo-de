// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual upstream DragDetWnd implementation in an isolated X server.
#include "dragDetWndX11.h"
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main(int argc, char **argv)
{
   const bool rgba = argc == 2 && std::strcmp(argv[1], "rgba") == 0;
   const bool legacy = argc == 2 && std::strcmp(argv[1], "legacy-rgb") == 0;
   Gtk::Main app(argc, argv);
   DragDetWnd detection;
   detection.SetGeometry(0, 0, 188, 60);
   detection.Show();
   for (int i = 0; i < 20; ++i) {
      while (Gtk::Main::events_pending()) Gtk::Main::iteration();
      usleep(10000);
   }
   auto window = detection.GetWnd()->get_window();
   Display *display = GDK_DISPLAY_XDISPLAY(window->get_display()->gobj());
   Window xid = GDK_WINDOW_XID(window->gobj());
   XWindowAttributes attributes;
   XGetWindowAttributes(display, xid, &attributes);
   if (legacy) {
      std::printf("Legacy detection: class=%d depth=%d mapped=%d\n", attributes.c_class, attributes.depth, attributes.map_state);
      return attributes.c_class == InputOutput && attributes.depth == 24 && attributes.map_state == IsViewable ? 0 : 1;
   }
   if (!rgba) {
      std::printf("X11: class=%d depth=%d\n", attributes.c_class, attributes.depth);
      return attributes.c_class == InputOnly ? 0 : 1;
   }
   if (attributes.depth != 32 || attributes.map_state != IsViewable) {
      std::fprintf(stderr, "Expected mapped ARGB window, got depth=%d mapped=%d\n",
                   attributes.depth, attributes.map_state);
      return 1;
   }
   int count = 0, ordering = 0;
   XRectangle *input = XShapeGetRectangles(display, xid, ShapeInput, &count, &ordering);
   bool receives_input = false;
   for (int i = 0; i < count; ++i) {
      receives_input |= input[i].width > 0 && input[i].height > 0;
   }
   if (input) XFree(input);
   XImage *image = XGetImage(display, xid, 0, 0, attributes.width, attributes.height,
                            AllPlanes, ZPixmap);
   if (!image) return 1;
   unsigned long painted = 0;
   for (int y = 0; y < image->height; ++y) {
      for (int x = 0; x < image->width; ++x) {
         if (XGetPixel(image, x, y) & 0xff000000UL) ++painted;
      }
   }
   XDestroyImage(image);
   std::printf("Wayland detection: depth=%d mapped=yes input=%s nontransparent_pixels=%lu\n",
               attributes.depth, receives_input ? "yes" : "no", painted);
   detection.Hide();
   return receives_input && painted == 0 ? 0 : 1;
}
