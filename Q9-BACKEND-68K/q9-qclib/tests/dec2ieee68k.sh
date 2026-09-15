#!/usr/bin/env bash
# Dezimal -> IEEE-754 auf echtem 68030.
#
# Warum das auf dem Zielrechner laufen MUSS: der Konverter rechnet die
# Bitmuster mit Ganzzahlen aus, und zwar in Gliedern zu 16 Bit, deren
# Produkte gerade eben in 32 Bit passen. Auf dem Mac ist "long" 64 Bit --
# dort faellt ein Ueberlauf also gar nicht auf. Erst der 68030 mit seinen
# 32 Bit zeigt, ob die Rechnung traegt. Uebersetzt wird mit QCC selbst,
# denn genau dort soll der Konverter spaeter sitzen.
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
[ -f "$IMG_SRC" ] || die "Image fehlt: $IMG_SRC"
[ -f "$QCC/tools/dec2ieee.c" ] || die "tools/dec2ieee.c fehlt"
WORK=/tmp/dec2ieee-68k
rm -rf "$WORK"; mkdir -p "$WORK"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"
MWOS_UNIX="$MWOS"
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 || die "Toolchain"
MWOS="$MWOS_UNIX"

cat "$QCC/tools/dec2ieee.c" "$QCC/tools/dec2ieee_test.c" > "$WORK/d2i.c"

echo "== 1/3 uebersetzen und binden (eigene Kette) =="
"$QCC/Q9-FRONTEND-C/q9-qcpp/build/qcpp" "$WORK/d2i.c" "$WORK/d2i.i" || die "qcpp"
"$QCC/Q9-FRONTEND-C/q9-qcir/build/qcir" "@$WORK/d2i.i" > "$WORK/d2i.ir" 2>"$WORK/qcir.err"
tail -1 "$WORK/d2i.ir" | grep -q '^OK$' || { sed 's/^/    /' "$WORK/qcir.err" | head -5; die "qcir"; }
"$QCC/Q9-BACKEND-68K/q9-qir68k/build/qir68k" "$WORK/d2i.ir" "$WORK/d2i.s68" -os9 -largedata -remotedata >/dev/null || die "qir68k"
"$QCC/Q9-BACKEND-68K/q9-qr68k/build/qr68k" "$WORK/d2i.s68" -o="$WORK/d2i.r" >"$WORK/qr68.log" 2>&1 \
	|| { sed 's/^/    /' "$WORK/qr68.log" | head -5; die "qr68"; }
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"
"$QCC/Q9-BACKEND-68K/q9-ql68k/build/ql68k" -a "$WORK/q9_cstart.r" "$WORK/d2i.r" \
	-l="$WORK/qclib.l" -M=32K "-O=$WORK/q9_d2i" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_d2i" ] || { sed 's/^/    /' "$WORK/link.log" | head -10; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/q9_d2i" | tr -d ' ') Byte)"

echo "== 2/3 ins Image =="
"$MWOS_TOOLSHED_OS9" copy -r "$WORK/q9_d2i" "$WORK/img.hda,/CMDS/q9_d2i" >/dev/null 2>&1 || die "copy"
"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/q9_d2i" -e -w -r -pe -pr >/dev/null 2>&1
echo "  ok"

echo "== 3/3 im Emulator ausfuehren =="
cat > "$WORK/run.exp" <<'EOF'
log_file -a LOGFILE
set timeout 600
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
send -s "/dd/CMDS/q9_d2i\r"
expect {
    -re {falsch[\r\n]} { }
    -re {Stack Overflow} { send_log "\nTEST: STACK OVERFLOW\n" }
    -re {PMMU}           { send_log "\nTEST: PMMU\n" }
    eof                  { send_log "\nTEST: EMULATOR WEG\n"; exit 1 }
    timeout              { send_log "\nTEST: TIMEOUT\n"; exit 1 }
}
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$WORK/img.hda|" -e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log"

grep -c '^ok ' "$WORK/run.log" >/dev/null 2>&1
okn=$(grep -c 'ok ' "$WORK/run.log" || true)
echo "  gemeldet richtig: $okn"
grep 'FALSCH' "$WORK/run.log" | sed 's/^/    /' | head -10
if grep -q 'dec2ieee fertig: 0 von 25 falsch' "$WORK/run.log"; then
	echo
	echo "DEZIMAL -> IEEE-754 LAEUFT AUF ECHTEM 68030 -- alle 25 Bitmuster stimmen"
	exit 0
fi
echo; echo "TEST ROT -- Auszug:"; tail -25 "$WORK/run.log" | sed 's/^/    /'
exit 1
