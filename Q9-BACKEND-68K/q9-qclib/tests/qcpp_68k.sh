#!/usr/bin/env bash
# Das zweite echte Werkzeug: qcpp gegen qclib.
#
# qr68 ist schon nachgewiesen; qcpp ist der andere Fall -- 4,35 MB Modul,
# und es reicht seine Ausgabe durch fwrite statt sie nur zu drucken.
# Gebunden wird mit der eigenen Kette, verglichen wird die praeprozessierte
# Datei byteweise mit dem Hostlauf.
#
# Der Emulatorlauf kommt unveraendert aus Q9-QCC/q9-cpp (test/run_68k.exp),
# damit ein Unterschied nur an der Bibliothek liegen kann.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd ../../.. && pwd)}"
: "${QCPP:=$FORGE/Q9-QCC/Q9-FRONTEND-C/q9-qcpp}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
: "${Q9FLUX:=/Volumes/SSD1TB/work-stargate/Q9-Flux-68k}"
: "${BASE:=$Q9FLUX/local_images/OS9SYS.stock-stargate.hda}"
: "${QCPP_ROF:=/tmp/qcpp-own/qcpp.r}"

WORK=/tmp/qclib-qcpp
die() { echo "FEHLER: $*" >&2; exit 2; }

[ -f "$REPO/build/qclib.l" ] || die "build/qclib.l fehlt -- vorher 'make'"
[ -f "$QCPP_ROF" ] || die "qcpp.r fehlt: $QCPP_ROF (mit der eigenen Kette bauen)"
[ -f "$BASE" ]     || die "Ausgangsabbild fehlt: $BASE"
[ -f "$QCPP/tests/run_68k.exp" ] || die "run_68k.exp fehlt"
[ -x "$QCPP/build/qcpp" ]       || die "qcpp (Host) fehlt"
[ -f "$QCPP/tests/qcpptest.c" ]  || die "Pruefquelle fehlt"

rm -rf "$WORK"; mkdir -p "$WORK"
cp "$QCPP_ROF" "$WORK/qcpp.r"
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
OS9="$MWOS_TOOLSHED_OS9"

echo "== 1/5 qcpp gegen qclib binden =="
# Der MODULNAME kommt aus -O= und muss "qcpp" lauten -- das Testskript
# ruft /dd/CMDS/qcpp.
QL68="${QL68:-$QCC/Q9-BACKEND-68K/q9-ql68k/build/ql68k}"
[ -x "$QL68" ] || die "ql68 fehlt: $QL68"
"$QL68" -a "$WORK/q9_cstart.r" "$WORK/qcpp.r" -l="$WORK/qclib.l" \
	-M=512K "-O=$WORK/qcpp" >"$WORK/link.log" 2>&1
[ -f "$WORK/qcpp" ] || { sed 's/^/    /' "$WORK/link.log" | head -10; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/qcpp" | tr -d ' ') Byte, mit ql68 gegen qclib)"

echo "== 2/5 Abbild bestuecken =="
IMAGE_NAME=OS9SYS.qclib-qcpp.hda
IMAGE="$Q9FLUX/local_images/$IMAGE_NAME"
rm -f "$IMAGE"
cp -c "$BASE" "$IMAGE" 2>/dev/null || cp "$BASE" "$IMAGE" || die "Abbild kopieren"
"$OS9" copy -r "$WORK/qcpp" "$IMAGE,/CMDS/qcpp" >/dev/null || die "copy Modul"
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/qcpp" >/dev/null || die "attr"
# Die Quelle mit "copy -l" (Zeilenenden), das Modul mit "copy -r" (roh).
"$OS9" copy -l -r "$QCPP/tests/qcpptest.c" "$IMAGE,/qcpptest.c" >/dev/null ||
	die "copy Quelle"
echo "  ok"

echo "== 3/5 Hostlauf zum Vergleich =="
"$QCPP/build/qcpp" "$QCPP/tests/qcpptest.c" "$WORK/host.i" || die "Hostlauf"
echo "  $(wc -c < "$WORK/host.i" | tr -d ' ') Byte"

echo "== 4/5 Emulator =="
Q9FLUX="$Q9FLUX" QCPP_IMAGE="local_images/$IMAGE_NAME" MWOS="$MWOS_UNIX" \
	expect -f "$QCPP/tests/run_68k.exp" > "$WORK/run.log" 2>&1 || {
		echo "  FEHLGESCHLAGEN -- letzte Zeilen:"
		tail -20 "$WORK/run.log" | sed 's/^/    /'
		exit 1
	}
grep -E "<<<" "$WORK/run.log" | sed 's/^/  /' || true

echo "== 5/5 Ergebnis vergleichen =="
rm -f "$WORK/target.i"
"$OS9" copy -l "$IMAGE,/qcpptest.i" "$WORK/target.i" >/dev/null || die "copy zurueck"
if cmp -s "$WORK/host.i" "$WORK/target.i"; then
	echo "  BYTEIDENTISCH ($(wc -c < "$WORK/target.i" | tr -d ' ') Byte)"
	echo
	echo "qcpp laeuft gegen qclib auf echtem 68030 -- zwei von drei"
	echo "Werkzeugen der Kette sind damit nachgewiesen."
	exit 0
fi
echo "  ABWEICHUNG (Host $(wc -c < "$WORK/host.i" | tr -d ' '), Ziel $(wc -c < "$WORK/target.i" | tr -d ' ') Byte)"
diff "$WORK/host.i" "$WORK/target.i" | head -10 | sed 's/^/    /'
exit 1
