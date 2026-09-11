#!/usr/bin/env bash
# Differenztest fuer die Schalter von l68, die das MODUL veraendern.
#
# Zwei Fragen je Fall, und die zweite ist die wichtigere:
#   1. Kommt ql68 auf dieselben Bytes wie l68?
#   2. Aendert der Schalter ueberhaupt etwas?
# Deshalb wird jeder Fall zusaetzlich gegen den l68-Lauf OHNE Schalter
# gehalten. Ein Fall, der bei beiden Bindern wirkungslos bleibt, ist kein
# Nachweis -- er stuende sonst gruen in der Liste, waehrend der Schalter
# in Wahrheit ungeprueft ist. Solche Faelle meldet das Skript als "ohne
# Wirkung" und zaehlt sie getrennt.
#
# Die Proben unter test/opt/ haben bewusst KRUMME Datengroessen (oa: 6
# Byte dc / 5 Byte ds, ob: 6/1), damit -b= sichtbar wird; oc hat gar keine
# Daten -- daran zeigt sich, ob ein leerer Block trotzdem auf die Grenze
# rueckt.
#
# MESSFALLE (aus der qr68/ql68-Arbeit): der MODULNAME kommt aus -O=.
# Beide Binder muessen deshalb denselben Basisnamen schreiben, nur in
# verschiedene Verzeichnisse -- sonst unterscheiden sich Name und CRC und
# das sieht wie ein Binderfehler aus.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QL68="$PWD/build/ql68"
[ -x "$QL68" ] || { echo "FEHLER: build/ql68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[ -d "$MWOS/OS9" ] || { echo "uebersprungen: $MWOS fehlt"; exit 0; }

TMP="${KEEP:-$(mktemp -d /tmp/ql68-opt.XXXXXX)}"
mkdir -p "$TMP/l" "$TMP/q" "$TMP/base"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
TMPWIN="Z:$(printf '%s' "$TMP" | sed 's#/#\\#g')"
wrun() { arch -x86_64 "$WINE_BIN" cmd /c "Z: && cd ${TMPWIN#*:} && set PATH=M:\\DOS\\BIN;%PATH% && $*" 2>&1 | tr -d '\r'; }

# --- Die Proben einmal assemblieren. ---
for f in test/opt/*.a; do
	b="$(basename "$f" .a)"
	cp "$f" "$TMP/"
	wrun "M:\\DOS\\BIN\\r68.exe $b.a -o=$b.r -q" >/dev/null
	[ -s "$TMP/$b.r" ] || { echo "FEHLER: r68 kommt an $b.a nicht durch"; exit 2; }
done

# --- Die Faelle: <Eingaben>|<Schalter> ---
# Die Eingaben sind Namen aus test/opt/ ohne Endung; oa traegt den
# Wurzel-psect und muss deshalb immer vorne stehen.
CASES="
od|
od|-M=1
od|-M=1K
od|-M=100
od|-M=100K
od|-b=2
od|-b=4
od|-b=8
od|-b=16
od|-x=2
od|-x=4
od|-x=8
od|-x=16
od|-S
od|-R=5
od|-R=255
od|-S -R=17
od|-b=8 -x=16
od|-M=4K -b=4 -S
od|-e=7
od|-t=os9_68k
oa ob|
oa ob|-b=2
oa ob|-b=4
oa ob|-b=8
oa ob|-b=16
oa ob|-x=8
oa ob|-M=2K
oa ob|-S -R=3
oa ob oc|
oa ob oc|-b=4
oa ob oc|-b=16
oa ob oc|-x=16
oa ob oc|-b=8 -x=8
"

echo "=== ql68 gegen l68, Schalter am Modul ==="
ok=0
bad=0
inert=0
lfail=0

while IFS='|' read -r rofs sw; do
	[ -n "$rofs" ] || continue

	# Eingabeliste in beiden Schreibweisen.
	lin=""
	qin=""
	for b in $rofs; do
		lin="$lin $b.r"
		qin="$qin $TMP/$b.r"
	done
	key="$(printf '%s' "$rofs" | tr -d ' ')"
	label="$(printf '%-12s %s' "$rofs" "${sw:-(ohne)}")"

	wrun "M:\\DOS\\BIN\\l68.exe $sw $lin -O=l\\omod" >/dev/null
	if [ ! -s "$TMP/l/omod" ]; then
		echo "  $label   l68 kommt nicht durch"
		lfail=$((lfail + 1))
		continue
	fi

	# Wirkungsnachweis: der schalterlose Lauf derselben Eingaben.
	if [ -z "$sw" ]; then
		cp "$TMP/l/omod" "$TMP/base/$key"
	fi

	if ! "$QL68" $sw $qin "-O=$TMP/q/omod" > "$TMP/msg" 2>&1; then
		echo "  $label   ql68 bricht ab:"
		sed 's/^/      /' "$TMP/msg" | head -3
		bad=$((bad + 1))
		continue
	fi

	if ! cmp -s "$TMP/l/omod" "$TMP/q/omod"; then
		echo "  $label   ABWEICHUNG"
		python3 tools/modcmp.py "$TMP/l/omod" "$TMP/q/omod" 2>/dev/null |
			sed 's/^/      /' | head -6
		bad=$((bad + 1))
		continue
	fi

	# Gleich -- aber wirkt der Schalter auch?
	if [ -n "$sw" ] && [ -s "$TMP/base/$key" ] &&
	   cmp -s "$TMP/l/omod" "$TMP/base/$key"; then
		echo "  $label   gleich, aber OHNE WIRKUNG (wie ohne Schalter)"
		inert=$((inert + 1))
		continue
	fi
	ok=$((ok + 1))
done <<< "$CASES"

echo
echo "  $ok gleich, $bad abweichend, $inert ohne Wirkung, $lfail von l68 verweigert"
# "Ohne Wirkung" ist kein Fehlschlag des Binders, aber ein ungeprueftes
# Stueck -- es soll auffallen, statt in einer gruenen Zahl zu verschwinden.

# --- Teil 2: die Schalter, die das Modul NICHT veraendern sollen ---
#
# ql68 nimmt sie an und uebergeht sie. Bisher stand das nur als Behauptung
# im Kommentar -- hier wird es gemessen, und zwar mit UMGEKEHRTER
# Erwartung: l68 MUSS dieselben Bytes liefern wie ohne den Schalter.
# Tut es das nicht, wirkt der Schalter doch, und ql68 uebergeht ihn zu
# Unrecht. Zusaetzlich muss ql68 selbst denselben Lauf schaffen.
INERT="
-a
-c
-i
-g
-j
-v
-w
-m
-s
-q
-f=pr
-mte
-mtw
-mtq
-swam
-gwj
"

echo
echo "=== Schalter, die das Modul nicht veraendern duerfen ==="
iok=0
iwirkt=0
ibad=0

# Grundlauf ohne Schalter, unter demselben Namen.
wrun "M:\\DOS\\BIN\\l68.exe od.r -O=base\\omod" >/dev/null
if [ ! -s "$TMP/base/omod" ]; then
	echo "  FEHLER: der Grundlauf selbst kommt nicht durch"
	exit 2
fi

while IFS= read -r sw; do
	[ -n "$sw" ] || continue
	label="$(printf '%-8s' "$sw")"

	wrun "M:\\DOS\\BIN\\l68.exe $sw od.r -O=l\\omod" >/dev/null
	if [ ! -s "$TMP/l/omod" ]; then
		echo "  $label   l68 kommt damit nicht durch"
		ibad=$((ibad + 1))
		continue
	fi
	if ! cmp -s "$TMP/l/omod" "$TMP/base/omod"; then
		echo "  $label   WIRKT DOCH -- l68 liefert andere Bytes als ohne"
		iwirkt=$((iwirkt + 1))
		continue
	fi
	if ! "$QL68" $sw "$TMP/od.r" "-O=$TMP/q/omod" > "$TMP/msg" 2>&1; then
		echo "  $label   ql68 bricht ab:"
		sed 's/^/      /' "$TMP/msg" | head -2
		ibad=$((ibad + 1))
		continue
	fi
	if ! cmp -s "$TMP/l/omod" "$TMP/q/omod"; then
		echo "  $label   ABWEICHUNG gegen l68"
		ibad=$((ibad + 1))
		continue
	fi
	iok=$((iok + 1))
done <<< "$INERT"

echo
echo "  $iok wirkungslos wie erwartet, $iwirkt wirken doch, $ibad Fehler"


# --- Teil 3: -r ohne Basis und die Antwortdatei -z= ---
#
# Beide Formen kommen im SDK-Korpus NICHT vor -- l68 kennt sie trotzdem,
# und ein Binder, der sie nicht kennt, waere nicht vollstaendig.
echo
echo "=== -r ohne Basis und -z= (Antwortdatei) ==="
printf 'od.r\n'          > "$TMP/z1.txt"
printf 'od.r\n-M=8K\n'   > "$TMP/z2.txt"
printf -- '-M=8K\nod.r\n' > "$TMP/z3.txt"

zok=0
zbad=0
zcase() {
	label="$1"
	shift
	rm -f "$TMP/l/omod" "$TMP/q/omod"
	wrun "M:\\DOS\\BIN\\l68.exe $* -O=l\\omod" >/dev/null
	if [ ! -s "$TMP/l/omod" ]; then
		echo "  $(printf '%-22s' "$label") l68 kommt nicht durch"
		zbad=$((zbad + 1))
		return
	fi
	if ! (cd "$TMP" && "$QL68" $* "-O=$TMP/q/omod") > "$TMP/msg" 2>&1; then
		echo "  $(printf '%-22s' "$label") ql68 bricht ab:"
		sed 's/^/      /' "$TMP/msg" | head -2
		zbad=$((zbad + 1))
		return
	fi
	if cmp -s "$TMP/l/omod" "$TMP/q/omod"; then
		echo "  $(printf '%-22s' "$label") gleich ($(wc -c < "$TMP/l/omod" | tr -d ' ') Byte)"
		zok=$((zok + 1))
	else
		echo "  $(printf '%-22s' "$label") ABWEICHUNG"
		zbad=$((zbad + 1))
	fi
}

# "-r" muss dasselbe sein wie "-r=0" (an l68 gemessen: beide 54 Byte).
zcase "-r (Vorgabe 0)"      -r od.r
zcase "-r=0"                -r=0 od.r
zcase "-z= nur die Datei"   -z=z1.txt
zcase "-z= Datei + Option"  -z=z2.txt
zcase "-z= Option zuerst"   -z=z3.txt

# Wirkungsnachweis: die Option AUS der Antwortdatei muss ankommen. Sonst
# waeren die drei -z=-Faelle nur deshalb gruen, weil beide Binder die
# Datei gleichermassen ignorieren.
rm -f "$TMP/q/za" "$TMP/q/zb"
(cd "$TMP" && "$QL68" -z=z1.txt "-O=$TMP/q/za") >/dev/null 2>&1
(cd "$TMP" && "$QL68" -z=z2.txt "-O=$TMP/q/zb") >/dev/null 2>&1
if cmp -s "$TMP/q/za" "$TMP/q/zb"; then
	echo "  -M=8K aus der Antwortdatei: OHNE WIRKUNG -- die Datei wird nicht gelesen"
	zbad=$((zbad + 1))
else
	echo "  -M=8K aus der Antwortdatei: wirkt (M\$Stack \$64 -> \$2064)"
	zok=$((zok + 1))
fi

echo
echo "  $zok gleich, $zbad abweichend"

exit $([ "$bad" -eq 0 ] && [ "$lfail" -eq 0 ] &&
       [ "$iwirkt" -eq 0 ] && [ "$ibad" -eq 0 ] &&
       [ "$zbad" -eq 0 ] && echo 0 || echo 1)
