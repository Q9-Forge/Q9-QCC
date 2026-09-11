#!/usr/bin/env bash
# "vsect remote" against r68, byte-for-byte. r68 is the oracle.
#
# WHY remote IS NEEDED: a non-remote vsect is addressed through d16(a6)
# angesprochen und passt damit in 64 KB; l68 lehnt mehr ab ("non-remote data
# allocation exceeds 64k bytes"). Remote-Daten zaehlen dort nicht mit -- l68
# legt sie im Datenbereich HINTER die initialisierten Daten, aus dem
# 16-Bit-Fenster heraus. Das ist der Weg, auf dem QCCs Datenmodell-Umbau
# (genullte Globals in den vsect, Faktor ~6 kleinere Module) ueberhaupt durch
# die Binder kommen kann.
#
# Until 2026-09-07 qr68 SILENTLY DISCARDED "remote": ROFs with and
# ohne remote waren byteidentisch, remotestatsiz blieb 0. Genau die
# Fehlerklasse, die dieses Projekt sonst bekaempft -- und sie hat den Umbau
# blockiert, ohne sich zu zeigen.
#
# The final case INTENTIONALLY tests without remote: it must remain
# byteidentisch bleiben, sonst hat die Aenderung den Normalfall beschaedigt.
set -uo pipefail
F=/Volumes/SSD1TB/projects/Q9-Forge
W=/tmp/remote-diff
rm -rf $W; mkdir -p $W; cd $W
source /Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh >/dev/null 2>&1
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all

gleich=0; ab=0

fall() {   # $1 = Name, dann die Quelle auf stdin
	nm=$1
	cat > $nm.a
	arch -x86_64 "$WINE_BIN" cmd /c \
	  "Z: && cd \\tmp\\remote-diff && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe $nm.a -o=r_$nm.r" \
	  > r68_$nm.log 2>&1
	if [ ! -f r_$nm.r ]; then
		printf '  %-12s r68 selbst kommt nicht durch: %s\n' "$nm" "$(head -2 r68_$nm.log | tr '\n' ' ')"
		return
	fi
	# Copy r68's timestamp so only content is compared.
	st=$(python3 -c "
import sys
d=open('r_$nm.r','rb').read()
print('%d,%d,%d,%d,%d,%d' % tuple(d[12:18]))
")
	$F/Q9-qr68/build/qr68k $nm.a -o=q_$nm.r -fdate=$st > qr68_$nm.log 2>&1
	if [ ! -f q_$nm.r ]; then
		printf '  %-12s qr68: %s\n' "$nm" "$(head -2 qr68_$nm.log | tr '\n' ' ')"
		ab=$((ab+1)); return
	fi
	if python3 $F/Q9-qr68/tools/rofcmp.py "$nm:r_$nm.r:q_$nm.r" >/dev/null 2>&1; then
		printf '  %-12s BYTEIDENTISCH (%s Byte)\n' "$nm" "$(wc -c < q_$nm.r | tr -d ' ')"
		gleich=$((gleich+1))
	else
		printf '  %-12s ABWEICHUNG\n' "$nm"
		python3 $F/Q9-qr68/tools/rofcmp.py "$nm:r_$nm.r:q_$nm.r" 2>&1 | head -6 | sed 's/^/      /'
		python3 - <<PYEOF
import struct
for f in ("r_$nm.r","q_$nm.r"):
    d=open(f,"rb").read()
    u=lambda o: struct.unpack(">I", d[o:o+4])[0]
    print("      %-10s statstorage %6d  idatsiz %5d  remotestatsiz %6d" % (f, u(0x14), u(0x18), u(0x2c)))
PYEOF
		ab=$((ab+1))
	fi
}

echo "=== nur ein remote-Block ==="
fall nur_remote <<'EOF'
         psect   pr1,0,0,0,0,0
f:
         moveq   #0,d0
         rts
         vsect remote
rblk:    ds.b    70000
         ends
         ends
EOF

echo "=== gemischt: nicht-remote, initialisiert, remote ==="
fall gemischt <<'EOF'
         psect   pr2,0,0,0,0,0
f:
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

echo "=== remote mit align und mehreren Marken ==="
fall mehrere <<'EOF'
         psect   pr3,0,0,0,0,0
f:
         movea.l #r1,a0
         adda.l  a6,a0
         movea.l #r3,a1
         adda.l  a6,a1
         moveq   #0,d0
         rts
         vsect remote
r1:      ds.b    3
r2:      ds.w    1
         align   16
r3:      ds.l    100
         ends
         ends
EOF

echo "=== ohne remote (darf sich NICHT geaendert haben) ==="
fall ohne <<'EOF'
         psect   pr4,0,0,0,0,0
f:
         movea.l #blk,a0
         adda.l  a6,a0
         moveq   #0,d0
         rts
         vsect
blk:     ds.b    8000
         ends
         ends
EOF

echo
echo "  $gleich gleich, $ab abweichend"
[ "$ab" -eq 0 ]
