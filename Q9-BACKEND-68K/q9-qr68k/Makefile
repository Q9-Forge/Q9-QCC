# qr68 -- 68k assembler for the Q9 toolchain, producing OS-9 ROF files
#
#   make           Build the host version (build/qr68k)
#   make test      Differential test against r68 (requires Wine + MWOS tools)
#   make clean

CC ?= cc
# -std=c89 keeps qr68 within the subset readable by QCC.
# -Wno-builtin-requires-header: libc prototypes are intentionally simplified
# in the source instead of being supplied by a header (see README).
CFLAGS ?= -std=c89 -Wall -Wextra -Wno-builtin-requires-header -O2

all: build/qr68k

build/qr68k: src/qr68.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<

test: build/qr68k
	./tests/difftest.sh
	./tests/insndiff.sh
	RFLAGS=-b ./tests/insndiff.sh tests/bopt.a
	./tests/usetest.sh
	./tests/remotetest.sh

# Additional real-world material: QCC backend sources and handwritten Q9-OS
# kernel sources.
backend: build/qr68k
	./tests/backend.sh
	./tests/handwritten.sh

# Build qr68 as an OS-9 module (qcpp -> QCC -> backend -> qr68 itself -> l68)
# and run it on a real 68030. SOURCE= selects the source assembled there;
# the result is compared byte-for-byte with the host run.
os9: build/qr68k
	./tools/build_os9.sh

test68k: build/qr68k
	./tools/test_68k.sh

# The largest corpus: SDK SCF drivers. It is NOT part of "check" because
# three of the 14 translatable sources intentionally differ (see README), so
# the command would always return 1. PORTDIR=/DRVDIR= select the group;
# UEXTRA= adds search directories.
mwos: build/qr68k
	./tests/mwos.sh

check: test backend

clean:
	rm -rf build

.PHONY: all test backend mwos os9 test68k check clean
