#!/usr/bin/env bash
# Prueft, dass sich die Werkzeuge der eigenen Kette SELBST uebersetzen lassen:
# qcpp -> qcir -> qir68k -> qr68k -> ql68k, gebunden gegen qclib (am Host, Ziel 68k/OS-9).
#
# Hintergrund (2026-09-27): die Selbstuebersetzung war unbemerkt kaputt --
#   - stdio.h/stdlib.h deklarierten putchar/putc/abort doppelt ("duplicate function"),
#   - atexit/bsearch mit Funktionszeiger-Parameter im extern-Prototyp (versteht qcir nicht),
#   - qcc_backend_c.cpp nutzte sizeof auf ein Struct-Feld.
# Kein Werkzeug hat das gemeldet, weil nur hello.c & Co regelmaessig durch die Kette liefen.
#
# Aufruf: tools/test_selfhost_build.sh [ausgabeverzeichnis]   (Standard: build/selfhost)
# Ergebnis: je Werkzeug ein 68k-Modul (Name = Werkzeugname) im Ausgabeverzeichnis;
# Exitcode 0 nur, wenn alle fuenf fehlerfrei uebersetzt und gebunden wurden.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$REPO/build/selfhost}"
INC="$REPO/Q9-FRONTEND-C/q9-qcpp/include"
QCPP="$REPO/Q9-FRONTEND-C/q9-qcpp/build/qcpp"
QCIR="$REPO/Q9-FRONTEND-C/q9-qcir/build/qcir"
QIR="$REPO/Q9-BACKEND-68K/q9-qir68k/build/qir68k"
QR="$REPO/Q9-BACKEND-68K/q9-qr68k/build/qr68k"
QL="$REPO/Q9-BACKEND-68K/q9-ql68k/build/ql68k"
LIB="$REPO/Q9-BACKEND-68K/q9-qclib/build"

for t in "$QCPP" "$QCIR" "$QIR" "$QR" "$QL" "$LIB/qclib.l" "$LIB/q9_cstart.r"; do
	[ -e "$t" ] || { echo "fehlt: $t (vorher make)" >&2; exit 2; }
done
mkdir -p "$OUT"
fail=0

# $1 Modulname, $2 Quelle, $3 Stack in KB, ab $4 zusaetzliche qcpp-Schalter
baue() {
	local name="$1" src="$2" stack="$3"; shift 3
	local d="$OUT/$name.work"
	rm -rf "$d"; mkdir -p "$d"
	if ! "$QCPP" "$@" -I"$INC" -I"$(dirname "$src")" "$src" "$d/x.i" 2>"$d/qcpp.err"; then
		echo "FEHLER $name: qcpp"; head -3 "$d/qcpp.err"; fail=1; return
	fi
	"$QCIR" "@$d/x.i" >"$d/x.ir" 2>"$d/qcir.err"
	if [ "$(tail -1 "$d/x.ir")" != OK ] || [ -s "$d/qcir.err" ]; then
		echo "FEHLER $name: qcir ($(tail -1 "$d/x.ir"))"; head -5 "$d/qcir.err"; fail=1; return
	fi
	if ! "$QIR" "$d/x.ir" "$d/x.s68" -os9 -largedata -remotedata >"$d/qir.log" 2>&1; then
		echo "FEHLER $name: qir68k"; tail -3 "$d/qir.log"; fail=1; return
	fi
	if ! "$QR" "$d/x.s68" -o="$d/x.r" >"$d/qr.log" 2>&1; then
		echo "FEHLER $name: qr68k"; tail -3 "$d/qr.log"; fail=1; return
	fi
	cp "$LIB/q9_cstart.r" "$LIB/qclib.l" "$d/"
	"$QL" -a "$d/q9_cstart.r" "$d/x.r" -l="$d/qclib.l" -M="${stack}K" -O="$OUT/$name" >"$d/ql.log" 2>&1
	if [ ! -f "$OUT/$name" ]; then
		echo "FEHLER $name: ql68k"; head -5 "$d/ql.log"; fail=1; return
	fi
	printf "  ok  %-7s %7d Byte Modul  (%d IR-Zeilen)\n" "$name" "$(wc -c <"$OUT/$name")" "$(wc -l <"$d/x.ir")"
}

echo "== Selbstuebersetzung der Kette (eigene Werkzeuge, gegen qclib) -> $OUT"
baue qcpp   "$REPO/Q9-FRONTEND-C/q9-qcpp/src/qcpp.c"                  512 -D_Q9OS
baue qcir   "$REPO/Q9-FRONTEND-C/q9-qcir/data/qcc_p.c"               1024
baue qir68k "$REPO/Q9-BACKEND-68K/q9-qir68k/src/qcc_backend_c.cpp"     64
baue qr68k  "$REPO/Q9-BACKEND-68K/q9-qr68k/src/qr68.c"                512 -D_Q9OS
baue ql68k  "$REPO/Q9-BACKEND-68K/q9-ql68k/src/ql68.c"                512 -D_Q9OS

[ $fail = 0 ] && echo "== alle fuenf ok" || echo "== FEHLGESCHLAGEN"
exit $fail
