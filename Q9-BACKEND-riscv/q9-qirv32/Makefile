CC = gcc
CFLAGS = -O2 -Wall -Wextra

all: build/qirv32

build/qirv32: src/qcc_backend_riscv.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf build/qirv32

.PHONY: all clean
