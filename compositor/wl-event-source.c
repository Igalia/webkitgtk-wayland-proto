#include "wl-event-source.h"

#include <wayland-server.h>
#include <gdk/gdk.h>

typedef struct _WaylandEventSource {
  GSource source;
  GPollFD pfd;
  struct wl_display *display;
} WaylandEventSource;

static gboolean
g_wl_event_source_prepare (GSource *base, gint *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
g_wl_event_source_check(GSource *base)
{
  WaylandEventSource *source = (WaylandEventSource *) base;
  return source->pfd.revents;
}

static gboolean
g_wl_event_source_dispatch(GSource *base,
			  GSourceFunc callback,
			  gpointer data)
{
  WaylandEventSource *source = (WaylandEventSource *) base;
  struct wl_display *display = source->display;
	struct wl_event_loop *loop;

  if (source->pfd.revents & G_IO_IN) {
  	loop = wl_display_get_event_loop (display);
  	wl_event_loop_dispatch (loop, -1);
  	wl_display_flush_clients (display);
    source->pfd.revents = 0;
  }

  if (source->pfd.revents & (G_IO_ERR | G_IO_HUP)) {
    g_error ("Lost connection to wayland compositor");
  }

  return TRUE;
}

static void
g_wl_event_source_finalize (GSource *source)
{

}

static GSourceFuncs wl_glib_source_funcs = {
  g_wl_event_source_prepare,
  g_wl_event_source_check,
  g_wl_event_source_dispatch,
  g_wl_event_source_finalize
};

GSource *
compositor_display_source_new (struct wl_display *display)
{
  GSource *source;
  WaylandEventSource *wl_source;
  char *name;
  struct wl_event_loop *loop;

  source = g_source_new (&wl_glib_source_funcs, sizeof (WaylandEventSource));
  name = g_strdup_printf ("Wayland Child Event source");
  g_source_set_name (source, name);
  g_free (name);
  wl_source = (WaylandEventSource *) source;

  wl_source->display = display;
	loop = wl_display_get_event_loop (display);
  wl_source->pfd.fd = wl_event_loop_get_fd (loop);
  wl_source->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
  g_source_add_poll (source, &wl_source->pfd);

  g_source_set_priority (source, GDK_PRIORITY_EVENTS);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  printf ("server: child display fd: %d\n", wl_source->pfd.fd);

  return source;
}
