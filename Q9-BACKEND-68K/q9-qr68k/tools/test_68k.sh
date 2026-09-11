#!/usr/bin/env bash
# Target run: qr68 assembles on a real 68030 and must produce the same
# bytes as the host build.
#
#   1  tools/build_os9.sh  baut das Modul (qr68 assembliert sich selbst)
#   2  Abbild bestuecken   Modul nach /CMDS, test/insn.a nach /dd
#   3  Emulator            q9_qr68 -v /dd/insn.a /dd/insn.r
#   4  Comparison          /dd/insn.r == host run of the same source
#
# Work on a COPY of the image ("cp -c", a CoW clone costs
# nothing): ToolShed writes directly to the file while an emulator is running,
# and multiple emulators often run on the Mac.
#
# Usage:  tools/test_68k.sh
# Exit:   0 = byte-identical, 1 = difference/error, 2 = setup failure
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${QCC:=$REPO/../Q9-QCC}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${Q9FLUX:=/Volumes/SSD1TB/work-stargate/Q9-Flux-68k}"
: "${BASE:=$Q9FLUX/local_images/OS9SYS.stock-stargate.hda}"
: "${ROMIMG:=$MWOS/OS9/68030/PORTS/Q9/CMDS/BOOTOBJS/ROMBUG/romimage.dev.running.BIN}"
: "${QUELLE:=$REPO/test/insn.a}"
WORK=/tmp/qr68-os9

die() { echo "FEHLER: $*" >&2; exit 2; }

[ -f "$BASE" ] || die "Ausgangsabbild fehlt: $BASE"
[ -x "$Q9FLUX/build/macos/q9.exe" ] || die "q9.exe fehlt (in Q9-Flux: make host)"
[ -f "$ROMIMG" ] || die "ROM-Abbild fehlt: $ROMIMG"

MWOS_UNIX="$MWOS"
"$REPO/tools/build_os9.sh" "$WORK" || exit 2

# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
OS9="$MWOS_TOOLSHED_OS9"

IMAGE_NAME=OS9SYS.qr68-test.hda
IMAGE="$Q9FLUX/local_images/$IMAGE_NAME"

echo
echo "== Abbild bestuecken =="
rm -f "$IMAGE"
cp -c "$BASE" "$IMAGE" 2>/dev/null || cp "$BASE" "$IMAGE" || die "Abbild kopieren"
"$OS9" copy -r "$WORK/q9_qr68" "$IMAGE,/CMDS/q9_qr68" || die "ToolShed copy (Modul)"
# ToolShed does NOT set the e attribute; without it OS-9 will not start the module
# (error 214).
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/q9_qr68" >/dev/null || die "ToolShed attr"
"$OS9" copy -l -r "$QUELLE" "$IMAGE,/insn.a" || die "ToolShed copy (Quelle)"

echo "== Hostlauf zum Vergleich =="
"$REPO/build/qr68" "$QUELLE" "$WORK/host.r" || die "Hostlauf"
echo "  $(wc -c < "$WORK/host.r" | tr -d ' ') Byte"

echo "== Emulator =="
Q9FLUX="$Q9FLUX" QR68_IMAGE="local_images/$IMAGE_NAME" MWOS="$MWOS_UNIX" \
	ROMIMG="$ROMIMG" expect -f "$REPO/test/run_68k.exp" > "$WORK/run.log" 2>&1 || {
		echo "FEHLGESCHLAGEN -- letzte Zeilen:"
		tail -25 "$WORK/run.log"
		exit 1
	}
grep -E "<<<" "$WORK/run.log" | sed 's/^/  /' || true

echo "== Ergebnis herausholen und vergleichen =="
rm -f "$WORK/target.r"
"$OS9" copy "$IMAGE,/insn.r" "$WORK/target.r" || die "ToolShed copy zurueck"
if cmp -s "$WORK/host.r" "$WORK/target.r"; then
	echo "  BYTEIDENTISCH ($(wc -c < "$WORK/target.r" | tr -d ' ') Byte)"
	echo
	echo "qr68 laeuft auf echtem 68030 und liefert dasselbe wie am Host."
	exit 0
fi
echo "  ABWEICHUNG (Host $(wc -c < "$WORK/host.r" | tr -d ' '), Ziel $(wc -c < "$WORK/target.r" | tr -d ' ') Byte)"
cmp -l "$WORK/host.r" "$WORK/target.r" | head -10 | sed 's/^/  /'
exit 1
