#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <wayland-server.h>
#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>

#include "compositor.h"
#include "os-compatibility.h"

/* ------------- Misc -------------- */

static void
init_egl (struct Display *d)
{
  EGLint major, minor;
  EGLint n;
  int ret;

  static const EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };
  static const EGLint egl_cfg_attribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, 1,
    EGL_DEPTH_SIZE, 1,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };

  d->egl_display = eglGetDisplay (d->wl_display);
  assert(d->egl_display);

  ret = eglInitialize(d->egl_display, &major, &minor);
  assert(ret == EGL_TRUE);

  eglBindAPI(EGL_OPENGL_ES_API);
  assert(ret == EGL_TRUE);

  ret = eglChooseConfig(d->egl_display, egl_cfg_attribs, &d->egl_config, 1, &n);
  assert(ret && n == 1);

  d->egl_ctx = eglCreateContext(d->egl_display, d->egl_config, EGL_NO_CONTEXT, context_attribs);
  assert(d->egl_ctx);

  ret = eglMakeCurrent(d->egl_display, NULL, NULL, d->egl_ctx);
  assert(ret == EGL_TRUE);

  d->egl_device = cairo_egl_device_create(d->egl_display, d->egl_ctx);
  assert (cairo_device_status(d->egl_device) == CAIRO_STATUS_SUCCESS);
}

static struct Display *
display_create (void)
{
  GdkDisplay *gdk_display =
    gdk_display_manager_get_default_display (gdk_display_manager_get ());

  struct Display *d = g_new0 (struct Display, 1);
  d->gdk_display = gdk_display;
  d->wl_display =  gdk_wayland_display_get_wl_display (gdk_display);

  if (!d->wl_display) {
    fprintf (stderr, "failed to connect to Wayland display\n");
    g_free (d);
    return NULL;
  }

  init_egl (d);

  return d;
}

/* ------------- Widget ------------ */

#define TYPE_VIEW_WIDGET                (view_widget_get_type())
#define VIEW_WIDGET(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj), TYPE_VIEW_WIDGET, ViewWidget))
#define VIEW_WIDGET_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass),  TYPE_VIEW_WIDGET, ViewWidgetClass))
#define IS_VIEW_WIDGET_(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), TYPE_VIEW_WIDGET))
#define IS_VIEW_WIDGET_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass),  TYPE_VIEW_WIDGET))
#define VIEW_WIDGET_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS((obj),  TYPE_VIEW_WIDGET, ViewWidgetClass))

typedef struct _ViewWidget       ViewWidget;
typedef struct _ViewWidgetClass  ViewWidgetClass;

typedef struct _ViewWidgetPrivate ViewWidgetPrivate;
struct  _ViewWidgetPrivate {
  struct Display *display;
  struct Compositor *compositor;
};

struct _ViewWidget {
  GtkContainer parent_instance;
  ViewWidgetPrivate *priv;
};

struct _ViewWidgetClass {
  GtkContainerClass parent_class;
};

G_DEFINE_TYPE(ViewWidget, view_widget, GTK_TYPE_CONTAINER)

static void
view_widget_init (ViewWidget* vw)
{
  ViewWidgetPrivate* priv = G_TYPE_INSTANCE_GET_PRIVATE (vw, TYPE_VIEW_WIDGET, ViewWidgetPrivate);
  memset (priv, 0, sizeof (ViewWidgetPrivate));
  vw->priv = priv;
}

static void
draw_dummy (GtkWidget *widget, cairo_t *cr)
{
  GtkAllocation allocation;
  gtk_widget_get_allocation (widget, &allocation);
  cairo_set_source_rgb (cr, 0.0, 1.0, 0.0);
  cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
  cairo_fill (cr);
}

static void
draw (GtkWidget *widget, cairo_t *cr)
{
  cairo_surface_t *surface;
  ViewWidget *vw = VIEW_WIDGET (widget);
  GtkAllocation allocation;

  if (!vw->priv->compositor || !vw->priv->compositor->nested_surface)
    return;

  surface = vw->priv->compositor->nested_surface->cairo_surface;
  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  gtk_widget_get_allocation (widget, &allocation);
  cairo_surface_mark_dirty (surface);
  cairo_set_source_surface (cr, surface, 0, 0);
  cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
  cairo_fill (cr);
}

static gboolean
view_widget_draw (GtkWidget* widget, cairo_t* cr)
{
  draw (widget, cr);
  ViewWidget *vw = VIEW_WIDGET (widget);

  struct NestedFrameCallback *nc, *next;
  wl_list_for_each_safe(nc, next, &vw->priv->compositor->frame_callback_list, link) {
    wl_callback_send_done(nc->resource, 0);
    wl_resource_destroy(nc->resource);
  }
  wl_list_init(&vw->priv->compositor->frame_callback_list);
  wl_display_flush_clients(vw->priv->compositor->child_display);

  gtk_widget_queue_draw (widget);

  return FALSE;
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
}

static void
view_widget_class_init (ViewWidgetClass* klass)
{
  GtkWidgetClass* widgetClass = GTK_WIDGET_CLASS (klass);
  widgetClass->realize = view_widget_realize;
  widgetClass->draw = view_widget_draw;

  g_type_class_add_private (klass, sizeof (ViewWidgetPrivate));
}

static GtkWidget *
view_widget_new (void)
{
  ViewWidget* vw = VIEW_WIDGET (g_object_new (TYPE_VIEW_WIDGET, NULL));
  vw->priv->display = display_create ();
  vw->priv->compositor = compositor_create (vw->priv->display);
  return GTK_WIDGET(vw);
}

/* ------------- Program ---------------- */

static void
launch_client (ViewWidget *vw, const char *path)
{
  int sv[2];
  pid_t pid;

  printf ("server: launching client\n");

  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
    fprintf(stderr, "launch_client: "
            "socketpair failed while launching '%s': %m\n", path);
    exit(-1);
  }

  pid = fork();
  if (pid == -1) {
    close(sv[0]);
    close(sv[1]);
    fprintf(stderr, "launch_client: "
            "fork failed while launching '%s': %m\n", path);
    exit(-1);
  }

  if (pid == 0) {
    int clientfd;
    char s[32];

    clientfd = dup(sv[1]);
    if (clientfd == -1) {
      fprintf(stderr, "compositor: dup failed: %m\n");
      exit(-1);
    }

    snprintf(s, sizeof s, "%d", clientfd);
    setenv("WAYLAND_SOCKET", s, 1);

    execl(path, path, NULL);

    fprintf(stderr, "compositor: executing '%s' failed: %m\n", path);
    exit(-1);
  }

  close(sv[1]);

  if (!wl_client_create(vw->priv->compositor->child_display, sv[0])) {
    close(sv[0]);
    fprintf(stderr, "launch_client: "
            "wl_client_create failed while launching '%s'.\n", path);
    exit(-1);
  }

  printf ("server: launch client finished\n");
}

int main(int argc, char *argv[])
{
  gtk_init (&argc, &argv);

  GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);

  GtkWidget *vw = view_widget_new ();
  gtk_container_add (GTK_CONTAINER (window), vw);

  gtk_widget_show (vw);
  gtk_widget_show (window);

  launch_client (VIEW_WIDGET (vw), "client");

  gtk_main ();

  return 0;
}
