#!/bin/bash
(rm ./server || 1) 2> /dev/null
gcc -o server -O0 -g3 `pkg-config  --cflags --libs wayland-client wayland-server wayland-egl egl glesv2 gtk+-3.0` \
	-lm wl-utils.c ViewWidget.c main.c os-compatibility.c eventsource.c
