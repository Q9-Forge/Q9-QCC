#!/usr/bin/env bash
# FERNDATEN ("vsect remote") gegen l68 -- byteweise. l68 ist das Orakel.
#
# WOFUER: ein nicht-remoter vsect wird ueber d16(a6) angesprochen und passt
# damit in 64 KB; darueber bricht l68 ab (test/datalimit.sh). Ferndaten zaehlen
# dort nicht mit -- l68 legt sie im Datenbereich HINTER die initialisierten
# Daten, aus dem 16-Bit-Fenster heraus. Das ist der Weg, auf dem QCCs
# Datenmodell-Umbau (genullte Globals in den vsect, Faktor ~6 kleinere Module)
# ueberhaupt durch die Binder kommen kann.
#
# ql68 hat Ferndaten bis 2026-09-07 mit "noch nicht gemessen" abgelehnt.
# GEMESSEN WURDE ALLES, WAS DAFUER GEBRAUCHT WIRD:
#   Reihenfolge   nicht-remote uninitialisiert, initialisiert, remote
#                 (8000 / 8 / 70000 ergibt Offsets 0 / 8000 / 8008)
#   M$Mem         Summe aller drei -- remote aendert die Groesse NICHT
#   end           Ende des GANZEN Bereichs, Ferndaten eingeschlossen
#   Sprungtabelle bleibt am Ende der INITIALISIERTEN Daten, auch mit remote
#   Typwoerter    Symbol $0002, Referenz Bit 1
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

WORK=/tmp/ql68-remote
rm -rf "$WORK"; mkdir -p "$WORK/l" "$WORK/q"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
TW="$(printf '%s' "$WORK" | sed 's#/#\\#g')"

gleich=0; ab=0

fall() {   # $1 = Name, Quelle auf stdin
	nm=$1
	cat > "$WORK/$nm.a"
	"$QR68" "$WORK/$nm.a" "-o=$WORK/$nm.r" >/dev/null 2>&1 || die "qr68 ($nm)"
	rm -f "$WORK/l/out" "$WORK/q/out"
	# Gleicher BASISNAME in verschiedenen Verzeichnissen: der Modulname kommt
	# aus dem Ausgabenamen.
	arch -x86_64 "$WINE_BIN" cmd /c \
		"set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe Z:$TW\\$nm.r -O=Z:$TW\\l\\out" \
		> "$WORK/l68_$nm.log" 2>&1
	"$QL68" "$WORK/$nm.r" "-O=$WORK/q/out" > "$WORK/ql68_$nm.log" 2>&1
	lda=no; [ -f "$WORK/l/out" ] && lda=ja
	qda=no; [ -f "$WORK/q/out" ] && qda=ja
	if [ "$lda" != "$qda" ]; then
		printf '  %-14s ABWEICHUNG: l68 Modul=%s, ql68 Modul=%s\n' "$nm" "$lda" "$qda"
		sed 's/^/        /' "$WORK/l68_$nm.log" "$WORK/ql68_$nm.log" | head -4
		ab=$((ab + 1)); return
	fi
	if [ "$lda" = no ]; then
		printf '  %-14s beide brechen ab (%s)\n' "$nm" \
			"$(sed -n 's/.*fatal - //p' "$WORK/l68_$nm.log" | head -1)"
		gleich=$((gleich + 1)); return
	fi
	if cmp -s "$WORK/l/out" "$WORK/q/out"; then
		printf '  %-14s BYTEIDENTISCH (%s Byte, M$Mem %s)\n' "$nm" \
			"$(wc -c < "$WORK/q/out" | tr -d ' ')" \
			"$(python3 -c "
import struct
d=open('$WORK/q/out','rb').read()
print(struct.unpack('>I', d[0x38:0x3c])[0])")"
		gleich=$((gleich + 1))
	else
		printf '  %-14s ABWEICHUNG in den Modulbytes\n' "$nm"
		cmp "$WORK/l/out" "$WORK/q/out" | head -1 | sed 's/^/        /'
		python3 - "$WORK/l/out" "$WORK/q/out" <<'PYEOF'
import struct, sys
for f in sys.argv[1:]:
    d = open(f, "rb").read()
    u = lambda o: struct.unpack(">I", d[o:o+4])[0]
    print("        %-22s %5d Byte  M$Mem %7d  IData-Offset %6d"
          % (f, len(d), u(0x38), u(u(0x40))))
PYEOF
		ab=$((ab + 1))
	fi
}

echo "=== ein Fernblock ueber der 64-KB-Grenze (ohne remote unmoeglich) ==="
fall nur_fern <<'EOF'
         psect   prf,(1<<8)!1,($80<<8)!1,1,256,start
start:
         movea.l #rblk,a0
         adda.l  a6,a0
         move.l  #7,(a0)
         moveq   #0,d0
         rts
         vsect remote
rblk:    ds.b    70000
         ends
         ends
EOF

echo "=== gemischt: nicht-remote, initialisiert, remote ==="
fall gemischt <<'EOF'
         psect   prg,(1<<8)!1,($80<<8)!1,1,256,start
start:
         movea.l #blk,a0
         adda.l  a6,a0
         movea.l #rblk,a1
         adda.l  a6,a1
         movea.l #iblk,a2
         adda.l  a6,a2
         moveq   #0,d0
         rts
         vsect
iblk:    dc.l    $11223344,$55667788
         ends
         vsect
blk:     ds.b    8000
         ends
         vsect remote
rblk:    ds.b    70000
         ends
         ends
EOF

echo "=== end/_enddata mit Ferndaten ==="
fall mit_end <<'EOF'
         psect   pre,(1<<8)!1,($80<<8)!1,1,256,start
start:
         movea.l #end,a0
         adda.l  a6,a0
         movea.l #rblk,a1
         adda.l  a6,a1
         moveq   #0,d0
         rts
         vsect
iblk:    dc.l    $11223344
         ends
         vsect remote
rblk:    ds.b    8000
         ends
         ends
EOF

echo "=== der Normalfall darf sich NICHT geaendert haben ==="
fall ohne_fern <<'EOF'
         psect   prn,(1<<8)!1,($80<<8)!1,1,256,start
start:
         movea.l #blk,a0
         adda.l  a6,a0
         movea.l #iblk,a1
         adda.l  a6,a1
         moveq   #0,d0
         rts
         vsect
iblk:    dc.l    $11223344
         ends
         vsect
blk:     ds.b    8000
         ends
         ends
EOF

echo
echo "  $gleich gleich, $ab abweichend"
[ "$ab" -eq 0 ]
