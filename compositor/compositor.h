#ifndef __COMPOSITOR_H__
#define __COMPOSITOR_H__

#include <wayland-client.h>
#include <wayland-egl.h>
#include <gtk/gtk.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cairo.h>
#include <cairo-gl.h>

struct Display {
  /* GDK display */
  GdkDisplay *gdk_display;

  /* Wayland display */
  struct wl_display *wl_display;

  /* EGL display */
  EGLDisplay egl_display;
  EGLConfig egl_config;
  EGLContext egl_ctx;
  cairo_device_t *egl_device;
};

struct NestedSurface;

struct Compositor {
  struct Display *display;
  struct wl_display *child_display;
  struct NestedSurface *nested_surface;
  struct wl_list frame_callback_list;
};

struct NestedSurface {
  struct wl_resource *buffer_resource;
  struct Compositor *compositor;
  EGLImageKHR *image;
  GLuint texture;
  struct wl_list link;
  cairo_surface_t *cairo_surface;
};

struct NestedFrameCallback {
  struct wl_resource *resource;
  struct wl_list link;
};

struct Compositor *compositor_create (struct Display *);

#endif
