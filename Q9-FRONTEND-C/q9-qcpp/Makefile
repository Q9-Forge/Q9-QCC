# qcpp -- C-Praeprozessor der Q9-Werkzeugkette
#
#   make           Hostfassung bauen (build/qcpp)
#   make test      Regressionssuite (50 Faelle, laeuft in unter einer Sekunde)
#   make difftest  Differenztest gegen cc -E ueber die MWOS-SDK-Header
#   make check     beides
#   make os9       qcpp als OS-9/68K-Modul (braucht Wine + MWOS-Toolchain)
#   make clean

CC ?= cc
# -std=c89, weil qcpp selbst in der Teilmenge bleiben soll, die QCC lesen
# kann. -Wno-builtin-requires-header: die libc-Prototypen stehen absichtlich
# vereinfacht in der Quelle statt per Header (s. README).
CFLAGS ?= -std=c89 -Wall -Wextra -Wno-builtin-requires-header -O2

all: build/qcpp

build/qcpp: src/qcpp.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<

test: build/qcpp
	./tests/run_tests.sh

difftest: build/qcpp
	./tests/difftest.sh

check: test difftest

os9: src/qcpp.c
	./tools/build_os9.sh

clean:
	rm -rf build

.PHONY: all test difftest check os9 clean
