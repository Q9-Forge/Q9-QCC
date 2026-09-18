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

FORGE ?= ../../..
QCC   ?= $(FORGE)/Q9-QCC
QR68  ?= $(FORGE)/Q9-QCC/Q9-BACKEND-68K/q9-qr68k/build/qr68k
QCPP  ?= $(QCC)/Q9-FRONTEND-C/q9-qcpp/build/qcpp
QCCP  ?= $(QCC)/Q9-FRONTEND-C/q9-qcir/build/qcir
QCCB  ?= $(QCC)/Q9-BACKEND-68K/q9-qir68k/build/qir68k

# Reihenfolge = Bindereihenfolge.
# Blaetter zuletzt: os9call.r ruft nichts mehr auf. mem_c.r braucht
# _os_srqmem daraus, steht also davor; str_c.r braucht gar nichts.
ROFS = $(patsubst src/extra_%.a,build/extra_%.r,$(sort $(wildcard src/extra_*.a))) \
       build/printf.r build/printf_c.r build/file.r build/file_c.r \
       build/str.r build/str_c.r build/ctype.r build/ctype_c.r \
       build/mem.r build/mem_c.r \
       build/iob.r build/os9call.r

all: build/qclib.l build/q9_cstart.r

build/qclib.l: $(ROFS)
	cat $(ROFS) > $@
	@echo "  $@: $$(wc -c < $@ | tr -d ' ') Byte"

# Ein C-Modul der Bibliothek: qcpp -> qcc -> Backend (-part, also ohne
# eigenes main) -> qr68. Jede Datei wird ein eigener psect und damit ein
# eigener ROF -- so bindet der Linker nur ein, was wirklich gebraucht wird.
build/%_c.r: src/%.c | build
	$(QCPP) -I$(QCC)/Q9-FRONTEND-C/q9-qcpp/include $< build/$*.i
	$(QCCP) "@build/$*.i" > build/$*.ir 2> build/$*.err
	@test "$$(tail -1 build/$*.ir)" = OK || { head -5 build/$*.err; exit 1; }
	$(QCCB) build/$*.ir build/$*.s68 -os9 -part
	$(QR68) build/$*.s68 -o=$@

build/%.r: src/%.a | build
	$(QR68) $< -o=$@

# Der C-Startcode gehoert zu Q9-QCC, wird aber hier gebraucht -- und muss
# mitgebaut werden, sonst haengt der Ziellauf an einer Datei, die zufaellig
# noch irgendwo herumliegt. "use q9defs.d" sucht relativ zum
# ARBEITSverzeichnis, deshalb liegen beide Dateien nebeneinander in build/.
build/q9_cstart.r: $(QCC)/runtime/os9/q9_cstart.a $(QCC)/runtime/os9/q9defs.d | build
	cp $(QCC)/runtime/os9/q9_cstart.a $(QCC)/runtime/os9/q9defs.d build/
	cd build && $(abspath $(QR68)) q9_cstart.a -o=q9_cstart.r

build:
	mkdir -p build

# Das Testprogramm ist ein VOLLprogramm (mit main), deshalb ohne -part
# und mit -largedata -- anders als die Bibliotheksmodule.
build/hello.r: tests/hello.c | build
	$(QCPP) -I$(QCC)/Q9-FRONTEND-C/q9-qcpp/include $< build/hello.i
	$(QCCP) "@build/hello.i" > build/hello.ir 2> build/hello.err
	@test "$$(tail -1 build/hello.ir)" = OK || { head -5 build/hello.err; exit 1; }
	$(QCCB) build/hello.ir build/hello.s68 -os9 -largedata
	$(QR68) build/hello.s68 -o=$@

# Dedicated fgets regression program. It is a complete module because the
# test exercises the public qclib interface on the target emulator.
build/fgtest.r: tests/fgtest.c | build
	$(QCPP) -I$(QCC)/Q9-FRONTEND-C/q9-qcpp/include $< build/fgtest.i
	$(QCCP) "@build/fgtest.i" > build/fgtest.ir 2> build/fgtest.err
	@test "$$(tail -1 build/fgtest.ir)" = OK || { head -5 build/fgtest.err; exit 1; }
	$(QCCB) build/fgtest.ir build/fgtest.s68 -os9 -largedata
	$(QR68) build/fgtest.s68 -o=$@

test: all build/hello.r
	./tests/hello68k.sh
	./tests/vsclib.sh

clean:
	rm -rf build

.PHONY: all test clean
