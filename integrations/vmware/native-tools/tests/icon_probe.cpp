// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtkmm.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

// Isolate the real callback from VMware RPC code; its body is included unchanged.
class DnDUIX11 {
public:
   GdkDragContext *mDragCtx = nullptr;
   void OnGtkDragBegin(const Glib::RefPtr<Gdk::DragContext>& context);
};
#include "on-gtk-drag-begin.inc"

int main(int argc, char **argv)
{
   bool transparent = argc == 2 && std::strcmp(argv[1], "transparent") == 0;
   Gtk::Main app(argc, argv);
   Gtk::Window source;
   source.set_default_size(200, 200);
   source.show();
   DnDUIX11 handler;
   source.signal_drag_begin().connect(sigc::mem_fun(handler, &DnDUIX11::OnGtkDragBegin));
   for (int i = 0; i < 20; ++i) {
      while (Gtk::Main::events_pending()) Gtk::Main::iteration();
      usleep(10000);
   }
   auto targets = Gtk::TargetList::create(std::vector<Gtk::TargetEntry>());
   targets->add("text/uri-list");
   GdkEvent *event = gdk_event_new(GDK_MOTION_NOTIFY);
   event->motion.window = GDK_WINDOW(g_object_ref(source.get_window()->gobj()));
   event->motion.time = GDK_CURRENT_TIME;
   event->motion.x = 10;
   event->motion.y = 10;
   event->motion.state = GDK_BUTTON1_MASK;
   gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));
   auto context = source.drag_begin(targets, Gdk::ACTION_COPY | Gdk::ACTION_MOVE, 1, event);
   gdk_event_free(event);
   for (int i = 0; i < 20; ++i) {
      while (Gtk::Main::events_pending()) Gtk::Main::iteration();
      usleep(10000);
   }
   if (!context || handler.mDragCtx != context->gobj()) return 2;
   std::printf("Context retained; requested actions=copy|move, negotiated actions=%d\n",
               gdk_drag_context_get_actions(context->gobj()));
   Display *display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
   Window root = DefaultRootWindow(display), parent, returned_root, *children = nullptr;
   unsigned int count = 0;
   XQueryTree(display, root, &returned_root, &parent, &children, &count);
   Atom type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
   Atom dnd_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DND", False);
   int result = 4;
   for (unsigned int i = 0; i < count; ++i) {
      Atom actual_type;
      int format;
      unsigned long nitems, after;
      unsigned char *data = nullptr;
      XGetWindowProperty(display, children[i], type_atom, 0, 8, False, AnyPropertyType,
                         &actual_type, &format, &nitems, &after, &data);
      bool icon = data && format == 32 && nitems && ((Atom *)data)[0] == dnd_atom;
      if (data) XFree(data);
      if (!icon) continue;
      XWindowAttributes attributes;
      XGetWindowAttributes(display, children[i], &attributes);
      if (attributes.map_state != IsViewable) continue;
      unsigned long painted = 0;
      XImage *image = XGetImage(display, children[i], 0, 0, attributes.width,
                               attributes.height, AllPlanes, ZPixmap);
      if (!image) continue;
      for (int y = 0; y < image->height; ++y)
         for (int x = 0; x < image->width; ++x)
            if (XGetPixel(image, x, y) & 0xff000000UL) ++painted;
      XDestroyImage(image);
      std::printf("Drag image: %dx%d depth=%d nontransparent_pixels=%lu\n",
                  attributes.width, attributes.height, attributes.depth, painted);
      result = transparent ? (attributes.depth == 32 && painted == 0 ? 0 : 1)
                           : (attributes.width > 1 && painted > 0 ? 0 : 1);
      break;
   }
   if (children) XFree(children);
   gtk_drag_cancel(context->gobj());
   while (Gtk::Main::events_pending()) Gtk::Main::iteration();
   return result;
}
