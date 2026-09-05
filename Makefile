# qclib -- die C-Bibliothek der Q9-Werkzeugkette
#
#   make        qclib.l bauen (mit der EIGENEN Kette: qcpp, qcc, qr68)
#   make test   das Testprogramm auf echtem 68030 laufen lassen
#   make clean
#
# DIE REIHENFOLGE IN qclib.l IST TEIL DES BAUS, nicht Kosmetik: der
# Binder macht nur EINEN Durchgang ueber eine einfache Bibliothek, also
# muessen Referenzen vorwaerts zeigen -- Nutzer zuerst, Blaetter zuletzt
# (Handbuch Kap. 9). printf.r ruft tc_printf_a, steht also VOR printf_c.r.
# "rdump -l" prueft diese Ordnung nach.

FORGE ?= ..
QCC   ?= $(FORGE)/Q9-QCC
QR68  ?= $(FORGE)/Q9-qr68/build/qr68
QCPP  ?= $(QCC)/q9-cpp/build/qcpp
QCCP  ?= $(QCC)/build/qcc_p
QCCB  ?= $(QCC)/build/qcc_backend

# Reihenfolge = Bindereihenfolge.
# Blaetter zuletzt: os9call.r ruft nichts mehr auf.
ROFS = build/printf.r build/printf_c.r build/iob.r build/os9call.r

all: build/qclib.l

build/qclib.l: $(ROFS)
	cat $(ROFS) > $@
	@echo "  $@: $$(wc -c < $@ | tr -d ' ') Byte"

# Ein C-Modul der Bibliothek: qcpp -> qcc -> Backend (-part, also ohne
# eigenes main) -> qr68. Jede Datei wird ein eigener psect und damit ein
# eigener ROF -- so bindet der Linker nur ein, was wirklich gebraucht wird.
build/%_c.r: src/%.c | build
	$(QCPP) -I$(QCC)/q9-cpp/include $< build/$*.i
	$(QCCP) "@build/$*.i" > build/$*.ir 2> build/$*.err
	@test "$$(tail -1 build/$*.ir)" = OK || { head -5 build/$*.err; exit 1; }
	$(QCCB) build/$*.ir build/$*.s68 -os9 -part
	$(QR68) build/$*.s68 -o=$@

build/%.r: src/%.a | build
	$(QR68) $< -o=$@

build:
	mkdir -p build

# Das Testprogramm ist ein VOLLprogramm (mit main), deshalb ohne -part
# und mit -largedata -- anders als die Bibliotheksmodule.
build/hello.r: test/hello.c | build
	$(QCPP) -I$(QCC)/q9-cpp/include $< build/hello.i
	$(QCCP) "@build/hello.i" > build/hello.ir 2> build/hello.err
	@test "$$(tail -1 build/hello.ir)" = OK || { head -5 build/hello.err; exit 1; }
	$(QCCB) build/hello.ir build/hello.s68 -os9 -largedata
	$(QR68) build/hello.s68 -o=$@

test: all build/hello.r
	./test/hello68k.sh

clean:
	rm -rf build

.PHONY: all test clean
