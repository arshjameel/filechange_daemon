CFLAGS= -Wall -pedantic -std=gnu99

all: build/filechanged

build/filechanged: filechange.c
	mkdir -p build
	gcc $(CFLAGS) `pkg-config --cflags --libs libnotify` filechange.c -o build/filechanged
