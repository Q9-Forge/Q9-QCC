#!/usr/bin/env bash
# short-Regressionstest auf dem ECHTEN 68k-Ziel (Q9-Flux-Emulator).
#
# Fuehrt tools/test_short_68k.c durch die komplette Kette
#   qcir -> qir68k -> r68 -> l68 -> ToolShed -> Emulator
# und vergleicht die Ausgabe fallweise mit der Erwartung unten.
#
# Warum das nicht ausschliesslich in runtests.sh gegen qccvm.py laeuft:
# Fall 8/9/10/11 (struct N: "short a; short b; int c;") ist im Host-Orakel
# NICHT pruefbar -- pointer_index() rechnet offset // type_size(tag), und
# das 2-Byte-Feld b (Byteoffset 2) sowie das 4-Byte-Feld c (Byteoffset 4)
# landen dort auf demselben Python-Listenplatz (2/2=1, 4/4=1). Auf echtem
# 68k (Byte-Speicher) ist dasselbe IR korrekt -- genau das prueft dieser
# Testlauf.
#
# Aufruf:   tools/test_short_68k.sh [-k]
#   -k   Zwischendateien in $WORK behalten (zum Nachsehen des .s68/.ir)
# Exit:    0 = alle Faelle gruen, 1 = mindestens einer rot/fehlend
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FLUX="${FLUX:-$REPO/../Q9-Flux-68k}"
IMG="${IMG:-$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"
WORK="${WORK:-/tmp/qcc-short68k}"
KEEP=0
[ "${1:-}" = "-k" ] && KEEP=1

EXPECT_IDS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
EXPECT_VALS=(2 65535 301 10 7 200 8 11 22 99999 8 65533 34464 40000 34464)
EXPECT_WHAT=(
	"sizeof(short)"
	"lokales short, negativ (65535 statt -1)"
	"globales short, Arithmetik"
	"short-Array, Schrittweite 2"
	"short ueber Zeiger schreiben/lesen"
	"struct M (int c; short a; short b;): Feld b"
	"sizeof(struct M)"
	"struct N (short a; short b; int c;): Feld a"
	"struct N: Feld b (NUR auf echtem 68k pruefbar, s. Kopf)"
	"struct N: Feld c (NUR auf echtem 68k pruefbar, s. Kopf)"
	"sizeof(struct N)"
	"short als Parameter per Wert (-3 -> 65533)"
	"short-Rueckgabe, NARROWH (100000 -> 34464)"
	"unsigned short"
	"expliziter Cast (short)100000, eigener Emissionsort im castExpr"
)

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -x "$REPO/build/qcir" ]    || die "build/qcir fehlt -- erst bauen"
[ -x "$REPO/build/qir68k" ] || die "build/qir68k fehlt"
[ -f "$IMG" ]                || die "Image nicht gefunden: $IMG"
[ -d "$FLUX" ]               || die "Q9-Flux nicht gefunden: $FLUX"

rm -rf "$WORK"; mkdir -p "$WORK"
cd "$WORK"
cp "$REPO/runtime/os9/q9_cstart.a" "$REPO/runtime/os9/q9defs.d" .

# shellcheck disable=SC1091
source /Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh >/dev/null 2>&1 \
	|| die "OS-9-Toolchain nicht ladbar"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
w() { arch -x86_64 "$WINE_BIN" cmd /c "$1" >/dev/null 2>&1; }

echo "== 1/5 uebersetzen =="
"$REPO/build/qcir" "@$REPO/tools/test_short_68k.c" > t.ir 2> t.err
rc=$?
last="$(tail -1 t.ir)"
if [ $rc -ne 0 ] || [ "$last" != "OK" ]; then
	echo "  qcir: rc=$rc Schlusswort=$last"
	head -5 t.err
	die "Testprogramm uebersetzt nicht -- das ist selbst schon ein Befund"
fi
echo "  ok ($(wc -l < t.ir | tr -d ' ') IR-Zeilen)"

echo "== 2/5 Backend + Assembler + Linker =="
"$REPO/build/qir68k" t.ir t.s68k -os9 >/dev/null || die "qir68k"
w 'Z: && cd \tmp\qcc-short68k && set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\r68.exe q9_cstart.a -o=q9_cstart.r'
[ -f q9_cstart.r ] || die "r68 auf q9_cstart.a (liegt q9defs.d daneben?)"
w 'Z: && cd \tmp\qcc-short68k && set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\r68.exe t.s68k -o=t.r'
[ -f t.r ] || die "r68 auf t.s68k"
w 'set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\l68.exe -a Z:\tmp\qcc-short68k\q9_cstart.r Z:\tmp\qcc-short68k\t.r -l=M:\OS9\68020\LIB\clib.l -l=M:\OS9\68020\LIB\os_lib.l -l=M:\OS9\68000\LIB\sys.l -M=64K -o=Z:\tmp\qcc-short68k\q9_shorttest'
[ -f q9_shorttest ] || die "l68"
echo "  ok ($(wc -c < q9_shorttest | tr -d ' ') Byte Modul)"

echo "== 3/5 ins Image =="
"$MWOS_TOOLSHED_OS9" copy -r q9_shorttest "$IMG,/CMDS/q9_shorttest" >/dev/null 2>&1 \
	|| die "ToolShed-copy (laeuft ein Emulator auf diesem Image?)"
echo "  ok"

echo "== 4/5 im Emulator ausfuehren =="
cat > run.exp <<'EOF'
log_file -a LOGFILE
set timeout 240
set send_slow {1 .003}
set prompt {[#$] ?$}
spawn ./build/macos/q9.exe --rom MWOSDIR/OS9/68030/PORTS/Q9/CMDS/BOOTOBJS/ROMBUG/romimage.dev.running.BIN --cf IMAGE
expect "devices online"
send "\r"
set li 0
for {set i 0} {$i < 10 && !$li} {incr i} {
    expect {
        "User name?:" { send "super\r"; exp_continue }
        -re {Password[^\r\n]*:} { send "Al35uUbC\r"; exp_continue }
        -re $prompt { set li 1 }
        timeout { send "\r" }
    }
}
if {!$li} { send_log "\nTEST: LOGIN FAILED\n"; exit 1 }
send -s "/dd/CMDS/q9_shorttest\r"
expect {
    -re $prompt          { }
    -re {Stack Overflow} { send_log "\nTEST: STACK OVERFLOW\n" }
    -re {PMMU}           { send_log "\nTEST: PMMU\n" }
    timeout              { send_log "\nTEST: TIMEOUT\n"; exit 1 }
}
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$IMG|" \
	-e "s|MWOSDIR|/Volumes/SSD1TB/projects/MWOS|" run.exp && rm -f run.exp.bak
rm -f run.log
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f run.log ] || die "kein Emulator-Log -- lief der Emulator?"
echo "  ok"

echo "== 5/5 Auswertung =="
tr -d '\r' < run.log | grep -oE '^[0-9]+:[0-9]+$' > got.txt || true
crash=""
grep -q 'TEST: PMMU'           run.log && crash="PMMU-Fault"
grep -q 'TEST: STACK OVERFLOW' run.log && crash="Stack Overflow"

fail=0
last_seen=0
for i in "${!EXPECT_IDS[@]}"; do
	id="${EXPECT_IDS[$i]}"; want="${EXPECT_VALS[$i]}"; what="${EXPECT_WHAT[$i]}"
	got="$(grep -m1 "^$id:" got.txt | cut -d: -f2)"
	if [ -z "$got" ]; then
		printf '  FEHLT  %-2s %-55s (erwartet %s)\n' "$id" "$what" "$want"; fail=1
	elif [ "$got" != "$want" ]; then
		printf '  FAIL   %-2s %-55s erwartet %s, bekommen %s\n' "$id" "$what" "$want" "$got"; fail=1
		last_seen=$id
	else
		printf '  ok     %-2s %-55s = %s\n' "$id" "$what" "$got"; last_seen=$id
	fi
done

echo
if [ -n "$crash" ]; then
	echo "ABSTURZ ($crash) -- letzter vollstaendiger Fall: $last_seen,"
	echo "der Absturz liegt also in Fall $((last_seen + 1)) (siehe Liste oben)."
	fail=1
fi
[ $KEEP -eq 1 ] && echo "Zwischendateien: $WORK"
if [ $fail -eq 0 ]; then echo "ALLE ${#EXPECT_IDS[@]} FAELLE GRUEN"; else echo "TEST ROT"; fi
exit $fail
