#ifndef _VIEW_WIDGET_H_
#define _VIEW_WIDGET_H_

#include <gtk/gtk.h>

#include "wl-utils.h"

G_BEGIN_DECLS

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
  struct display *display;
  guint timeout_id;
  struct nested *nested;
};

struct _ViewWidget {
    GtkContainer parent_instance;

    /*< private >*/
    ViewWidgetPrivate *priv;
};

struct _ViewWidgetClass {
    GtkContainerClass parent_class;
};

GType view_widget_get_type (void);
GtkWidget *view_widget_new (void);

G_END_DECLS

#endif
