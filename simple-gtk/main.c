#include <gtk/gtk.h>

/* ------------- Widget ------------ */

#define TYPE_VIEW_WIDGET                (view_widget_get_type())
#define VIEW_WIDGET(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj), TYPE_VIEW_WIDGET, ViewWidget))
#define VIEW_WIDGET_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass),  TYPE_VIEW_WIDGET, ViewWidgetClass))
#define IS_VIEW_WIDGET_(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), TYPE_VIEW_WIDGET))
#define IS_VIEW_WIDGET_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass),  TYPE_VIEW_WIDGET))
#define VIEW_WIDGET_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS((obj),  TYPE_VIEW_WIDGET, ViewWidgetClass))

typedef struct _ViewWidget       ViewWidget;
typedef struct _ViewWidgetClass  ViewWidgetClass;

struct _ViewWidget {
    GtkContainer parent_instance;
};

struct _ViewWidgetClass {
    GtkContainerClass parent_class;
};

G_DEFINE_TYPE(ViewWidget, view_widget, GTK_TYPE_CONTAINER)

static void
view_widget_init (ViewWidget* vw)
{
}

static gboolean
view_widget_draw (GtkWidget* widget, cairo_t* cr)
{
  GtkAllocation allocation;
  gtk_widget_get_allocation (widget, &allocation);
  cairo_set_source_rgb (cr, 1.0, 0.0, 0.0);
  cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
  cairo_fill (cr);
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
  GtkWidgetClass* widgetClass = GTK_WIDGET_CLASS(klass);
  widgetClass->realize = view_widget_realize;
  widgetClass->draw = view_widget_draw;
}

static GtkWidget *
view_widget_new (void)
{
  ViewWidget* vw = VIEW_WIDGET (g_object_new (TYPE_VIEW_WIDGET, NULL));
  return GTK_WIDGET(vw);
}

/* ------------- Program ---------------- */

int main(int argc, char *argv[])
{
  gtk_init (&argc, &argv);

  GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);

  GtkWidget *vw = view_widget_new ();
  gtk_container_add (GTK_CONTAINER (window), vw);

  gtk_widget_show (vw);
  gtk_widget_show (window);

  gtk_main ();

  return 0;
}
