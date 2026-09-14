#!/usr/bin/env bash
# Der eigentliche Pruefstein: qclib gegen Microwares clib.
#
# Byteidentitaet taugt hier nicht -- fuer clib gibt es keine Quellen, also
# auch keine gemeinsame Eingabe. Was zaehlt, ist das VERHALTEN: DASSELBE
# Programm, einmal gegen qclib und einmal gegen clib gebunden, muss auf
# echtem 68030 Zeichen fuer Zeichen dasselbe ausgeben.
#
# Beide Module laufen im SELBEN Emulatorlauf nacheinander -- das spart
# nicht nur Zeit, es schliesst auch aus, dass ein Unterschied blosser
# Umgebung geschuldet ist.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd ../../.. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux/Q9-Flux-68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -f "$REPO/build/qclib.l" ] || die "build/qclib.l fehlt -- vorher 'make'"
[ -f "$REPO/build/hello.r" ] || die "build/hello.r fehlt -- vorher 'make build/hello.r'"
[ -f "$IMG_SRC" ]            || die "Image fehlt: $IMG_SRC"

WORK=/tmp/qclib-vs
rm -rf "$WORK"; mkdir -p "$WORK"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"
cp "$REPO/build/hello.r" "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
TW="Z:$(printf '%s' "$WORK" | sed 's#/#\\#g')"

echo "== 1/4 zwei Module binden =="
# a) gegen qclib -- mit dem eigenen Binder.
QL68="${QL68:-$QCC/Q9-BACKEND-68K/q9-ql68k/build/ql68k}"
[ -x "$QL68" ] || die "ql68 fehlt: $QL68"
"$QL68" -a "$WORK/q9_cstart.r" "$WORK/hello.r" -l="$WORK/qclib.l" \
	-M=8K "-O=$WORK/q9_hq" >"$WORK/link_q.log" 2>&1
[ -f "$WORK/q9_hq" ] || { sed 's/^/    /' "$WORK/link_q.log" | head -6; die "ql68"; }

# b) gegen Microwares clib -- die ist ein libgen-Archiv, das nur l68 liest.
arch -x86_64 "$WINE_BIN" cmd /c \
	"set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe -a $TW\\q9_cstart.r $TW\\hello.r -l=M:\\OS9\\68020\\LIB\\clib.l -l=M:\\OS9\\68020\\LIB\\os_lib.l -l=M:\\OS9\\68000\\LIB\\sys.l -M=8K -o=$TW\\q9_hc" \
	>"$WORK/link_c.log" 2>&1
[ -f "$WORK/q9_hc" ] || { sed 's/^/    /' "$WORK/link_c.log" | head -6; die "l68 gegen clib"; }
echo "  qclib: $(wc -c < "$WORK/q9_hq" | tr -d ' ') Byte, clib: $(wc -c < "$WORK/q9_hc" | tr -d ' ') Byte"

echo "== 2/4 beide ins Image =="
for m in q9_hq q9_hc; do
	"$MWOS_TOOLSHED_OS9" copy -r "$WORK/$m" "$WORK/img.hda,/CMDS/$m" >/dev/null 2>&1 ||
		die "ToolShed-copy $m"
	"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/$m" -e -w -r -pe -pr >/dev/null 2>&1
done
echo "  ok"

echo "== 3/4 beide im selben Lauf ausfuehren =="
cat > "$WORK/run.exp" <<'EOF'
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
foreach {marke modul} {QMARK q9_hq CMARK q9_hc EMARK -} {
    if {$modul eq "-"} { send -s "echo $marke\r" } else {
        send -s "echo $marke\r"
        expect -re $prompt
        send -s "/dd/CMDS/$modul\r"
        # Auf die SCHLUSSMARKE des Programms warten, nicht auf den Prompt
        # (siehe hello68k.sh): ein Prompt aus dem Puffer trifft sofort.
        expect {
            -re {hello fertig}   { }
            -re {Stack Overflow} { send_log "\nTEST: STACK OVERFLOW\n" }
            -re {PMMU}           { send_log "\nTEST: PMMU\n" }
            eof                  { send_log "\nTEST: EMULATOR WEG\n"; exit 1 }
            timeout              { send_log "\nTEST: TIMEOUT\n"; exit 1 }
        }
    }
    expect {
        -re $prompt          { }
        -re {Stack Overflow} { send_log "\nTEST: STACK OVERFLOW\n" }
        -re {PMMU}           { send_log "\nTEST: PMMU\n" }
        timeout              { send_log "\nTEST: TIMEOUT\n"; exit 1 }
    }
}
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$WORK/img.hda|" \
	-e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log"
echo "  ok"

echo "== 4/4 Ausgaben vergleichen =="
# Zwischen den Marken herausschneiden; Echo-Zeilen und Prompts fallen weg.
saeubere() {
	tr -d '\r' < "$WORK/run.log" |
		sed -n "/^$1\$/,/^$2\$/p" |
		grep -vE "^($1|$2)\$" |
		grep -vE 'MARK|^\$ ?$|q9_h[qc]' |
		sed 's/^[$#] *//'
}
saeubere QMARK CMARK > "$WORK/aus_q.txt"
saeubere CMARK EMARK > "$WORK/aus_c.txt"

if grep -q "TEST: " "$WORK/run.log"; then
	echo "  Abbruch im Emulator:"
	grep "TEST: " "$WORK/run.log" | sed 's/^/    /'
	exit 1
fi
if [ ! -s "$WORK/aus_q.txt" ] || [ ! -s "$WORK/aus_c.txt" ]; then
	echo "  FEHLER: eine der beiden Ausgaben ist leer -- lief das Modul?"
	echo "  (Log: $WORK/run.log)"
	exit 1
fi
echo "  qclib:"; sed 's/^/      /' "$WORK/aus_q.txt"
echo "  clib:";  sed 's/^/      /' "$WORK/aus_c.txt"
echo
if diff -u "$WORK/aus_c.txt" "$WORK/aus_q.txt" > "$WORK/diff.txt"; then
	echo "  *** ZEICHENGLEICH -- qclib verhaelt sich wie Microwares clib ***"
	exit 0
fi
echo "  UNTERSCHIED (- clib, + qclib):"
sed 's/^/      /' "$WORK/diff.txt" | head -20
exit 1
