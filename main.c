#include <gtk/gtk.h>
#include <assert.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gdk/gdkwayland.h>
#include <string.h>
#include <sys/socket.h>

#include "ViewWidget.h"

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

	if (!wl_client_create(vw->priv->nested->child_display, sv[0])) {
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
  gtk_window_set_title (GTK_WINDOW (window), "View Widget Test");
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);

  GtkWidget *vw = view_widget_new ();
  gtk_container_add (GTK_CONTAINER (window), vw);

  gtk_widget_show (vw);
  gtk_widget_show (window);

  launch_client (VIEW_WIDGET (vw), "client");

  gtk_main ();

  return 0;
}
