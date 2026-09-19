CC = gcc
CFLAGS = -O2 -Wall -Wextra

all: build/qlv32

build/qlv32: src/qlv32.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf build/qlv32

.PHONY: all clean
