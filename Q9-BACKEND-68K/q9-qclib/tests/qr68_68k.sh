#!/usr/bin/env bash
# Traegt die Bibliothek ein ECHTES Werkzeug?
#
# hello.c beweist wenig: ein paar Zeilen Ausgabe und eine kleine Datei.
# Der belastbare Test ist qr68 selbst -- 1,19 MB Modul, das eine Quelle
# assembliert und dabei BYTEWEISE dasselbe liefern muss wie am Host.
# Gebunden wird gegen qclib statt gegen Microwares clib; alles andere
# bleibt gleich, damit ein Unterschied nur an der Bibliothek liegen kann.
#
# Der Emulatorlauf selbst kommt unveraendert aus Q9-qr68
# (test/run_68k.exp) -- er ist dort schon erprobt.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd ../../.. && pwd)}"
: "${QR68:=$FORGE/Q9-QCC/Q9-BACKEND-68K/q9-qr68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
: "${Q9FLUX:=/Volumes/SSD1TB/work-stargate/Q9-Flux-68k}"
: "${BASE:=$Q9FLUX/local_images/OS9SYS.stock-stargate.hda}"
: "${ROMIMG:=$MWOS/OS9/68030/PORTS/Q9/CMDS/BOOTOBJS/ROMBUG/romimage.dev.running.BIN}"
: "${QUELLE:=$QR68/tests/insn.a}"
: "${QR68_ROF:=/tmp/clib-probe/qr68.r}"

WORK=/tmp/qclib-qr68
die() { echo "FEHLER: $*" >&2; exit 2; }

[ -f "$REPO/build/qclib.l" ] || die "build/qclib.l fehlt -- vorher 'make'"
[ -f "$QR68_ROF" ]  || die "qr68.r fehlt: $QR68_ROF (Q9-qr68/tools/build_os9.sh)"
[ -f "$BASE" ]      || die "Ausgangsabbild fehlt: $BASE"
[ -x "$Q9FLUX/build/macos/q9.exe" ] || die "q9.exe fehlt"
[ -f "$QR68/tests/run_68k.exp" ]     || die "run_68k.exp fehlt"
[ -x "$QR68/build/qr68" ]           || die "qr68 (Host) fehlt"

rm -rf "$WORK"; mkdir -p "$WORK"
cp "$QR68_ROF" "$WORK/qr68.r"
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
OS9="$MWOS_TOOLSHED_OS9"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
TW="Z:$(printf '%s' "$WORK" | sed 's#/#\\#g')"

echo "== 1/5 qr68 gegen qclib binden =="
# Mit dem EIGENEN Binder. Bis ql68 die LEA-Form von -a beherrschte, ging
# das nur mit l68: der Bezug auf QCCs Laufzeitanker (tc_extcall_tmp) ist
# in einem 1,19-MB-Modul zu weit fuer ein Wort, und es ist ein
# Datenbezug, kein Sprung.
QL68="${QL68:-$QCC/Q9-BACKEND-68K/q9-ql68k/build/ql68k}"
[ -x "$QL68" ] || die "ql68 fehlt: $QL68"
"$QL68" -a "$WORK/q9_cstart.r" "$WORK/qr68.r" -l="$WORK/qclib.l" \
	-M=512K "-O=$WORK/q9_qr68" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_qr68" ] || { sed s/^/ / "$WORK/link.log" | head -10; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/q9_qr68" | tr -d " ") Byte, mit ql68 gegen qclib)"

echo "== 2/5 Abbild bestuecken =="
IMAGE_NAME=OS9SYS.qclib-qr68.hda
IMAGE="$Q9FLUX/local_images/$IMAGE_NAME"
rm -f "$IMAGE"
cp -c "$BASE" "$IMAGE" 2>/dev/null || cp "$BASE" "$IMAGE" || die "Abbild kopieren"
"$OS9" copy -r "$WORK/q9_qr68" "$IMAGE,/CMDS/q9_qr68" >/dev/null || die "copy Modul"
# Ohne e-Attribut startet OS-9 das Modul nicht (Fehler 214).
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/q9_qr68" >/dev/null || die "attr"
"$OS9" copy -l -r "$QUELLE" "$IMAGE,/insn.a" >/dev/null || die "copy Quelle"
echo "  ok"

echo "== 3/5 Hostlauf zum Vergleich =="
"$QR68/build/qr68" "$QUELLE" "$WORK/host.r" || die "Hostlauf"
echo "  $(wc -c < "$WORK/host.r" | tr -d ' ') Byte"

echo "== 4/5 Emulator =="
Q9FLUX="$Q9FLUX" QR68_IMAGE="local_images/$IMAGE_NAME" MWOS="$MWOS_UNIX" \
	ROMIMG="$ROMIMG" expect -f "$QR68/tests/run_68k.exp" > "$WORK/run.log" 2>&1 || {
		echo "  FEHLGESCHLAGEN -- letzte Zeilen:"
		tail -20 "$WORK/run.log" | sed 's/^/    /'
		exit 1
	}
grep -E "<<<" "$WORK/run.log" | sed 's/^/  /' || true

echo "== 5/5 Ergebnis vergleichen =="
rm -f "$WORK/target.r"
"$OS9" copy "$IMAGE,/insn.r" "$WORK/target.r" >/dev/null || die "copy zurueck"
if cmp -s "$WORK/host.r" "$WORK/target.r"; then
	echo "  BYTEIDENTISCH ($(wc -c < "$WORK/target.r" | tr -d ' ') Byte)"
	echo
	echo "qr68 laeuft gegen qclib auf echtem 68030 und liefert dasselbe"
	echo "wie am Host -- die Bibliothek traegt ein echtes Werkzeug."
	exit 0
fi
echo "  ABWEICHUNG (Host $(wc -c < "$WORK/host.r" | tr -d ' '), Ziel $(wc -c < "$WORK/target.r" | tr -d ' ') Byte)"
cmp -l "$WORK/host.r" "$WORK/target.r" | head -10 | sed 's/^/    /'
exit 1
