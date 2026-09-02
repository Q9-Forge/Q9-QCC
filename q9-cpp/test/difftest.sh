#!/usr/bin/env bash
# Differenztest: qcpp gegen den Host-Praeprozessor (cc -E).
#
# Warum so und nicht mit handgeschriebenen Sollwerten: die Suite in
# test/run_tests.sh prueft, was ich beim Schreiben BEDACHT habe. Dieser Test
# prueft an echtem Material, was ich nicht bedacht habe -- und braucht dafuer
# kein Orakel, das erst selbst richtig sein muss.
#
# Vergleichsbedingungen (sonst vergleicht man Vorbelegungen statt Verhalten):
#   cc   -E -undef -nostdinc   -- ohne die Host-Makros und ohne /usr/include
#   qcpp -ansi                 -- clang setzt __STDC__ unabhaengig von -undef
# Verglichen wird der Tokenstrom (Zwischenraum und Umbrueche zusammengefasst),
# nicht die Formatierung: zwei normgerechte Praeprozessoren duerfen anders
# umbrechen.
#
# Aufruf:
#   ./test/difftest.sh                       -- SDK-Header (Standardkorpus)
#   ./test/difftest.sh <datei.c> [-I...]     -- eine Datei
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QCPP=build/qcpp
[ -x "$QCPP" ] || { echo "FEHLER: $QCPP fehlt -- vorher 'make'"; exit 2; }

MWOS=${MWOS:-/Volumes/SSD1TB/projects/MWOS}
DEFS1="$MWOS/SRC/DEFS"
DEFS2="$MWOS/OS9/SRC/DEFS"

TMP="$(mktemp -d /tmp/qcpp-diff.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

norm() { tr '\t\n' '  ' < "$1" | tr -s ' ' | sed 's/^ //; s/ $//'; }
# Zeichenkette ohne jeden Zwischenraum: zwei normgerechte Praeprozessoren
# duerfen unterschiedlich setzen (qcpp schiebt zur Sicherheit einen
# Zwischenraum zwischen Satzzeichen, cc gibt die Quellabstaende wieder). Nur
# ein Unterschied im TEXT der Tokens ist ein echter Befund, ein Unterschied
# in den Abstaenden wird gezaehlt und gemeldet, gilt aber nicht als Fehler.
squash() { tr -d ' \t\n' < "$1"; }

same=0
spacing=0
diffr=0
ccfail=0
qfail=0
difflist=""
spacelist=""

# compare <quelle> <anzeigename> <includeargs...>
compare() {
	local src="$1"; shift
	local label="$1"; shift
	local inc=("$@")
	local ccargs=()
	local qargs=()
	local d

	for d in "${inc[@]}"; do
		ccargs+=("-I$d")
		qargs+=("-I$d")
	done

	if ! cc -E -undef -nostdinc "${ccargs[@]}" -D_OSK=1 -D_UCC=1 \
	     "$src" > "$TMP/cc.i" 2> "$TMP/cc.err"; then
		ccfail=$((ccfail + 1))
		return
	fi
	if ! "$QCPP" -ansi "${qargs[@]}" "$src" "$TMP/q.i" > "$TMP/q.err" 2>&1; then
		qfail=$((qfail + 1))
		printf 'QCPP-ABBRUCH  %s\n' "$label"
		sed 's/^/              /' "$TMP/q.err" | head -3
		return
	fi

	grep -v '^#' "$TMP/cc.i" > "$TMP/cc2.i"
	if [ "$(norm "$TMP/cc2.i")" = "$(norm "$TMP/q.i")" ]; then
		same=$((same + 1))
		return
	fi
	if [ "$(squash "$TMP/cc2.i")" = "$(squash "$TMP/q.i")" ]; then
		spacing=$((spacing + 1))
		spacelist="$spacelist
    $label"
		return
	fi
	diffr=$((diffr + 1))
	difflist="$difflist
    $label"
	printf 'ABWEICHUNG    %s\n' "$label"
	diff <(norm "$TMP/cc2.i" | tr ' ' '\n') \
	     <(norm "$TMP/q.i" | tr ' ' '\n') | head -10 |
		sed 's/^/              /'
}

if [ $# -gt 0 ]; then
	src="$1"; shift
	incs=()
	for a in "$@"; do
		case "$a" in
		-I*) incs+=("${a#-I}");;
		esac
	done
	[ ${#incs[@]} -eq 0 ] && incs=("$DEFS1" "$DEFS2")
	compare "$src" "$src" "${incs[@]}"
else
	echo "=== Differenztest gegen cc -E, Korpus: MWOS-SDK-Header ==="
	n=0
	for h in "$DEFS1"/*.h "$DEFS2"/*.h; do
		[ -f "$h" ] || continue
		case "$h" in
		*"(2)"*) continue;;      # Dubletten im SDK
		esac
		# ZWEIMAL einbinden. Der Test hat den Header vorher genau einmal
		# eingebunden -- und konnte damit strukturell nicht sehen, dass
		# "#pragma once" nicht beachtet wurde (im SDK nutzt es z.B.
		# SRC/DEFS/stdcomp.h). Ein Header mit Wiederholungsschutz muss
		# bei beiden Praeprozessoren einmal erscheinen, einer ohne
		# Schutz bei beiden zweimal -- der Vergleich bleibt also
		# aussagekraeftig und deckt jetzt beides ab.
		printf '#include <%s>\n#include <%s>\n' \
			"$(basename "$h")" "$(basename "$h")" > "$TMP/w.c"
		compare "$TMP/w.c" "$(basename "$h")" "$DEFS1" "$DEFS2"
		n=$((n + 1))
	done
	echo "  $n Header geprueft"
fi

echo
echo "=== Zusammenfassung ==="
echo "  voll identisch: $same   gleicher Tokentext, andere Abstaende: $spacing"
echo "  echte Abweichung: $diffr   qcpp-Abbruch: $qfail   (cc-Abbruch, uebersprungen: $ccfail)"
[ -n "$spacelist" ] && echo "  nur Abstaende:$spacelist"
if [ "$diffr" -gt 0 ] || [ "$qfail" -gt 0 ]; then
	[ -n "$difflist" ] && echo "  abweichend:$difflist"
	exit 1
fi
exit 0
