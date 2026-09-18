CC = gcc
CFLAGS = -O2 -Wall -Wextra

all: build/qov32

build/qov32: src/qov32.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf build/qov32

.PHONY: all clean
