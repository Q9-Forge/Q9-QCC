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
	./test/remotetest.sh

# Zusaetzlich an echtem Material: den Quellen des QCC-Backends und den
# handgeschriebenen Kernelquellen von Q9-OS.
backend: build/qr68
	./test/backend.sh
	./test/handwritten.sh

# qr68 als OS-9-Modul bauen (qcpp -> QCC -> Backend -> qr68 selbst -> l68)
# und auf echtem 68030 fahren. QUELLE= waehlt die Quelle, die dort
# assembliert wird; verglichen wird byteweise mit dem Hostlauf.
os9: build/qr68
	./tools/build_os9.sh

test68k: build/qr68
	./tools/test_68k.sh

# Der schwerste Korpus: die SCF-Treiber des SDK. Laeuft NICHT in "check",
# weil drei der 14 uebersetzbaren Quellen bewusst abweichen (s. README) --
# der Aufruf haette also immer Exitcode 1. PORTDIR=/DRVDIR= waehlen die
# Gruppe, UEXTRA= zusaetzliche Suchverzeichnisse.
mwos: build/qr68
	./test/mwos.sh

check: test backend

clean:
	rm -rf build

.PHONY: all test backend mwos os9 test68k check clean
