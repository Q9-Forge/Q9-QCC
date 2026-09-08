#!/usr/bin/env bash
# Das VIERTE Werkzeug auf dem Ziel: QCCs Backend selbst.
#
# qcpp, qcc (der Parser), qr68 und ql68 sind auf dem 68030 nachgewiesen.
# qcc_backend war das einzige Glied, das dort NIE gelaufen ist -- der Weg vom
# IR zum Assembler. Damit fehlte der Kette genau ein Schritt, um sich auf dem
# Ziel selbst zu bauen.
#
# Dass es lange nicht ging, hatte einen Grund: qcc_backend_c.cpp war nicht in
# QCCs Teilmenge uebersetzbar. Gebraucht wurden dafuer (alles 2026-09-07):
#   - Zeigerarrays als Strukturfeld in QCC (fuer "char* args[6]", 106 Zugriffe)
#   - TC_MAX_CTRL von 64 auf 128 (gemessen: 68 gebraucht, else-if-Kette)
#   - NULL in q9-cpp/include/stddef.h
#   - vier Zeilen im Backend selbst (verhaltensneutral, byteweise geprueft)
#   - zehn Funktionen in qclib
#
# GEPRUEFT WIRD BYTEIDENTITAET: derselbe IR-Eingang muss auf dem 68030
# denselben Assembler ergeben wie am Host. Der Ausgabename ist dabei WICHTIG
# -- bei -os9 leitet das Backend den psect-Namen daraus ab, verschiedene Namen
# geben also immer einen Unterschied. Beide Laeufe schreiben "out.s68".
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd .. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"
: "${IR:=$REPO/build/hello.ir}"

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -f "$REPO/build/qclib.l" ]     || die "build/qclib.l fehlt -- vorher make"
[ -f "$REPO/build/q9_cstart.r" ] || die "build/q9_cstart.r fehlt -- vorher make"
[ -f "$IR" ]                     || die "IR-Eingang fehlt: $IR"
[ -f "$IMG_SRC" ]                || die "Abbild fehlt: $IMG_SRC"

WORK=/tmp/qclib-qccb
rm -rf "$WORK"; mkdir -p "$WORK/host" "$WORK/ziel"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
OS9="$MWOS_TOOLSHED_OS9"

echo "== 1/5 das Backend mit der eigenen Kette zum 68k-Modul =="
"$QCC/q9-cpp/build/qcpp" -I"$QCC/q9-cpp/include" "$QCC/Source/qcc_backend_c.cpp" \
	"$WORK/b.i" || die "qcpp"
"$QCC/build/qcc_p" "@$WORK/b.i" > "$WORK/b.ir" 2> "$WORK/b.err"
[ "$(tail -1 "$WORK/b.ir")" = OK ] || { head -5 "$WORK/b.err"; die "qcc_p auf das Backend"; }
"$QCC/build/qcc_backend" "$WORK/b.ir" "$WORK/qccb.s68" -os9 -largedata -remotedata >/dev/null ||
	die "Backend uebersetzt sich selbst nicht"
"$FORGE/Q9-qr68/build/qr68" "$WORK/qccb.s68" "-o=$WORK/qccb.r" || die "qr68"
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"
# Der MODULNAME kommt aus -O= und muss q9_qccb lauten -- der Emulatorlauf
# ruft /dd/CMDS/q9_qccb.
"$FORGE/Q9-ql68/build/ql68" -a "$WORK/q9_cstart.r" "$WORK/qccb.r" -l="$WORK/qclib.l" \
	-M=64K "-O=$WORK/q9_qccb" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_qccb" ] || { head -8 "$WORK/link.log"; die "ql68"; }
echo "  ok ($(wc -l < "$WORK/qccb.s68" | tr -d ' ') Assemblerzeilen -> $(wc -c < "$WORK/qccb.r" | tr -d ' ') Byte ROF -> $(wc -c < "$WORK/q9_qccb" | tr -d ' ') Byte Modul)"

echo "== 2/5 Hostlauf zum Vergleich =="
cp "$IR" "$WORK/host/in.ir"
( cd "$WORK/host" && "$QCC/build/qcc_backend" in.ir out.s68 -os9 >/dev/null ) ||
	die "Hostlauf"
echo "  $(wc -c < "$WORK/host/out.s68" | tr -d ' ') Byte"

echo "== 3/5 Abbild bestuecken =="
"$OS9" copy -r "$WORK/q9_qccb" "$WORK/img.hda,/CMDS/q9_qccb" >/dev/null 2>&1 || die "copy Modul"
"$OS9" attr "$WORK/img.hda,/CMDS/q9_qccb" -e -w -r -pe -pr >/dev/null 2>&1 || die "attr"
# ROH kopieren, NICHT mit "copy -l": das setzt OS-9-Zeilenenden ($0d), und
# qclibs fgets trennt an $0a (siehe test/lineend68k.sh und src/file.c).
"$OS9" copy -r "$IR" "$WORK/img.hda,/in.ir" >/dev/null 2>&1 || die "copy IR"
echo "  ok"

echo "== 4/5 im Emulator: das Backend uebersetzt auf dem 68030 =="
cat > "$WORK/run.exp" <<'EOF'
log_file -a LOGFILE
# Weit gefasst: das Modul ist 9,6 MB gross und will erst vom CF-Abbild
# geladen werden.
set timeout 1200
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
if {!$li} { send_log "\nQCCB: LOGIN FAILED\n"; exit 1 }
send_log "\nQCCB: LAUF BEGINNT\n"
send -s "/dd/CMDS/q9_qccb /dd/in.ir /dd/out.s68 -os9\r"
expect {
    -re $prompt          { send_log "\nQCCB: LAUF ZU ENDE\n" }
    -re {PMMU}           { send_log "\nQCCB: PMMU\n" }
    -re {Stack Overflow} { send_log "\nQCCB: STACK OVERFLOW\n" }
    eof                  { send_log "\nQCCB: EMULATOR WEG\n"; exit 1 }
    timeout              { send_log "\nQCCB: TIMEOUT\n" }
}
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$WORK/img.hda|" \
	-e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log"
grep -q "QCCB: LAUF ZU ENDE" "$WORK/run.log" || {
	echo "  Lauf nicht regulaer beendet:"; grep "QCCB:" "$WORK/run.log" | sed 's/^/    /'
	tr -d '\r' < "$WORK/run.log" | tail -12 | sed 's/^/    /'; exit 1; }
echo "  ok"

echo "== 5/5 Ergebnis vergleichen =="
pkill -f "q9.exe.*$WORK/img.hda" 2>/dev/null || true
sleep 2
"$OS9" copy -r "$WORK/img.hda,/out.s68" "$WORK/ziel/out.s68" >/dev/null 2>&1 ||
	die "Assembler nicht aus dem Abbild lesbar"
h=$(wc -c < "$WORK/host/out.s68" | tr -d ' ')
z=$(wc -c < "$WORK/ziel/out.s68" | tr -d ' ')
if cmp -s "$WORK/host/out.s68" "$WORK/ziel/out.s68"; then
	echo "  BYTEIDENTISCH ($z Byte)"
	echo
	echo "qcc_backend laeuft auf echtem 68030 -- damit ist JEDES Glied der"
	echo "Kette auf dem Ziel nachgewiesen: qcpp, qcc, qcc_backend, qr68, ql68."
	exit 0
fi
echo "  ABWEICHUNG (Host $h, Ziel $z Byte)"
cmp "$WORK/host/out.s68" "$WORK/ziel/out.s68" | head -2 | sed 's/^/    /'
diff "$WORK/host/out.s68" "$WORK/ziel/out.s68" | head -10 | sed 's/^/    /'
exit 1
