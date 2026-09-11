#!/usr/bin/env bash
# Bootstrap-Kette mit ausschliesslich eigenen Werkzeugen:
#
#   qcpp (+ q9-cpp/include)  ->  QCC  ->  IR
#
# Kein xcc, kein Wine, kein Python. Der bisherige Weg brauchte beides:
#
#   xcc -pp Data/qcc_p.c > build/qcc_p.xcc.i        (Microware, ueber Wine)
#   python3 tools/bootstrap_prepare.py ...          (Header-Vorspann wegschneiden)
#
# Wovon die Python-Stufe befreit hat und warum das jetzt entfaellt:
#   - Expandierter SDK-Header-Vorspann     -> eigene Header in q9-cpp/include
#   - stderr als (&_niob[2]) bzw. __stderrp -> eigenes stdio.h deklariert es
#   - Apples __builtin___sprintf_chk        -> kommt ohne Apple-Header nicht vor
#   - Leerzeichen um "." (xcc bei Makroexpansion) -> qcpp setzt keine
#   - "((void)0);" aus QCC_OUTPUT_FLUSH()   -> der Generator gibt jetzt
#     "(void)0" aus (Q9-Parsec Source/codegen.cpp), was QCCs voidCastStmt
#     entspricht
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"   # .../Q9-FRONTEND-C/q9-qcpp
QCC="$(cd "$REPO/../.." && pwd)"                         # .../Q9-QCC
QCC="$(cd "$REPO/.." && pwd)"                             # .../Q9-QCC
cd "$QCC"

SRC="$QCC/Q9-FRONTEND-C/q9-qcir/data/qcc_p.c"
OUT="$REPO/build/qcc_p.q9.c"
IR="$REPO/build/qcc_p.q9.ir"
ERR="$REPO/build/qcc_p.q9.err"
REF=build/qcc_p.bootstrap.ir      # Referenz des alten xcc+Python-Weges, falls da

[ -x "$REPO/build/qcpp" ] || { echo "FEHLER: qcpp fehlt (make)"; exit 2; }
[ -x build/qcir ] || { echo "FEHLER: build/qcir fehlt"; exit 2; }
mkdir -p "$REPO/build"

echo "== 1/3 vorverarbeiten (qcpp + eigene Header) =="
"$REPO/build/qcpp" -I"$REPO/include" "$SRC" "$OUT"
echo "  $OUT ($(wc -c < "$OUT" | tr -d ' ') Byte)"

echo "== 2/3 QCC uebersetzt das Ergebnis =="
if ! ./build/qcir "@$OUT" > "$IR" 2> "$ERR"; then
	echo "FEHLGESCHLAGEN -- Meldungen:"
	head -20 "$ERR"
	exit 1
fi
last="$(tail -1 "$IR")"
msgs="$(wc -l < "$ERR" | tr -d ' ')"
echo "  $(wc -l < "$IR" | tr -d ' ') IR-Zeilen, Schlusswort $last, $msgs Meldungen"
[ "$last" = OK ] || { echo "FEHLER: Schlusswort ist nicht OK"; exit 1; }
[ "$msgs" = 0 ] || { echo "FEHLER: es gab Meldungen"; head -20 "$ERR"; exit 1; }

echo "== 3/3 Vergleich mit dem alten xcc+Python-Weg =="
if [ ! -f "$REF" ]; then
	echo "  uebersprungen -- $REF ist nicht da"
	echo "  (erzeugen mit tools/build_xcc_bootstrap.sh, braucht Wine)"
	exit 0
fi

# Erwartet wird BYTEIDENTITAET. Zwischenstand am 2026-09-02, der die Sache
# erklaert: solange der Generator "((void)0)" ausgab, entfernte
# bootstrap_prepare.py diese drei Anweisungen komplett, waehrend dieser Weg sie
# behielt -- Unterschied genau 3x (PUSH 0 / DROP). Seit der Generator
# "(void)0" ausgibt (Q9-Parsec Source/codegen.cpp), greift das Muster im
# Python-Skript nicht mehr, beide Wege behalten die Anweisungen, und das IR
# ist auf das Byte gleich. Eine Abweichung hier ist also ein Befund und kein
# bekannter Rest.
if cmp -s "$REF" "$IR"; then
	echo "  byteidentisch zum alten Weg ($(wc -c < "$IR" | tr -d ' ') Byte)"
	echo
	echo "Bootstrap laeuft ohne xcc, ohne Wine und ohne Python."
	exit 0
fi
echo "  ABWEICHUNG:"
diff "$REF" "$IR" | head -30 | sed 's/^/  /'
exit 1
