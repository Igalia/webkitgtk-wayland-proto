#include "wl-utils.h"
#include "eventsource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
  
#include <gdk/gdkwayland.h>

static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static PFNEGLBINDWAYLANDDISPLAYWL bind_display;
static PFNEGLUNBINDWAYLANDDISPLAYWL unbind_display;
static PFNEGLQUERYWAYLANDBUFFERWL query_buffer;

#define container_of(ptr, type, member) ({				\
	const __typeof__( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

static void
checkEGLError (void)
{
  int error = eglGetError ();
  if (error != EGL_SUCCESS) {
    fprintf (stderr, "server: EGL ERROR: 0x%x\n", error);
  }
}

static void
checkGLError (void)
{
  int error = glGetError ();
  if (error != GL_NO_ERROR) {
    fprintf (stderr, "server: GL ERROR: 0x%x\n", error);
  }
}

static void
destroy_nested_frame_callback(struct wl_resource *resource)
{
	struct nested_frame_callback *callback = wl_resource_get_user_data(resource);
	wl_list_remove(&callback->link);
	free(callback);
}

static void
destroy_nested_surface(struct wl_resource *resource)
{
	struct nested_surface *surface = wl_resource_get_user_data(resource);
	free(surface);
}

/* ===================== DISPLAY ========================= */

static void
init_egl (struct display *d)
{
	EGLint major, minor;
	EGLint n;
  int ret;

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	static const EGLint argb_cfg_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_DEPTH_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	d->dpy = eglGetDisplay (d->display);
  assert(d->dpy);

	ret = eglInitialize(d->dpy, &major, &minor);
	assert(ret == EGL_TRUE);

	eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);

  ret = eglChooseConfig(d->dpy, argb_cfg_attribs, &d->argb_config, 1, &n);
	assert(ret && n == 1);

	d->argb_ctx =
    eglCreateContext(d->dpy, d->argb_config, EGL_NO_CONTEXT, context_attribs);
	assert(d->argb_ctx);

	ret = eglMakeCurrent(d->dpy, NULL, NULL, d->argb_ctx);
  assert(ret == EGL_TRUE);

	d->argb_device = cairo_egl_device_create(d->dpy, d->argb_ctx);
	assert (cairo_device_status(d->argb_device) == CAIRO_STATUS_SUCCESS);
}

static struct display *
display_create (GtkWidget *widget)
{
  GdkDisplay *gdk_display;
  struct display *d;

	d = malloc(sizeof (struct display));
	if (d == NULL)
		return NULL;
  memset(d, 0, sizeof (struct display));

  gdk_display =
    gdk_display_manager_get_default_display (gdk_display_manager_get ());
  d->gdk_display = gdk_display;
  d->display =  gdk_wayland_display_get_wl_display (d->gdk_display);
	if (d->display == NULL) {
		fprintf(stderr, "failed to connect to Wayland display: %m\n");
		free(d);
		return NULL;
	}

	init_egl(d);

	return d;
}

static cairo_device_t *
display_get_cairo_device(struct display *display)
{
	return display->argb_device;
}

static int
display_acquire_surface(struct display *display,
                        struct egl_window_surface *surface,
                        EGLContext ctx)
{
	cairo_device_t *device;

  device = cairo_surface_get_device(surface->cairo_surface);
	if (!device)
		return -1;

	if (!ctx) {
		if (device == surface->display->argb_device)
			ctx = surface->display->argb_ctx;
		else
			assert(0);
	}

	cairo_device_flush(device);
	cairo_device_acquire(device);
	if (eglMakeCurrent(surface->display->dpy, surface->egl_surface,
			    surface->egl_surface, ctx) != EGL_TRUE) {
    checkEGLError();
		fprintf(stderr, "server: failed to make surface current\n");
  }

	return 0;
}

static void
display_release_surface(struct display *display,
                        struct egl_window_surface *surface)
{
	cairo_device_t *device;

	device = cairo_surface_get_device(surface->cairo_surface);
	if (!device)
		return;

	if (!eglMakeCurrent(surface->display->dpy, NULL, NULL,
			    surface->display->argb_ctx)) {
    checkEGLError ();
		fprintf(stderr, "server: failed to make context current\n");
  }
	cairo_device_release(device);
}

static EGLDisplay
display_get_egl_display(struct display *d)
{
	return d->dpy;
}

/* ===================== SURFACE ========================= */

static void
surface_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
paint_surface (struct nested_surface *nested_surface)
{
  cairo_surface_t *cairo_surface;
  cairo_t *cr;
  GtkAllocation allocation;
  struct nested *nested;

  nested = nested_surface->nested;
  gtk_widget_get_allocation (nested->widget, &allocation);

  cairo_surface = nested->surface->cairo_surface;
  cr = cairo_create (cairo_surface);
  cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
	cairo_set_source_rgb (cr, 1, 0, 0);
	cairo_fill (cr);

	cairo_destroy (cr);
}

static void
surface_attach(struct wl_client *client,
	       struct wl_resource *resource,
	       struct wl_resource *buffer_resource, int32_t sx, int32_t sy)
{
//  printf ("server: surface_attach\n");

	struct nested_surface *surface = wl_resource_get_user_data(resource);
	struct nested *nested = surface->nested;
	struct wl_buffer *buffer = wl_resource_get_user_data(buffer_resource);
	EGLint format, width, height;
	cairo_device_t *device;

	if (surface->buffer_resource)
		wl_buffer_send_release(surface->buffer_resource);

	surface->buffer_resource = buffer_resource;
	if (!query_buffer(nested->egl_display, buffer,
			  EGL_TEXTURE_FORMAT, &format)) {
		fprintf(stderr, "attaching non-egl wl_buffer\n");
		return;
	}

	if (surface->image != EGL_NO_IMAGE_KHR)
		destroy_image(nested->egl_display, surface->image);
	if (surface->cairo_surface)
		cairo_surface_destroy(surface->cairo_surface);

	switch (format) {
	case EGL_TEXTURE_RGB:
	case EGL_TEXTURE_RGBA:
		break;
	default:
		fprintf(stderr, "unhandled format: %x\n", format);
		return;
	}

	surface->image = create_image(nested->egl_display, NULL,
				      EGL_WAYLAND_BUFFER_WL, buffer, NULL);
	if (surface->image == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "failed to create img\n");
		return;
	}

	query_buffer(nested->egl_display, buffer, EGL_WIDTH, &width);
	query_buffer(nested->egl_display, buffer, EGL_HEIGHT, &height);

//  printf ("server: attach: width: %d\n", width);
//  printf ("server: attach: height: %d\n", height);

	device = display_get_cairo_device(nested->display);
	surface->cairo_surface = 
		cairo_gl_surface_create_for_texture(device,
						    CAIRO_CONTENT_COLOR_ALPHA,
						    surface->texture,
						    width, height);

	glBindTexture(GL_TEXTURE_2D, surface->texture);
  checkGLError();
	image_target_texture_2d(GL_TEXTURE_2D, surface->image);
  checkEGLError();

//  paint_surface (surface);
}

static void
surface_damage(struct wl_client *client,
	       struct wl_resource *resource,
	       int32_t x, int32_t y, int32_t width, int32_t height)
{
}

static void
surface_frame(struct wl_client *client,
	      struct wl_resource *resource, uint32_t id)
{
//  printf ("server: surface_frame\n");

	struct nested_frame_callback *callback;
	struct nested_surface *surface = wl_resource_get_user_data(resource);
	struct nested *nested = surface->nested;

	callback = malloc(sizeof(struct nested_frame_callback));
	if (callback == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	callback->resource =
		wl_client_add_object(client, &wl_callback_interface, NULL, id, callback);
	wl_resource_set_destructor(callback->resource, destroy_nested_frame_callback);

	wl_list_insert(nested->frame_callback_list.prev, &callback->link);
}

static void
surface_set_opaque_region(struct wl_client *client,
			  struct wl_resource *resource,
			  struct wl_resource *region_resource)
{
	fprintf(stderr, "surface_set_opaque_region not implemented\n");
}

static void
surface_set_input_region(struct wl_client *client,
			 struct wl_resource *resource,
			 struct wl_resource *region_resource)
{
	fprintf(stderr, "surface_set_input_region not implemented\n");
}

static void
surface_commit(struct wl_client *client, struct wl_resource *resource)
{
}

static void
surface_set_buffer_transform(struct wl_client *client,
			     struct wl_resource *resource, int transform)
{
	fprintf(stderr, "surface_set_buffer_transform not implemented\n");
}

static const struct wl_surface_interface surface_interface = {
	surface_destroy,
	surface_attach,
	surface_damage,
	surface_frame,
	surface_set_opaque_region,
	surface_set_input_region,
	surface_commit,
	surface_set_buffer_transform
};

/* ===================== EGL_WINDOW_SURFACE ======================== */

struct egl_window_surface *
egl_window_surface_create (struct display *display, GtkWidget *widget)
{
  EGLNativeWindowType egl_window;
  EGLSurface egl_surface;
  cairo_surface_t *cairo_surface;
  GtkAllocation allocation;
  GdkWindow *gdk_window;
  struct wl_surface *wl_surface;
  struct egl_window_surface *egl_window_surface;

  egl_window_surface = malloc (sizeof (struct egl_window_surface));
  if (!egl_window_surface)
    return NULL;
	memset(egl_window_surface, 0, sizeof (struct egl_window_surface));

  gtk_widget_get_allocation (widget, &allocation);

  gdk_window = gtk_widget_get_window (widget);
  wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);

  egl_window =
    wl_egl_window_create (wl_surface, allocation.width, allocation.height);

  egl_surface =
    eglCreateWindowSurface (display->dpy, display->argb_config, egl_window, NULL);

	cairo_surface =
		cairo_gl_surface_create_for_egl(display->argb_device, egl_surface,
						allocation.width, allocation.height);

  egl_window_surface->display = display;
  egl_window_surface->surface = wl_surface;
  egl_window_surface->egl_window = egl_window;
  egl_window_surface->egl_surface = egl_surface;
  egl_window_surface->cairo_surface = cairo_surface;

  return egl_window_surface;
}


/* ===================== COMPOSITOR ======================== */

static void
compositor_create_surface(struct wl_client *client,
			  struct wl_resource *resource, uint32_t id)
{
	struct nested *nested;
	struct nested_surface *nested_surface;

//  printf ("server: compositor_create_surface\n");

  nested = wl_resource_get_user_data(resource);
  nested_surface = malloc(sizeof(struct nested_surface));
	if (nested_surface == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	memset(nested_surface, 0, sizeof *nested_surface);
	nested_surface->nested = nested;

/*
  nested->surface = egl_window_surface_create (nested->display, nested->widget);

	display_acquire_surface(nested->display, nested->surface, NULL);
*/
	glGenTextures(1, &nested_surface->texture);
	glBindTexture(GL_TEXTURE_2D, nested_surface->texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

/*
	display_release_surface(nested->display, nested->surface);
*/
	nested_surface->resource =
		wl_client_add_object(client, &wl_surface_interface,
				     &surface_interface, id, nested_surface);
	wl_resource_set_destructor(nested_surface->resource, destroy_nested_surface);

	nested->nested_surface = nested_surface;
}

static const struct wl_compositor_interface compositor_interface = {
	compositor_create_surface,
};

static void
compositor_bind(struct wl_client *client,
            		void *data, uint32_t version, uint32_t id)
{
  wl_client_add_object(client, &wl_compositor_interface,
                       &compositor_interface, id, data);
}

static int
nested_compositor_init (struct nested *nested)
{
	const char *extensions;
	int ret;

	wl_list_init(&nested->frame_callback_list);

	nested->child_display = wl_display_create();

  wayland_display_source_new (nested->child_display);

	if (!wl_global_create(nested->child_display,
                              &wl_compositor_interface,
                              wl_compositor_interface.version,
                              nested, compositor_bind)) {
		fprintf(stderr, "failed to bind nested compositor\n");
		return -1;
  }

	wl_display_init_shm(nested->child_display);

	nested->egl_display = display_get_egl_display(nested->display);
	extensions = eglQueryString(nested->egl_display, EGL_EXTENSIONS);
	if (strstr(extensions, "EGL_WL_bind_wayland_display") == NULL) {
		fprintf(stderr, "no EGL_WL_bind_wayland_display extension\n");
		return -1;
	}

	bind_display = (void *) eglGetProcAddress("eglBindWaylandDisplayWL");
	unbind_display = (void *) eglGetProcAddress("eglUnbindWaylandDisplayWL");
	create_image = (void *) eglGetProcAddress("eglCreateImageKHR");
	destroy_image = (void *) eglGetProcAddress("eglDestroyImageKHR");
	query_buffer = (void *) eglGetProcAddress("eglQueryWaylandBufferWL");
	image_target_texture_2d =
		(void *) eglGetProcAddress("glEGLImageTargetTexture2DOES");

	ret = bind_display(nested->egl_display, nested->child_display);
	if (!ret) {
		fprintf(stderr, "failed to bind wl_display\n");
		return -1;
	}

//  printf ("server: nested compositor initialized\n");

	return 0;
}

/* ===================== API ======================== */

struct nested *
wlu_nested_compositor_create (struct display *display, GtkWidget *widget)
{
	struct nested *nested;

	nested = malloc(sizeof (struct nested));
	if (!nested)
		return NULL;
	memset(nested, 0, sizeof (struct nested));

	nested->display = display;
  nested->widget = widget;

	nested_compositor_init (nested);

	return nested;
}

struct display *
wlu_display_create (GtkWidget *widget)
{
  return display_create (widget);
}

/*
void
wlu_nested_surface_resize (struct nested *nested, int width, int height)
{
  struct egl_window_surface *surface = nested->egl_window_surface;
  wl_egl_window_resize (surface->egl_window, width, height, 0, 0);
  cairo_gl_surface_set_size (surface->cairo_surface, width, height);
}
*/
