#!/usr/bin/gmake

CFLAGS = -Wall -Werror
LIBS = -lparted -ladm

schillix-install:
	gcc $(CFLAGS) $(LIBS) main.c disk.c -o schillix-install

clean:
	rm -f schillix-install
