#ifndef __EVENT_SOURCE_H__
#define __EVENT_SOURCE_H__

#include <wayland-client.h>
#include <glib.h>

GSource *
wayland_display_source_new (struct wl_display *display);

#endif
