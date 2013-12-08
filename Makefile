CC = gcc

COMMON_FLAGS = -O0 -g3 -ggdb -Wall
COMMON_LIBS = wayland-client wayland-egl egl glesv2

SERVER_SOURCES = \
	main.c \
	wl-utils.c \
	ViewWidget.c \
	os-compatibility.c \
	eventsource.c

CLIENT_SOURCES = \
	nested-client.c

all: server client

server: Makefile $(SERVER_SOURCES)
	@$(CC) $(COMMON_FLAGS) \
		`pkg-config --libs --cflags $(COMMON_LIBS) gtk+-3.0 wayland-server` \
		-o server \
		$(SERVER_SOURCES)

client: Makefile $(CLIENT_SOURCES)
	@$(CC) -lm $(COMMON_FLAGS) \
		`pkg-config --libs --cflags $(COMMON_LIBS)` \
		-o client \
		$(CLIENT_SOURCES)

clean:
	@rm server client
