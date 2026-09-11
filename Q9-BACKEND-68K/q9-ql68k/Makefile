# ql68 -- Binder der Q9-Werkzeugkette
#
#   make        Hostfassung bauen (build/ql68k)
#   make test   Differenztest gegen l68 (braucht Wine + MWOS-Toolchain)
#   make clean

CC ?= cc
# -std=c89, weil ql68 in der Teilmenge bleiben soll, die QCC lesen kann.
CFLAGS ?= -std=c89 -Wall -Wextra -Wno-builtin-requires-header -O2

all: build/ql68k

build/ql68k: src/ql68.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<

test: build/ql68k
	./tests/difftest.sh
	./tests/multi.sh
	./tests/rawtest.sh
	./tests/descs.sh
	./tests/optstest.sh
	./tests/jumptest.sh
	./tests/datalimit.sh
	./tests/remotetest.sh

check: test

clean:
	rm -rf build

.PHONY: all test check clean
