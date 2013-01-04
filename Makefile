#!/usr/bin/gmake

schillix-install:
	gcc main.c -o schillix-install

clean:
	rm -f schillix-install
