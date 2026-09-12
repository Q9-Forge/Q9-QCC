#!/usr/bin/env bash
# fgets und ferror: qclib gegen die HOST-libc.
#
# Warum nicht gegen clib wie alles andere: fuer fgets ist clib kein gueltiges
# Orakel. Gemessen mit test/lineend68k.sh trennt Microwares fgets an $0d, weil
# ihr C '\n' auf CR abbildet -- QCC bildet es auf $0a ab, und alle Dateien
# dieser Kette entstehen damit. Die Host-libc versteht '\n' genauso als $0a,
# also ist SIE hier das Orakel.
#
# Das Orakel wird JEDES MAL neu erzeugt (clang auf dieselbe Quelle) und nicht
# als Erwartungstext hinterlegt -- ein hingeschriebener Sollwert kann denselben
# Denkfehler enthalten wie der Code.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd .. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux/Q9-Flux-68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -f "$REPO/build/qclib.l" ]     || die "build/qclib.l fehlt -- vorher make"
[ -f "$REPO/build/q9_cstart.r" ] || die "build/q9_cstart.r fehlt -- vorher make"
[ -f "$IMG_SRC" ]                || die "Abbild fehlt: $IMG_SRC"

WORK=/tmp/qclib-fgets
rm -rf "$WORK"; mkdir -p "$WORK/host"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"

echo "== 1/4 das Orakel: dieselbe Quelle am Host =="
cp test/fgtest.c "$WORK/host/"
( cd "$WORK/host" && cc -w -O1 -o fgh fgtest.c ) || die "clang"
( cd "$WORK/host" && ./fgh ) > "$WORK/host.out" 2>&1 || die "Hostlauf"
sed 's/^/    /' "$WORK/host.out"

echo "== 2/4 gegen qclib binden (eigene Kette) =="
"$QCC/q9-cpp/build/qcpp" -I"$QCC/q9-cpp/include" test/fgtest.c "$WORK/fg.i" || die "qcpp"
"$QCC/build/qcir" "@$WORK/fg.i" > "$WORK/fg.ir" 2> "$WORK/fg.err"
[ "$(tail -1 "$WORK/fg.ir")" = OK ] || { head -5 "$WORK/fg.err"; die "qcir"; }
"$QCC/build/qir_68k" "$WORK/fg.ir" "$WORK/fg.s68" -os9 -largedata >/dev/null || die "Backend"
"$FORGE/Q9-qr68/build/qr68" "$WORK/fg.s68" "-o=$WORK/fg.r" || die "qr68"
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"
"$FORGE/Q9-ql68/build/ql68" -a "$WORK/q9_cstart.r" "$WORK/fg.r" -l="$WORK/qclib.l" \
	-M=64K "-O=$WORK/q9_fgtest" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_fgtest" ] || { head -6 "$WORK/link.log"; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/q9_fgtest" | tr -d ' ') Byte)"

echo "== 3/4 im Emulator =="
"$MWOS_TOOLSHED_OS9" copy -r "$WORK/q9_fgtest" "$WORK/img.hda,/CMDS/q9_fgtest" >/dev/null 2>&1 ||
	die "copy"
"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/q9_fgtest" -e -w -r -pe -pr >/dev/null 2>&1 ||
	die "attr"
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
if {!$li} { send_log "\nFG: LOGIN FAILED\n"; exit 1 }
send -s "/dd/CMDS/q9_fgtest\r"
expect {
    -re {zeilen [0-9]+ ferror} { send_log "\nFG: DURCH\n" }
    -re {PMMU}                 { send_log "\nFG: PMMU\n" }
    -re {Stack Overflow}       { send_log "\nFG: STACK\n" }
    eof                        { send_log "\nFG: EMULATOR WEG\n"; exit 1 }
    timeout                    { send_log "\nFG: TIMEOUT\n" }
}
expect -re $prompt
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$WORK/img.hda|" \
	-e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log"
grep -q "FG: DURCH" "$WORK/run.log" || {
	echo "  Lauf nicht regulaer beendet:"; grep "FG:" "$WORK/run.log" | sed 's/^/    /'
	tail -12 "$WORK/run.log" | sed 's/^/    /'; exit 1; }
echo "  ok"

echo "== 4/4 vergleichen =="
# Aus dem Terminal-Log nur die Zeilen des Programms schneiden -- und die
# eigene Marke wieder heraus (daran ist der Gegenlauf schon einmal
# haengengeblieben).
tr -d '\r' < "$WORK/run.log" |
	grep -E '^(geschrieben|zeile|zeilen) ' > "$WORK/ziel.out" || true
sed 's/^/    /' "$WORK/ziel.out"
echo
if diff -q "$WORK/host.out" "$WORK/ziel.out" >/dev/null; then
	echo "  *** ZEICHENGLEICH zum Hostlauf -- fgets und ferror stimmen ***"
	exit 0
fi
echo "  ABWEICHUNG:"
diff "$WORK/host.out" "$WORK/ziel.out" | sed 's/^/    /'
exit 1
