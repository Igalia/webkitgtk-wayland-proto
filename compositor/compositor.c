#include "compositor.h"
#include "wl-event-source.h"

#include <wayland-server.h>
#include <string.h>

/* EGL functions */
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static PFNEGLBINDWAYLANDDISPLAYWL bind_display;
static PFNEGLUNBINDWAYLANDDISPLAYWL unbind_display;
static PFNEGLQUERYWAYLANDBUFFERWL query_buffer;

/* ===== SURFACE INTERFACE ====== */

static void
destroy_nested_frame_callback (struct wl_resource *resource)
{
  struct NestedFrameCallback *callback = wl_resource_get_user_data (resource);
  wl_list_remove (&callback->link);
  g_free (callback);
}

static void
surface_destroy (struct wl_client *client, struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
surface_attach (struct wl_client *client,
                struct wl_resource *resource,
                struct wl_resource *buffer_resource,
                int32_t sx, int32_t sy)
{
  printf ("compositor: surface_attach\n");

  if (!buffer_resource) {
    g_print ("compositor: warning: surface attach with NULL buffer\n");
    return;
  }

  EGLint format;
  struct NestedSurface *surface = wl_resource_get_user_data (resource);
  struct Compositor *c = surface->compositor;

  if (!query_buffer (c->display->egl_display, buffer_resource,
                     EGL_TEXTURE_FORMAT, &format)) {
    g_print ("compositor: attaching non-egl buffer\n");
    return;
  }

  if (format != EGL_TEXTURE_RGB && format != EGL_TEXTURE_RGBA) {
    g_print ("compositor: unhandled format: %x\n", format);
    return;
  }

  surface->buffer_resource = buffer_resource;
}

static void
surface_damage (struct wl_client *client,
                struct wl_resource *resource,
                int32_t x, int32_t y, int32_t width, int32_t height)
{
  g_print ("compositor: surface_damage not implemented\n");
}

static void
surface_frame (struct wl_client *client,
               struct wl_resource *resource, uint32_t id)
{
  printf ("compositor: surface frame\n");

  struct NestedFrameCallback *callback;
  struct NestedSurface *surface = wl_resource_get_user_data (resource);
  struct Compositor *c = surface->compositor;

  callback = g_new0 (struct NestedFrameCallback, 1);
  callback->resource = wl_resource_create (client, &wl_callback_interface, 1, id);
  wl_resource_set_implementation (callback->resource, NULL, callback, destroy_nested_frame_callback);

  wl_list_insert (c->frame_callback_list.prev, &callback->link);
}

static void
surface_set_opaque_region (struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *region_resource)
{
  g_print ("compositor: surface_set_opaque_region not implemented\n");
}

static void
surface_set_input_region (struct wl_client *client,
                          struct wl_resource *resource,
                          struct wl_resource *region_resource)
{
  g_print ("compositor: surface_set_input_region not implemented\n");
}

static void
paint_surface (cairo_surface_t *surface, int width, int height)
{
  cairo_t *cr;
  cr = cairo_create (surface);
  cairo_rectangle (cr, 0, 0, width, height);
  cairo_set_source_rgb (cr, 1, 0, 0);
  cairo_fill (cr);
  cairo_destroy (cr);
}

static void
surface_commit (struct wl_client *client, struct wl_resource *resource)
{
  struct NestedSurface *surface = wl_resource_get_user_data (resource);
  struct Compositor *c = surface->compositor;
  int width, height;

  /* Create EGL Image from attached buffer */
  if (surface->image != EGL_NO_IMAGE_KHR)
    destroy_image (c->display->egl_display, surface->image);

  EGLDisplay egl_display = c->display->egl_display;
  surface->image =
    create_image (egl_display, NULL, EGL_WAYLAND_BUFFER_WL,
                  surface->buffer_resource, NULL);

  if (surface->image == EGL_NO_IMAGE_KHR) {
    g_print ("compositor: failed to create EGLImage on surface commit\n");
    return;
  }

  /* Render buffer with Cairo */
  if (surface->cairo_surface)
    cairo_surface_destroy (surface->cairo_surface);

  query_buffer (egl_display, surface->buffer_resource, EGL_WIDTH, &width);
  query_buffer (egl_display, surface->buffer_resource, EGL_HEIGHT, &height);

  printf ("compositor: buffer width: %d\n", width);
  printf ("compositor: buffer height: %d\n", height);

  cairo_device_t *device = c->display->egl_device;
  surface->cairo_surface =
    cairo_gl_surface_create_for_texture (device,
                                         CAIRO_CONTENT_COLOR_ALPHA,
                                         surface->texture,
                                         width, height);

  glBindTexture (GL_TEXTURE_2D, surface->texture);
  image_target_texture_2d (GL_TEXTURE_2D, surface->image);

  paint_surface (surface->cairo_surface, width, height);
}

static void
surface_set_buffer_transform(struct wl_client *client,
			     struct wl_resource *resource, int transform)
{
  g_print ("compositor: surface_set_buffer_transform not implemented\n");
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

/* ===== COMPOSITOR INTERFACE ====== */

static void
destroy_nested_surface (struct wl_resource *resource)
{
  struct NestedSurface *surface = wl_resource_get_user_data (resource);
  g_free (surface);
}

static void
compositor_create_surface (struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
  struct Compositor *c;
  struct NestedSurface *surface;

  printf ("compositor: create surface\n");

  c = wl_resource_get_user_data (resource);
  surface = g_new0 (struct NestedSurface, 1);

  surface->compositor = c;

  glGenTextures (1, &surface->texture);
  glBindTexture (GL_TEXTURE_2D, surface->texture);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  struct wl_resource *surface_resource =
    wl_resource_create (client, &wl_surface_interface, 1, id);
  wl_resource_set_implementation (surface_resource, &surface_interface,
                                  surface, destroy_nested_surface);

  c->nested_surface = surface;
}


static const struct wl_compositor_interface compositor_interface = {
  compositor_create_surface,
};

static void
compositor_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
  struct Compositor *c = data;
  struct wl_resource *resource =
    wl_resource_create(client, &wl_compositor_interface, MIN(version, 3), id);
  wl_resource_set_implementation (resource, &compositor_interface, c, NULL);
}

static int
compositor_init (struct Compositor *c)
{
  const gchar *extensions;

  wl_list_init (&c->frame_callback_list);

  /* Create client child display and the event source for it */
  c->child_display = wl_display_create ();
  compositor_display_source_new (c->child_display);

  /* Register display object */
  if (!wl_global_create (c->child_display,
                         &wl_compositor_interface,
                         wl_compositor_interface.version,
                         c, compositor_bind)) {
    g_print("compositor: failed to bind nested compositor\n");
    return -1;
  }

  wl_display_init_shm (c->child_display);

  /* Bind child display */
  EGLDisplay egl_display = c->display->egl_display;
  extensions = eglQueryString(egl_display, EGL_EXTENSIONS);
  if (strstr (extensions, "EGL_WL_bind_wayland_display") == NULL) {
    g_print ("compositor: no EGL_WL_bind_wayland_display extension\n");
    return -1;
  }

  bind_display = (void *) eglGetProcAddress("eglBindWaylandDisplayWL");
  unbind_display = (void *) eglGetProcAddress("eglUnbindWaylandDisplayWL");
  create_image = (void *) eglGetProcAddress("eglCreateImageKHR");
  destroy_image = (void *) eglGetProcAddress("eglDestroyImageKHR");
  query_buffer = (void *) eglGetProcAddress("eglQueryWaylandBufferWL");
  image_target_texture_2d =	(void *) eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if (!bind_display (egl_display, c->child_display)) {
    g_print ("compositor: failed to bind wl_display\n");
    return -1;
  }

  g_print ("compositor: nested compositor initialized\n");

  return 0;
}


struct Compositor *
compositor_create (GtkWidget *widget, struct Display *d)
{
  struct Compositor *c = g_new0 (struct Compositor, 1);
  c->display = d;
  c->widget = widget;
  compositor_init (c);
  return c;
}
