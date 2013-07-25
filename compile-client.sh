#!/bin/bash
(rm ./client || 1) 2> /dev/null
gcc -o client -O0 -g3 `pkg-config  --cflags --libs wayland-client  wayland-egl egl glesv2 cairo-glesv2` \
	-lm nested-client.c
