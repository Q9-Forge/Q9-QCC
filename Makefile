# ql68 -- Binder der Q9-Werkzeugkette
#
#   make        Hostfassung bauen (build/ql68)
#   make test   Differenztest gegen l68 (braucht Wine + MWOS-Toolchain)
#   make clean

CC ?= cc
# -std=c89, weil ql68 in der Teilmenge bleiben soll, die QCC lesen kann.
CFLAGS ?= -std=c89 -Wall -Wextra -Wno-builtin-requires-header -O2

all: build/ql68

build/ql68: src/ql68.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<

test: build/ql68
	./test/difftest.sh
	./test/multi.sh
	./test/rawtest.sh
	./test/descs.sh

check: test

clean:
	rm -rf build

.PHONY: all test check clean
