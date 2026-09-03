# qr68 -- 68k-Assembler der Q9-Werkzeugkette, Ausgabe als OS-9-ROF
#
#   make           Hostfassung bauen (build/qr68)
#   make test      Differenztest gegen r68 (braucht Wine + MWOS-Toolchain)
#   make clean

CC ?= cc
# -std=c89, weil qr68 in der Teilmenge bleiben soll, die QCC lesen kann.
# -Wno-builtin-requires-header: die libc-Prototypen stehen absichtlich
# vereinfacht in der Quelle statt per Header (s. README).
CFLAGS ?= -std=c89 -Wall -Wextra -Wno-builtin-requires-header -O2

all: build/qr68

build/qr68: src/qr68.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<

test: build/qr68
	./test/difftest.sh
	./test/insndiff.sh
	RFLAGS=-b ./test/insndiff.sh test/bopt.a
	./test/usetest.sh

# Zusaetzlich an echtem Material: den Quellen des QCC-Backends und den
# handgeschriebenen Kernelquellen von Q9-OS.
backend: build/qr68
	./test/backend.sh
	./test/handwritten.sh

check: test backend

clean:
	rm -rf build

.PHONY: all test backend check clean
