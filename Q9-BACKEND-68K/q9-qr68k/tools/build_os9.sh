#!/usr/bin/env bash
# Build qr68 as an OS-9 module for the 68030 using the native toolchain:
#
#   1  qcpp        src/qr68.c            -> qr68.i      (preprocessor)
#   2  qcir        @qr68.i               -> qr68.ir     (compiler)
#   3  qir_68k     qr68.ir               -> qr68.s68    (code generation)
#   4  qr68        qr68.s68              -> qr68.r      (ITSELF)
#   5  r68         q9_cstart.a           -> q9_cstart.r (runtime entry)
#   6  l68         + clib/os_lib/sys.l  -> q9_qr68     (module)
#
# Step 4 is the key point: qr68 assembles its own module source. Only l68 and
# Microware's clib remain external.
#
# Compile with -D_Q9OS, which sets table sizes to target dimensions
# (s. Kommentar im Quelltext): QCCs Backend legt genullte Felder in den
# INITIALISIERTEN Datenbereich, und der wandert vollstaendig ins Modul.
#
# Usage: tools/build_os9.sh [output-directory]
# Exit:  0 = module built, 2 = setup failure
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${QCC:=$REPO/../Q9-QCC}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${STACK_KB:=512}"
WORK="${1:-/tmp/qr68-os9}"

die() { echo "FEHLER: $*" >&2; exit 2; }

[ -x "$REPO/build/qr68" ]        || die "build/qr68 fehlt (make)"
[ -x "$QCC/q9-cpp/build/qcpp" ]  || die "$QCC/q9-cpp/build/qcpp fehlt"
[ -x "$QCC/build/qcir" ]        || die "$QCC/build/qcir fehlt"
[ -x "$QCC/build/qir_68k" ]  || die "$QCC/build/qir_68k fehlt"

MWOS_UNIX="$MWOS"
rm -rf "$WORK"; mkdir -p "$WORK"
cp "$QCC/runtime/os9/q9_cstart.a" "$QCC/runtime/os9/q9defs.d" "$WORK/" ||
	die "q9_cstart.a/q9defs.d nicht gefunden"

# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
w() { arch -x86_64 "$WINE_BIN" cmd /c "$1" >>"$WORK/wine.log" 2>&1; }
WWORK="$(printf '%s' "$WORK" | sed 's#/#\\#g')"

echo "== 1/6 qcpp =="
"$QCC/q9-cpp/build/qcpp" -D_Q9OS -I"$QCC/q9-cpp/include" "$REPO/src/qr68.c" \
	"$WORK/qr68.i" || die "qcpp"
echo "  $(wc -c < "$WORK/qr68.i" | tr -d ' ') Byte"

echo "== 2/6 QCC =="
"$QCC/build/qcir" "@$WORK/qr68.i" > "$WORK/qr68.ir" 2> "$WORK/qr68.err"
last="$(tail -1 "$WORK/qr68.ir")"
msgs="$(wc -l < "$WORK/qr68.err" | tr -d ' ')"
echo "  $(wc -l < "$WORK/qr68.ir" | tr -d ' ') IR-Zeilen, Schlusswort $last, $msgs Meldungen"
[ "$last" = OK ] || { head -10 "$WORK/qr68.err"; die "QCC lehnt qr68 ab"; }
[ "$msgs" = 0 ]  || { head -10 "$WORK/qr68.err"; die "Semantikmeldungen"; }

echo "== 3/6 Backend (-os9 -largedata -remotedata) =="
# -largedata is required: qr68 holds far more than 32 KB of global state;
# ohne die Indirektionstabelle meldet der Assembler "value out of range".
"$QCC/build/qir_68k" "$WORK/qr68.ir" "$WORK/qr68.s68" -os9 -largedata -remotedata \
	>/dev/null || die "qir_68k"
echo "  $(wc -c < "$WORK/qr68.s68" | tr -d ' ') Byte Assembler"

echo "== 4/6 qr68 assembliert sich selbst =="
"$REPO/build/qr68" "$WORK/qr68.s68" "$WORK/qr68.r" || die "qr68 auf sich selbst"
echo "  $(wc -c < "$WORK/qr68.r" | tr -d ' ') Byte ROF"

echo "== 5/6 r68 auf q9_cstart.a =="
w "Z: && cd $WWORK && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe q9_cstart.a -o=q9_cstart.r"
[ -f "$WORK/q9_cstart.r" ] || die "r68 auf q9_cstart.a (liegt q9defs.d daneben?)"

echo "== 6/6 l68 =="
w "set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe -a Z:$WWORK\\q9_cstart.r Z:$WWORK\\qr68.r -l=M:\\OS9\\68020\\LIB\\clib.l -l=M:\\OS9\\68020\\LIB\\os_lib.l -l=M:\\OS9\\68000\\LIB\\sys.l -M=${STACK_KB}K -o=Z:$WWORK\\q9_qr68"
[ -f "$WORK/q9_qr68" ] || {
	grep -iE "error|unresolved|undefined" "$WORK/wine.log" | head -20
	die "l68"
}
size="$(wc -c < "$WORK/q9_qr68" | tr -d ' ')"

# ToolShed's "ident" crashes on modules of this size (measured with qcpp,
# Exit 138). Der Kopf wird deshalb direkt gelesen: Sync $4AFC, und M$Size
# steht bei Offset 4 (davor M$ID und M$SysRev).
hdr="$(od -A n -t x1 -N 8 "$WORK/q9_qr68" | tr -d ' \n')"
[ "${hdr:0:4}" = "4afc" ] || die "Modulkopf ohne Sync 4AFC: $hdr"
msize=$((0x${hdr:8:8}))
[ "$msize" = "$size" ] || die "M\$Size ($msize) passt nicht zur Dateigroesse ($size)"
echo "  Modul: $size Byte, Kopf ok (Sync 4AFC, M\$Size == Dateigroesse)"
echo
echo "fertig: $WORK/q9_qr68"
