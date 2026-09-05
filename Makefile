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
ROFS = build/printf.r build/printf_c.r build/iob.r

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

test: all
	./test/hello68k.sh

clean:
	rm -rf build

.PHONY: all test clean
