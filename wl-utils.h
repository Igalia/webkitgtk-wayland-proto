#ifndef __WAYLAND_UTILS_H__
#define __WAYLAND_UTILS_H__

#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cairo.h>
#include <cairo-gl.h>
#include <sys/epoll.h>
#include <gtk/gtk.h>

#include "os-compatibility.h"

struct nested;
struct task;
struct egl_window_surface;

struct task {
	void (*run)(struct task *task, uint32_t events);
	struct wl_list link;
};

struct display {
  GdkDisplay *gdk_display;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_shm *shm;

	EGLDisplay dpy;
	EGLConfig argb_config;
	EGLContext argb_ctx;
	cairo_device_t *argb_device;
	uint32_t serial;

	int display_fd;
	uint32_t display_fd_events;
	struct task display_task;
	int epoll_fd;

	int running;
};

struct nested {
	struct display *display;
  GtkWidget *widget;
	struct egl_window_surface *surface;

	struct wl_display *child_display;
	struct task child_task;

	EGLDisplay egl_display;
//	struct program *texture_program;

	struct nested_surface *nested_surface;
	struct wl_list frame_callback_list;
};

struct nested_surface {
	struct wl_resource *resource;
	struct wl_resource *buffer_resource;
	struct nested *nested;
	EGLImageKHR *image;
	GLuint texture;
	struct wl_list link;
	cairo_surface_t *cairo_surface;
};

struct nested_frame_callback {
	struct wl_resource *resource;
	struct wl_list link;
};

struct egl_window_surface {
	struct display *display;
	struct wl_surface *surface;
	struct wl_egl_window *egl_window;
	EGLSurface egl_surface;
	cairo_surface_t *cairo_surface;
};

struct nested *
wlu_nested_compositor_create (struct display *display, GtkWidget *widget);

struct display *
wlu_display_create (GtkWidget *widget);

/*
void
wlu_nested_surface_resize (struct nested *nested);
*/
#endif
