#!/usr/bin/env bash
# DIE 64-KB-GRENZE DER NICHT-REMOTE-DATEN, an l68 gehalten.
#
# Ein nicht-remoter vsect wird ueber "d16(a6)" angesprochen; mit dem
# $8000-Vorspann deckt ein 16-Bit-Displacement genau die Offsets 0..65535 ab.
# Darueber kann der Code seine Daten nicht mehr erreichen, und l68 bricht
# deshalb ab ("**** fatal - non-remote data allocation exceeds 64k bytes").
#
# ql68 hat diesen Fall bis 2026-09-07 KLAGLOS GEBAUT: 102 Byte Modul mit
# M$Mem = 70000 -- ein Kopf, der stimmt, und Code, der seine Daten nicht
# erreicht. Genau diese Luecke liess beim Datenmodell-Versuch (2026-09-06)
# eine Machbarkeitsprobe gruen aussehen, die mit l68 sofort aufgefallen waere.
#
# GEPRUEFT WIRD AN DER GRENZE, nicht in der Mitte: 65536 muss durchgehen und
# byteidentisch sein, 65540 (die naechste durch vier teilbare Groesse, denn
# qr68 rundet vsects auf ein Vielfaches von 4) muss auf BEIDEN Seiten
# abbrechen. Dazu die Summenregel: reservierte UND initialisierte Daten
# zaehlen zusammen.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd .. && pwd)}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
QR68="${QR68:-$FORGE/Q9-qr68/build/qr68}"
QL68="${QL68:-$REPO/build/ql68}"

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -x "$QR68" ] || die "qr68 fehlt: $QR68"
[ -x "$QL68" ] || die "ql68 fehlt: $QL68"

WORK=/tmp/ql68-datalimit
rm -rf "$WORK"; mkdir -p "$WORK/l" "$WORK/q"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all

gleich=0; abweichend=0

quelle() {   # $1 = reservierte Bytes, $2 = initialisierte Bytes
	{
		printf '         psect   dlim,(1<<8)!1,($80<<8)!1,1,256,start\n'
		printf 'start:\n'
		printf '         moveq   #0,d0\n'
		printf '         rts\n'
		if [ "$2" -gt 0 ]; then
			printf '         vsect\n         dc.b    '
			i=0
			while [ "$i" -lt "$2" ]; do
				[ "$i" -gt 0 ] && printf ','
				printf '1'
				i=$((i + 1))
			done
			printf '\n         ends\n'
		fi
		if [ "$1" -gt 0 ]; then
			printf '         vsect\nublock:  ds.b    %d\n         ends\n' "$1"
		fi
		printf '         ends\n'
	} > "$WORK/p.a"
}

fall() {   # $1 = reserviert, $2 = initialisiert, $3 = "modul" oder "abbruch"
	quelle "$1" "$2"
	"$QR68" "$WORK/p.a" "-o=$WORK/p.r" >/dev/null 2>&1 || die "qr68 (${1}/${2})"
	rm -f "$WORK/l/out" "$WORK/q/out"
	# Gleicher BASISNAME in verschiedenen Verzeichnissen: der Modulname kommt
	# aus dem Ausgabenamen, verschiedene Namen geben immer einen Unterschied.
	arch -x86_64 "$WINE_BIN" cmd /c \
		"set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe Z:$(printf '%s' "$WORK/p.r" | sed 's#/#\\#g') -O=Z:$(printf '%s' "$WORK/l/out" | sed 's#/#\\#g')" \
		> "$WORK/l68.log" 2>&1
	"$QL68" "$WORK/p.r" "-O=$WORK/q/out" > "$WORK/ql68.log" 2>&1
	lda=no; [ -f "$WORK/l/out" ] && lda=ja
	qda=no; [ -f "$WORK/q/out" ] && qda=ja

	if [ "$3" = abbruch ]; then
		if [ "$lda" = no ] && [ "$qda" = no ]; then
			printf '  %6d + %-4d  beide brechen ab\n' "$1" "$2"
			printf '                  l68:  %s\n' "$(sed -n 's/.*fatal - //p' "$WORK/l68.log" | head -1)"
			printf '                  ql68: %s\n' "$(sed -n 's/^ql68: //p' "$WORK/ql68.log" | head -1)"
			gleich=$((gleich + 1))
		else
			printf '  %6d + %-4d  ABWEICHUNG: l68 Datei=%s, ql68 Datei=%s\n' "$1" "$2" "$lda" "$qda"
			[ "$qda" = ja ] && printf '                  ql68 hat gebaut, wo l68 abbricht -- genau der Mangel\n'
			abweichend=$((abweichend + 1))
		fi
		return
	fi

	if [ "$lda" = no ] || [ "$qda" = no ]; then
		printf '  %6d + %-4d  ABWEICHUNG: l68 Datei=%s, ql68 Datei=%s\n' "$1" "$2" "$lda" "$qda"
		sed 's/^/                  /' "$WORK/l68.log" "$WORK/ql68.log" | head -4
		abweichend=$((abweichend + 1))
		return
	fi
	if cmp -s "$WORK/l/out" "$WORK/q/out"; then
		printf '  %6d + %-4d  Modul byteidentisch (%s Byte)\n' "$1" "$2" \
			"$(wc -c < "$WORK/q/out" | tr -d ' ')"
		gleich=$((gleich + 1))
	else
		printf '  %6d + %-4d  ABWEICHUNG in den Modulbytes\n' "$1" "$2"
		cmp "$WORK/l/out" "$WORK/q/out" | head -1 | sed 's/^/                  /'
		abweichend=$((abweichend + 1))
	fi
}

echo "=== an der Grenze (nur reservierte Daten) ==="
fall 65536 0 modul
fall 65540 0 abbruch
echo
echo "=== die Summenregel: reservierte UND initialisierte zaehlen zusammen ==="
fall 65000 400 modul
fall 65400 400 abbruch
echo
echo "=== weit darueber ==="
fall 200000 0 abbruch

echo
echo "  $gleich gleich, $abweichend abweichend"
[ "$abweichend" -eq 0 ]
