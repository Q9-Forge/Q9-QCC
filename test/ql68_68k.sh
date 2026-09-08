#!/usr/bin/env bash
# Das dritte und letzte Werkzeug: ql68 auf echtem 68030.
#
# Der Binder BINDET auf dem Ziel -- er bekommt dieselben Eingaben wie am
# Host (Startcode, ein Programm, qclib.l) und muss byteweise dasselbe
# Modul liefern. Damit ist die ganze Kette auch auf dem Ziel nachgewiesen.
#
# Die ZIELfassung hat kleine Puffer (QL_IN/QL_OUT je 512 KB, s.
# _Q9OS-Zweig in ql68.c) -- die Eingaben sind zusammen keine 20 KB.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd .. && pwd)}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${Q9FLUX:=/Volumes/SSD1TB/work-stargate/Q9-Flux-68k}"
: "${BASE:=$Q9FLUX/local_images/OS9SYS.stock-stargate.hda}"
: "${ROMIMG:=$MWOS/OS9/68030/PORTS/Q9/CMDS/BOOTOBJS/ROMBUG/romimage.dev.running.BIN}"
: "${QL68_ROF:=/tmp/ql68-own/ql68.r}"

WORK=/tmp/qclib-ql68
die() { echo "FEHLER: $*" >&2; exit 2; }

[ -f "$REPO/build/qclib.l" ] || die "build/qclib.l fehlt -- vorher 'make'"
[ -f "$REPO/build/hello.r" ] || die "build/hello.r fehlt"
[ -f "$QL68_ROF" ] || die "ql68.r fehlt: $QL68_ROF (mit der eigenen Kette bauen)"
[ -f "$BASE" ]     || die "Ausgangsabbild fehlt: $BASE"
[ -x "$Q9FLUX/build/macos/q9.exe" ] || die "q9.exe fehlt"

rm -rf "$WORK"; mkdir -p "$WORK"
cp "$QL68_ROF" "$WORK/ql68.r"
cp "$REPO/build/q9_cstart.r" "$REPO/build/hello.r" "$REPO/build/qclib.l" "$WORK/"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
OS9="$MWOS_TOOLSHED_OS9"
QL68="${QL68:-$FORGE/Q9-ql68/build/ql68}"
[ -x "$QL68" ] || die "ql68 (Host) fehlt"

echo "== 1/5 ql68 selbst binden (mit sich selbst, gegen qclib) =="
"$QL68" -a "$WORK/q9_cstart.r" "$WORK/ql68.r" -l="$WORK/qclib.l" \
	-M=512K "-O=$WORK/q9_ql68" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_ql68" ] || { sed 's/^/    /' "$WORK/link.log" | head -10; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/q9_ql68" | tr -d ' ') Byte)"

echo "== 2/5 Hostlauf: dieselbe Bindeaufgabe am Host =="
# Derselbe Ausgabename wie auf dem Ziel -- der MODULNAME kommt aus -O=.
"$QL68" -a "$WORK/q9_cstart.r" "$WORK/hello.r" -l="$WORK/qclib.l" \
	-M=8K "-O=$WORK/hmod" >/dev/null 2>&1 || die "Hostlauf"
echo "  $(wc -c < "$WORK/hmod" | tr -d ' ') Byte"

echo "== 3/5 Abbild bestuecken =="
IMAGE_NAME=OS9SYS.qclib-ql68.hda
IMAGE="$Q9FLUX/local_images/$IMAGE_NAME"
rm -f "$IMAGE"
cp -c "$BASE" "$IMAGE" 2>/dev/null || cp "$BASE" "$IMAGE" || die "Abbild kopieren"
"$OS9" copy -r "$WORK/q9_ql68" "$IMAGE,/CMDS/q9_ql68" >/dev/null || die "copy Modul"
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/q9_ql68" >/dev/null || die "attr"
for f in q9_cstart.r hello.r qclib.l; do
	"$OS9" copy -r "$WORK/$f" "$IMAGE,/$f" >/dev/null || die "copy $f"
done
echo "  ok"

echo "== 4/5 Emulator: ql68 bindet auf dem 68030 =="
cat > "$WORK/run.exp" <<'EOF'
log_file -a LOGFILE
set timeout 900
set send_slow {1 .003}
set prompt {ROOT# *$}
spawn ./build/macos/q9.exe --rom MWOSDIR --cf IMAGE
expect {
    "devices online" {}
    timeout { send_user "\n<<< BOOT TIMEOUT >>>\n"; exit 1 }
}
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
if {!$li} { send_user "\n<<< LOGIN FEHLGESCHLAGEN >>>\n"; exit 1 }
# Nicht auf den Prompt warten -- der steht noch im Puffer und traefe
# sofort, bevor das 1,7-MB-Modul ueberhaupt geladen ist.
send -s "/dd/CMDS/q9_ql68 -a /dd/q9_cstart.r /dd/hello.r -l=/dd/qclib.l -M=8K -O=/dd/hmod\r"
expect {
    -re $prompt          { send_user "\n<<< QL68 LIEF AUF 68K DURCH >>>\n" }
    -re {Stack Overflow} { send_user "\n<<< STACK OVERFLOW >>>\n"; exit 1 }
    -re {PMMU}           { send_user "\n<<< PMMU >>>\n"; exit 1 }
    timeout              { send_user "\n<<< TIMEOUT >>>\n"; exit 1 }
}
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$IMAGE|" \
	-e "s|MWOSDIR|$ROMIMG|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$Q9FLUX" && expect -f "$WORK/run.exp" > "$WORK/exp.log" 2>&1 ) || {
	echo "  FEHLGESCHLAGEN -- letzte Zeilen:"
	tail -20 "$WORK/run.log" 2>/dev/null | sed 's/^/    /'
	exit 1
}
grep -E "<<<" "$WORK/exp.log" "$WORK/run.log" 2>/dev/null | sed 's/^/  /' | head -3

echo "== 5/5 Ergebnis vergleichen =="
rm -f "$WORK/target.mod"
"$OS9" copy "$IMAGE,/hmod" "$WORK/target.mod" >/dev/null || die "copy zurueck"
if cmp -s "$WORK/hmod" "$WORK/target.mod"; then
	echo "  BYTEIDENTISCH ($(wc -c < "$WORK/target.mod" | tr -d ' ') Byte)"
	echo
	echo "ql68 bindet auf echtem 68030 und liefert dasselbe wie am Host."
	echo "Damit sind alle drei Werkzeuge der Kette auf dem Ziel nachgewiesen."
	exit 0
fi
echo "  ABWEICHUNG (Host $(wc -c < "$WORK/hmod" | tr -d ' '), Ziel $(wc -c < "$WORK/target.mod" | tr -d ' ') Byte)"
cmp -l "$WORK/hmod" "$WORK/target.mod" 2>/dev/null | head -10 | sed 's/^/    /'
exit 1
