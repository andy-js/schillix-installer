#!/usr/bin/gmake

CFLAGS = -Wall -Werror
LIBS = -lparted

schillix-install:
	gcc $(CFLAGS) $(LIBS) main.c -o schillix-install

clean:
	rm -f schillix-install
