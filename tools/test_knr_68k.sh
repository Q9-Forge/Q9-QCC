#!/usr/bin/env bash
# K&R-Funktionsdefinitionen auf dem ECHTEN 68k-Ziel (Q9-Flux-Emulator).
#
# Fuehrt tools/test_knr_68k.c durch die komplette Kette
#   qcir -> qir68k -> r68 -> l68 -> ToolShed -> Emulator
# und vergleicht die Ausgabe fallweise mit der Erwartung unten.
#
# Warum zusaetzlich zu runtests.sh: ein K&R-Parameter bekommt seinen Typ in
# tc_krend NACHTRAEGLICH aus der Deklaration hinter der Klammer. Eine falsche
# BREITE oder Byte-Reihenfolge sieht die Host-VM nicht -- sie modelliert
# typisierte Zellen statt Bytes. Der big-endian 68030 sieht sie sofort.
#
# Aufruf:   tools/test_knr_68k.sh [-k]
#   -k   Zwischendateien in $WORK behalten (zum Nachsehen des .s68/.ir)
# Exit:    0 = alle Faelle gruen, 1 = mindestens einer rot/fehlend
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FLUX="${FLUX:-$REPO/../Q9-Flux/Q9-Flux-68k}"
IMG="${IMG:-$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"
WORK="${WORK:-/tmp/qcc-knr68k}"
KEEP=0
[ "${1:-}" = "-k" ] && KEEP=1

# id -> erwarteter Wert. Reihenfolge = Reihenfolge im Testprogramm.
EXPECT_IDS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20)
EXPECT_VALS=(7 42 23 567 0 0 5 121 7 34 6 6 42 120 16 33 50 10 123 -7)
EXPECT_WHAT=(
	"Grundform: int a; int b;"
	"mehrere Deklaratoren in einer Zeile"
	"Deklarationen VERTAUSCHT (Klammer ist massgeblich)"
	"gemischte Breiten char/short/int, vertauscht"
	"schmale Typen negativ: gleich wie ANSI (Differenz)"
	"unsigned char: gleich wie ANSI (Differenz)"
	"char* als K&R-Parameter"
	"char b[] ist ein Zeiger"
	"struct* als K&R-Parameter"
	"ganze struct per Wert als K&R-Parameter"
	"double als K&R-Parameter"
	"derselbe double ueber ANSI (muss gleich sein)"
	"Name ohne Deklaration ist implizit int"
	"Rekursion ueber eine K&R-Funktion"
	"K&R ruft ANSI"
	"void-Rueckgabe als K&R"
	"unsigned int als K&R-Parameter"
	"K&R-Aufruf als Argument einer K&R-Funktion"
	"drei Parameter, Reihenfolge ueber die Klammer"
	"negativer int-Parameter"
)

# Nach dem Umbau vom 2026-09-12 liegen die Werkzeuge in eigenen
# Teilprojekten, nicht mehr unter Q9-QCC/build.
QCIR="${QCIR:-$REPO/Q9-FRONTEND-C/q9-qcir/build/qcir}"
QIR68K="${QIR68K:-$REPO/Q9-BACKEND-68K/q9-qir68k/build/qir68k}"

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -x "$QCIR" ]  || die "$QCIR fehlt -- erst bauen"
[ -x "$QIR68K" ] || die "$QIR68K fehlt"
[ -f "$IMG" ]                    || die "Image nicht gefunden: $IMG"
[ -d "$FLUX" ]                   || die "Q9-Flux nicht gefunden: $FLUX"

rm -rf "$WORK"; mkdir -p "$WORK"
cd "$WORK"
cp "$REPO/runtime/os9/q9_cstart.a" "$REPO/runtime/os9/q9defs.d" .

# shellcheck disable=SC1091
source /Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh >/dev/null 2>&1 \
	|| die "OS-9-Toolchain nicht ladbar"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
# r68/l68 erwarten das Prefix mit M: = MWOS (nicht das os9-Prefix, wo M: auf
# projects/ zeigt) -- siehe tools/build_xcc_bootstrap.sh.
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
w() { arch -x86_64 "$WINE_BIN" cmd /c "$1" >/dev/null 2>&1; }

echo "== 1/5 uebersetzen =="
"$QCIR" "@$REPO/tools/test_knr_68k.c" > t.ir 2> t.err
rc=$?
last="$(tail -1 t.ir)"
if [ $rc -ne 0 ] || [ "$last" != "OK" ]; then
	echo "  qcir: rc=$rc Schlusswort=$last"
	head -5 t.err
	die "Testprogramm uebersetzt nicht -- das ist selbst schon ein Befund"
fi
echo "  ok ($(wc -l < t.ir | tr -d ' ') IR-Zeilen)"

echo "== 2/5 Backend + Assembler + Linker =="
"$QIR68K" t.ir t.s68k -os9 >/dev/null || die "qir68k"
w 'Z: && cd \tmp\qcc-knr68k && set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\r68.exe q9_cstart.a -o=q9_cstart.r'
[ -f q9_cstart.r ] || die "r68 auf q9_cstart.a (liegt q9defs.d daneben?)"
w 'Z: && cd \tmp\qcc-knr68k && set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\r68.exe t.s68k -o=t.r'
[ -f t.r ] || die "r68 auf t.s68k"
w 'set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\l68.exe -a Z:\tmp\qcc-knr68k\q9_cstart.r Z:\tmp\qcc-knr68k\t.r -l=M:\OS9\68020\LIB\clib.l -l=M:\OS9\68020\LIB\os_lib.l -l=M:\OS9\68000\LIB\sys.l -M=64K -o=Z:\tmp\qcc-knr68k\q9_knrtest'
[ -f q9_knrtest ] || die "l68"
echo "  ok ($(wc -c < q9_knrtest | tr -d ' ') Byte Modul)"

echo "== 3/5 ins Image =="
"$MWOS_TOOLSHED_OS9" copy -r q9_knrtest "$IMG,/CMDS/q9_knrtest" >/dev/null 2>&1 \
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
send -s "/dd/CMDS/q9_knrtest\r"
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
# Nur die "<id>:<wert>"-Paare, robust gegen CR und Boot-Rauschen.
tr -d '\r' < run.log | grep -oE '^[0-9]+:-?[0-9]+$' > got.txt || true
crash=""
grep -q 'TEST: PMMU'           run.log && crash="PMMU-Fault"
grep -q 'TEST: STACK OVERFLOW' run.log && crash="Stack Overflow"

fail=0
last_seen=0
for i in "${!EXPECT_IDS[@]}"; do
	id="${EXPECT_IDS[$i]}"; want="${EXPECT_VALS[$i]}"; what="${EXPECT_WHAT[$i]}"
	got="$(grep -m1 "^$id:" got.txt | cut -d: -f2)"
	if [ -z "$got" ]; then
		printf '  FEHLT  %-2s %-40s (erwartet %s)\n' "$id" "$what" "$want"; fail=1
	elif [ "$got" != "$want" ]; then
		printf '  FAIL   %-2s %-40s erwartet %s, bekommen %s\n' "$id" "$what" "$want" "$got"; fail=1
		last_seen=$id
	else
		printf '  ok     %-2s %-40s = %s\n' "$id" "$what" "$got"; last_seen=$id
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
