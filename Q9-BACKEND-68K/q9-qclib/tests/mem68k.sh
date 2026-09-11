#!/usr/bin/env bash
# Faehrt test/memprobe.c auf echtem 68030 und druckt, was die Maschine an
# Speicher hergibt. Kein Soll-Ist-Vergleich -- eine MESSUNG.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd .. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux-68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -f "$REPO/build/qclib.l" ] || die "build/qclib.l fehlt -- vorher make"
[ -f "$IMG_SRC" ]            || die "Abbild fehlt: $IMG_SRC"

WORK=/tmp/qclib-mem
rm -rf "$WORK"; mkdir -p "$WORK"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"

echo "== 1/3 Sonde bauen und binden =="
"$QCC/q9-cpp/build/qcpp" -I"$QCC/q9-cpp/include" test/memprobe.c "$WORK/mp.i" ||
	die "qcpp"
"$QCC/build/qcir" "@$WORK/mp.i" > "$WORK/mp.ir" 2> "$WORK/mp.err"
[ "$(tail -1 "$WORK/mp.ir")" = OK ] || { head -5 "$WORK/mp.err"; die "qcir"; }
"$QCC/build/qir_68k" "$WORK/mp.ir" "$WORK/mp.s68" -os9 -largedata >/dev/null ||
	die "Backend"
"$FORGE/Q9-qr68/build/qr68" "$WORK/mp.s68" "-o=$WORK/mp.r" || die "qr68"
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"
"$FORGE/Q9-ql68/build/ql68" -a "$WORK/q9_cstart.r" "$WORK/mp.r" \
	-l="$WORK/qclib.l" -M=64K "-O=$WORK/q9_memprobe" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_memprobe" ] || { head -6 "$WORK/link.log"; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/q9_memprobe" | tr -d " ") Byte)"

echo "== 2/3 ins Abbild und laufen lassen =="
"$MWOS_TOOLSHED_OS9" copy -r "$WORK/q9_memprobe" "$WORK/img.hda,/CMDS/q9_memprobe" \
	>/dev/null 2>&1 || die "copy"
"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/q9_memprobe" -e -w -r -pe -pr \
	>/dev/null 2>&1 || die "attr"
cat > "$WORK/run.exp" <<"EOF"
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
if {!$li} { send_log "\nMEM: LOGIN FAILED\n"; exit 1 }
# mfree zeigt den freien Systemspeicher aus Sicht von OS-9 selbst --
# ein zweites, unabhaengiges Mass neben dem der Sonde.
send -s "mfree\r"
expect -re $prompt
send -s "/dd/CMDS/q9_memprobe\r"
expect {
    -re {sonde zu ende}   { send_log "\nMEM: SONDE DURCH\n" }
    -re {log [0-9]+: FEHL} { send_log "\nMEM: LOG-LEITER GERISSEN\n" }
    -re {Stack Overflow}  { send_log "\nMEM: STACK OVERFLOW\n" }
    -re {PMMU}            { send_log "\nMEM: PMMU\n" }
    timeout               { send_log "\nMEM: TIMEOUT\n" }
}
expect -re $prompt
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$WORK/img.hda|" \
	-e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log"

echo "== 3/3 Messwerte =="
tr -d "\r" < "$WORK/run.log" | grep -E "groesster|eingabepuffer|log |darueber|verbraucht|Abgeben|sonde|MEM:|free RAM|MByte RAM" |
	sed "s/^/  /"
echo
echo "(Log: $WORK/run.log)"
