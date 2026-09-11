#!/usr/bin/env bash
# Differential test: qr68 against Microware r68.
#
# r68 is the oracle. Comparison is BYTE-FOR-BYTE; only the six
# timestamp bytes in the ROF header (offset 12..17) are excluded because they
# are the only non-reproducible r68 output (two runs differed in one seconds byte).
#
# Usage:
#   ./test/difftest.sh              -- built-in probes
#   ./test/difftest.sh file.a ...   -- real sources
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QR68=build/qr68k
[ -x "$QR68" ] || { echo "FEHLER: $QR68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
TMP="$(mktemp -d /tmp/qr68-diff.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
TMPWIN="$(printf '%s' "$TMP" | sed 's#/#\\#g')"

# Run r68; read its timestamp from the output file and pass it to qr68 with
# read and pass to qr68 with -fdate=; this keeps comparison strict without
# excluding the six bytes.
run_r68() {
	arch -x86_64 "$WINE_BIN" cmd /c \
		"Z: && cd $TMPWIN && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe $1 -o=$2" \
		>> "$TMP/wine.log" 2>&1
}

pairs=""
n=0

# probe <name> <quelltext>
probe() {
	printf '%s' "$2" > "$TMP/$1.a"
	pairs="$pairs $1"
	n=$((n + 1))
}

if [ $# -gt 0 ]; then
	for f in "$@"; do
		base="$(basename "$f" .a)"
		cp "$f" "$TMP/$base.a"
		pairs="$pairs $base"
		n=$((n + 1))
	done
else
	probe leer '         psect   mini,0,0,1,0,0
start    rts
         ends
'
	probe kurz '         psect   mi,0,0,1,0,0
         rts
         ends
'
	probe lang '         psect   minixyz,0,0,1,0,0
         rts
         ends
'
	probe zwei '         psect   mi,0,0,1,0,0
         rts
         nop
         ends
'
	probe global '         psect   mi,0,0,1,0,0
start:   rts
         ends
'
	probe extern '         psect   mi,0,0,1,0,0
         jsr      fremd
         rts
         ends
'
	# Also checks alphabetical global ordering and grouping of references by name.
	probe mehrere '         psect   mi,0,0,1,0,0
a:       rts
b:       rts
c:       nop
         jsr      x1
         jsr      x2
         jsr      x1
         ends
'
	# Separate address spaces for initialized and reserved data, including
	# ordering independent of source order.
	probe vsect '         psect   mi,0,0,1,0,0
         vsect
wert:    dc.l    5
puffer:  ds.b    16
         ends
start:   rts
         ends
'
	probe vsect2 '         psect   mi,0,0,1,0,0
         vsect
d1:      dc.l    1
d2:      dc.l    2
u1:      ds.b    4
u2:      ds.b    4
         ends
         rts
         ends
'
	probe ausdruck '         psect   mi,0,0,1,0,0
WERT     equ     $10
ZWEI     set     WERT>>3
         vsect
tab:     dc.l    WERT,WERT*2,(WERT!1)&$ff,-WERT
kette:   dc.b    "abc",0
         ends
         rts
         ends
'
fi

echo "=== Differenztest qr68 gegen r68 ($n Faelle) ==="
args=""
fail=0
for name in $pairs; do
	if ! run_r68 "$name.a" "$name.r" || [ ! -f "$TMP/$name.r" ]; then
		echo "  $name: r68 FEHLGESCHLAGEN -- uebersprungen"
		grep -iE "error" "$TMP/wine.log" | tail -3 | sed 's/^/      /'
		continue
	fi
	# Copy the timestamp from r68 output.
	stamp="$(python3 - "$TMP/$name.r" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
print(",".join(str(b) for b in d[12:18]))
PY
)"
	if ! "$QR68" "-fdate=$stamp" "$TMP/$name.a" "$TMP/$name.q" > "$TMP/$name.msg" 2>&1; then
		echo "  $name: qr68 bricht ab:"
		sed 's/^/      /' "$TMP/$name.msg" | head -3
		fail=1
		continue
	fi
	args="$args $name:$TMP/$name.r:$TMP/$name.q"
done

python3 tools/rofcmp.py $args || fail=1
exit $fail
