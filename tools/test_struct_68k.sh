#!/usr/bin/env bash
# Struct-Regressionstest auf dem ECHTEN 68k-Ziel (Q9-Flux-Emulator).
#
# Fuehrt tools/test_struct_68k.c durch die komplette Kette
#   qcc_p -> qcc_backend -> r68 -> l68 -> ToolShed -> Emulator
# und vergleicht die Ausgabe fallweise mit der Erwartung unten.
#
# Warum das nicht in runtests.sh gegen qccvm.py laeuft: die Host-VM ist fuer
# Structs kein gueltiges Orakel (siehe Kopf von test_struct_68k.c). Ein Fall,
# den sie als 0 meldet, kann auf 68k voellig richtig sein -- und umgekehrt.
#
# Aufruf:   tools/test_struct_68k.sh [-k]
#   -k   Zwischendateien in $WORK behalten (zum Nachsehen des .s68/.ir)
# Exit:    0 = alle Faelle gruen, 1 = mindestens einer rot/fehlend
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FLUX="${FLUX:-$REPO/../Q9-Flux-68k}"
IMG="${IMG:-$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"
WORK="${WORK:-/tmp/qcc-struct68k}"
KEEP=0
[ "${1:-}" = "-k" ] && KEEP=1

# id -> erwarteter Wert. Reihenfolge = Reihenfolge im Testprogramm.
EXPECT_IDS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35)
EXPECT_VALS=(98 3 11 98 98 98 3 98 5 7 105 63 105 2 1 41 42 7 88 1 71 72 73 74 75 60 55 56 81 82 83 84 85 86 87)
EXPECT_WHAT=(
	"Zuweisung lokal -> lokal"
	"Initialisierung aus Variable"
	"Initialisierung aus Funktionsrueckgabe"
	"Zuweisung lokal -> global"
	"Initialisierung aus global"
	"Zuweisung an Array-Element"
	"Lesen eines ganzen Array-Elements"
	"Struct als Parameter per Wert"
	"Wertsemantik: Original unveraendert"
	"ternaer + Praedekrement + Rueckgabe"
	"derselbe Pfad, zweiter Pop"
	"leerer Stapel -> anderer Ternaer-Zweig"
	"f().feld -- Feldzugriff auf Rueckgabe"
	"f().feld, Rechenwert"
	"Rueckgabe direkt als Argument"
	"&structVar trifft das Objekt"
	"&structVar, zweites Feld"
	"&& (tcTypePop4-Pfad)"
	"Struct aus 2D-Array lesen"
	"2D-Array-Struct als Argument"
	"Zeigerarray-Feld, fester Index"
	"Zeigerarray-Feld, variabler Index"
	"Zeigerarray-Feld, Schreiben mit variablem Index"
	"Zeigerarray-Feld ueber -> (Zeiger auf Struct)"
	"Zeigerarray-Feld einer globalen Struct"
	"Groesse mit Zeigerarray-Feld (Layout)"
	"&arr[i] auf Struct-Array, global (Schrittweite)"
	"&arr[i] auf Struct-Array, lokal (Schrittweite)"
	"s.zeigerfeld[j] lesen, global"
	"s.zeigerfeld[j] schreiben, global"
	"sp->zeigerfeld[j] lesen (char*, Schritt 1)"
	"sp->zeigerfeld[j] schreiben"
	"s.zeigerfeld[j] lesen, lokal"
	"s.zeigerfeld[j] schreiben, lokal"
	"v = p[i] -- ganze Struct ueber einen Zeiger"
)

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -x "$REPO/build/qcc_p" ]       || die "build/qcc_p fehlt -- erst bauen"
[ -x "$REPO/build/qcc_backend" ] || die "build/qcc_backend fehlt"
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
"$REPO/build/qcc_p" "@$REPO/tools/test_struct_68k.c" > t.ir 2> t.err
rc=$?
last="$(tail -1 t.ir)"
if [ $rc -ne 0 ] || [ "$last" != "OK" ]; then
	echo "  qcc_p: rc=$rc Schlusswort=$last"
	head -5 t.err
	die "Testprogramm uebersetzt nicht -- das ist selbst schon ein Befund"
fi
echo "  ok ($(wc -l < t.ir | tr -d ' ') IR-Zeilen)"

echo "== 2/5 Backend + Assembler + Linker =="
"$REPO/build/qcc_backend" t.ir t.s68 -os9 >/dev/null || die "qcc_backend"
w 'Z: && cd \tmp\qcc-struct68k && set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\r68.exe q9_cstart.a -o=q9_cstart.r'
[ -f q9_cstart.r ] || die "r68 auf q9_cstart.a (liegt q9defs.d daneben?)"
w 'Z: && cd \tmp\qcc-struct68k && set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\r68.exe t.s68 -o=t.r'
[ -f t.r ] || die "r68 auf t.s68"
w 'set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\l68.exe -a Z:\tmp\qcc-struct68k\q9_cstart.r Z:\tmp\qcc-struct68k\t.r -l=M:\OS9\68020\LIB\clib.l -l=M:\OS9\68020\LIB\os_lib.l -l=M:\OS9\68000\LIB\sys.l -M=64K -o=Z:\tmp\qcc-struct68k\q9_structtest'
[ -f q9_structtest ] || die "l68"
echo "  ok ($(wc -c < q9_structtest | tr -d ' ') Byte Modul)"

echo "== 3/5 ins Image =="
"$MWOS_TOOLSHED_OS9" copy -r q9_structtest "$IMG,/CMDS/q9_structtest" >/dev/null 2>&1 \
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
send -s "/dd/CMDS/q9_structtest\r"
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
