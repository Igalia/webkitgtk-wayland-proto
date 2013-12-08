#include "ViewWidget.h"

#include <sys/time.h>

#include <stdio.h>
#include <assert.h> 
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>

#define ENABLE_VW_LOG         0 
#define ENABLE_FPS            0
#define ENABLE_TIME_INFO      0

/* Use g_timeout_add to queue next frame draw events */
#define USE_TIMEOUT_FOR_NEXT_FRAME   1
#define TIMEOUT_TARGET_FPS           60.0f
#define TIMEOUT_PRIORITY             GDK_PRIORITY_EVENTS

#if ENABLE_VW_LOG
#define VW_LOG(s) (printf ("VW: %s\n", s))
#else
#define VW_LOG(s) ;
#endif

#define VW_WAR(s) (printf ("VW: [WARNING] %s\n", s))

G_DEFINE_TYPE(ViewWidget, view_widget, GTK_TYPE_CONTAINER)

static void
post_frame_callbacks (struct nested *nested)
{
	struct nested_frame_callback *nc, *next;
	wl_list_for_each_safe(nc, next, &nested->frame_callback_list, link) {
		wl_callback_send_done(nc->resource, 0);
		wl_resource_destroy(nc->resource);
	}
	wl_list_init(&nested->frame_callback_list);
	wl_display_flush_clients(nested->child_display);
}

static void
view_widget_init (ViewWidget* vw)
{
  ViewWidgetPrivate* priv =
    G_TYPE_INSTANCE_GET_PRIVATE (vw, TYPE_VIEW_WIDGET, ViewWidgetPrivate);
  memset (priv, 0, sizeof (ViewWidgetPrivate));

  vw->priv = priv;
}

static void
view_widget_dispose (GObject* object)
{
}

static void
view_widget_finalize (GObject* object)
{
}

static void
view_widget_realize(GtkWidget* widget)
{
  gtk_widget_set_realized (widget, TRUE);

  GtkAllocation allocation;
  gtk_widget_get_allocation (widget, &allocation);

  GdkWindowAttr attributes;
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = allocation.x;
  attributes.y = allocation.y;
  attributes.width = allocation.width;
  attributes.height = allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK | GDK_EXPOSURE_MASK;

  gint attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;
  GdkWindow* window = gdk_window_new (gtk_widget_get_parent_window (widget),
                                      &attributes, attributes_mask);

  gtk_widget_set_window (widget, window);
  gdk_window_set_user_data (window, widget);

  gtk_style_context_set_background (
    gtk_widget_get_style_context (widget), window);
}

static void
view_widget_map(GtkWidget* widget)
{
  GTK_WIDGET_CLASS (view_widget_parent_class)->map (widget);
}

static float
time_diff (struct timeval *cur_time, struct timeval *last_time)
{
  suseconds_t time_diff_sec, time_diff_usec;
  time_diff_sec = cur_time->tv_sec - last_time->tv_sec;
  time_diff_usec = cur_time->tv_usec - last_time->tv_usec;
  return (time_diff_sec * 1000000L +  time_diff_usec) / (float) 1000L;
}

static gboolean
next_frame_cb (GtkWidget *widget)
{
  // printf ("server: next_frame_cb\n");
  VIEW_WIDGET (widget)->priv->timeout_id = 0;
  gtk_widget_queue_draw (widget);
  return FALSE;
}

static void
draw (ViewWidget *vw, cairo_t *cr)
{
  GtkAllocation allocation;

  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  if (!vw->priv->nested || !vw->priv->nested->nested_surface) {
    return;
  }

  /* FIXME: Why do  I need this? */
  gtk_widget_get_allocation (GTK_WIDGET (vw), &allocation);
  cairo_surface_mark_dirty (vw->priv->nested->nested_surface->cairo_surface );
  cairo_set_source_surface(cr, vw->priv->nested->nested_surface->cairo_surface, 0, 0);
  cairo_rectangle(cr, 0, 0,	allocation.width, allocation.height);
  cairo_fill(cr);

  /* FIXME: in the original example this is done in the main surface's
    frame callback, but here it seems to do the trick too */
  post_frame_callbacks (vw->priv->nested);
}

static gboolean
view_widget_draw (GtkWidget* widget, cairo_t* cr)
{
  static int frame_count = 0;
  static struct timeval start_time;
  static struct timeval prev_frame_time;
  static float total_elapsed_time_ms;

  double draw_time_ms = 0;
  unsigned int next_frame_delay;
  struct timeval last_time;
  struct timeval cur_time;

  // printf ("server: widget: draw\n");

  if (frame_count == 0) {
    gettimeofday (&start_time, NULL);
    total_elapsed_time_ms = 0.0f;
    prev_frame_time = start_time;
  } else {
    gettimeofday (&cur_time, NULL);
    total_elapsed_time_ms = time_diff (&cur_time, &start_time);
#if ENABLE_VW_LOG
    printf ("Real frame time: %.2f\n", time_diff (&cur_time, &prev_frame_time));
#endif
    prev_frame_time = cur_time;
  }

  /* Draw */
  ViewWidget *vw = VIEW_WIDGET (widget);
  gettimeofday (&last_time, NULL);
  draw (vw, cr);
  GTK_WIDGET_CLASS (view_widget_parent_class)->draw (widget, cr);
  gettimeofday (&cur_time, NULL);
  frame_count++;

  /* Compute draw time */
  draw_time_ms = time_diff (&cur_time, &last_time);

#if ENABLE_FPS
  if (total_elapsed_time_ms > 2000) {
    printf ("FPS INFO: %u frames in %.2f seconds => %.2f fps\n",
             frame_count,
             total_elapsed_time_ms / 1000.0f,
             frame_count * 1000.0f / total_elapsed_time_ms);
    frame_count = 0;
  }
#endif

#if ENABLE_TIME_INFO
  printf ("Draw time for frame %d (ms): %.2f\n", frame_count, draw_time_ms);
#endif

#if USE_TIMEOUT_FOR_NEXT_FRAME
  next_frame_delay = MAX (1000.0f / TIMEOUT_TARGET_FPS - draw_time_ms, 0);
#if ENABLE_TIME_INFO
  printf ("Scheduling next frame in %u ms\n", next_frame_delay);
#endif
//  printf ("server: widget: set timeout for next frame in %d ms\n", next_frame_delay);
  vw->priv->timeout_id =
    g_timeout_add_full (TIMEOUT_PRIORITY, next_frame_delay,
                       (GSourceFunc) next_frame_cb, widget, 0);
#else
  gtk_widget_queue_draw (widget);
#endif

  return FALSE;
}

static void
view_widget_size_allocate(GtkWidget* widget, GtkAllocation* allocation)
{
  /*
  GtkAllocation oldAllocation;
  gtk_widget_get_allocation(widget, &oldAllocation);
  gboolean sizeChanged = allocation->width != oldAllocation.width ||
                         allocation->height != oldAllocation.height;

  printf ("server: size changed: %d, %d\n", allocation->width, allocation->height);
  if (sizeChanged) {
    printf ("server: resizing egl window: not implemented\n");
  }
  */

  GTK_WIDGET_CLASS(view_widget_parent_class)->size_allocate(widget,
                                                            allocation);
}

static void
view_widget_get_preferred_width (GtkWidget* widget, gint* minimum, gint* natural)
{
  *minimum = *natural = 250;
}

static void
view_widget_get_preferred_height (GtkWidget* widget, gint* minimum, gint* natural)
{
  *minimum = *natural = 250;
}

static void
view_widget_class_init (ViewWidgetClass* klass)
{
  GObjectClass* objectClass = G_OBJECT_CLASS(klass);
  objectClass->dispose = view_widget_dispose;
  objectClass->finalize = view_widget_finalize;

  GtkWidgetClass* widgetClass = GTK_WIDGET_CLASS(klass);
  widgetClass->realize = view_widget_realize;
  widgetClass->draw = view_widget_draw;
  widgetClass->size_allocate = view_widget_size_allocate;
  widgetClass->get_preferred_width = view_widget_get_preferred_width;
  widgetClass->get_preferred_height = view_widget_get_preferred_height;

  // widgetClass->map = view_widget_map;

  g_type_class_add_private(klass, sizeof(ViewWidgetPrivate));
}

GtkWidget *
view_widget_new (void)
{
  ViewWidget* vw = VIEW_WIDGET(g_object_new(TYPE_VIEW_WIDGET, NULL));
  vw->priv->display = wlu_display_create (GTK_WIDGET (vw));
  vw->priv->nested = wlu_nested_compositor_create (vw->priv->display, GTK_WIDGET (vw));
  return GTK_WIDGET(vw);
}
