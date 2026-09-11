#!/usr/bin/env bash
# SELBSTHOST von qcpp: der Praeprozessor verarbeitet seine EIGENE Quelle vor,
# QCC uebersetzt das Ergebnis, und das daraus gebaute 68k-Modul laeuft auf
# echtem 68030 -- mit byteweisem Vergleich gegen den Hostlauf.
#
#   Stufe 0  qcpp (Host)          src/qcpp.c            -> qcpp.self.c
#   Stufe 1  QCC (Host)           @qcpp.self.c          -> self.ir
#   Stufe 2  qir_68k -> r68 -> l68                  -> 68k-Modul
#   Stufe 3  Modul im Emulator    /dd/qcpptest.c        -> /dd/self.i
#   Pruefung self.i == Hostlauf derselben Quelle (byteweise)
#
# In der Kette kommt KEINE fremde Toolchain fuer die SPRACHE vor: kein xcc,
# kein Host-cc, kein Python. r68/l68 sind Assembler und Binder, nicht
# Compiler -- die stehen als naechstes auf der Liste (s. README).
#
# Aufruf:  tools/selfhost_68k.sh
# Exit:    0 = Fixpunkt, 1 = abweichend/Fehler, 2 = Aufbauproblem
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"     # .../Q9-FRONTEND-C/q9-qcpp
QCC="$(cd "$REPO/../.." && pwd)"                             # .../Q9-QCC

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${Q9FLUX:=$QCC/../Q9-Flux-68k}"
: "${BASE:=$Q9FLUX/local_images/OS9SYS.hda}"
: "${STACK_KB:=512}"
WORK="${WORK:-/tmp/qcpp-selfhost}"

die() { echo "FEHLER: $*" >&2; exit 2; }

[ -x "$REPO/build/qcpp" ]      || die "q9-cpp/build/qcpp fehlt (make)"
[ -x "$QCC/build/qcir" ]      || die "build/qcir fehlt"
[ -x "$QCC/build/qir68k" ] || die "build/qir68k fehlt"
[ -f "$BASE" ]                 || die "Ausgangsimage fehlt: $BASE"
[ -x "$Q9FLUX/build/macos/q9.exe" ] || die "q9.exe fehlt (in Q9-Flux: make host)"

MWOS_UNIX="$MWOS"
IMAGE_NAME=OS9SYS.qcpp-self.hda
IMAGE="$Q9FLUX/local_images/$IMAGE_NAME"

rm -rf "$WORK"; mkdir -p "$WORK"
cp "$QCC/runtime/os9/q9_cstart.a" "$QCC/runtime/os9/q9defs.d" "$WORK/"

# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
OS9="$MWOS_TOOLSHED_OS9"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
w() { arch -x86_64 "$WINE_BIN" cmd /c "$1" >>"$WORK/wine.log" 2>&1; }

cd "$REPO"

echo "== 1/6 qcpp verarbeitet seine eigene Quelle vor =="
./build/qcpp -Iinclude src/qcpp.c "$WORK/qcpp.self.c" || die "qcpp auf sich selbst"
echo "  $(wc -c < "$WORK/qcpp.self.c" | tr -d ' ') Byte (Quelle: $(wc -c < src/qcpp.c | tr -d ' '))"

echo "== 2/6 QCC uebersetzt das Ergebnis =="
"$QCC/build/qcir" "@$WORK/qcpp.self.c" > "$WORK/self.ir" 2> "$WORK/self.err"
last="$(tail -1 "$WORK/self.ir")"
msgs="$(wc -l < "$WORK/self.err" | tr -d ' ')"
echo "  $(wc -l < "$WORK/self.ir" | tr -d ' ') IR-Zeilen, Schlusswort $last, $msgs Meldungen"
[ "$last" = OK ] || { head -10 "$WORK/self.err"; die "QCC lehnt die eigene Vorverarbeitung ab"; }
[ "$msgs" = 0 ] || { head -10 "$WORK/self.err"; die "Semantikmeldungen"; }

echo "== 3/6 Backend (-os9 -largedata) =="
# -largedata ist Pflicht: qcpp haelt weit mehr als 32 KB globalen Zustand,
# ohne die Indirektionstabelle meldet r68 "value out of range".
"$QCC/build/qir68k" "$WORK/self.ir" "$WORK/qcpp.s68k" -os9 -largedata >/dev/null ||
	die "qir68k"
echo "  $(wc -c < "$WORK/qcpp.s68k" | tr -d ' ') Byte Assembler"

echo "== 4/6 r68 + l68 =="
w "Z: && cd \\tmp\\qcpp-selfhost && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe q9_cstart.a -o=q9_cstart.r"
[ -f "$WORK/q9_cstart.r" ] || die "r68 auf q9_cstart.a (liegt q9defs.d daneben?)"
w "Z: && cd \\tmp\\qcpp-selfhost && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe qcpp.s68k -o=qcpp.r"
[ -f "$WORK/qcpp.r" ] || {
	grep -iE "error|out of range" "$WORK/wine.log" | head -10
	die "r68 auf qcpp.s68k"
}
w "set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe -a Z:\\tmp\\qcpp-selfhost\\q9_cstart.r Z:\\tmp\\qcpp-selfhost\\qcpp.r -l=M:\\OS9\\68020\\LIB\\clib.l -l=M:\\OS9\\68020\\LIB\\os_lib.l -l=M:\\OS9\\68000\\LIB\\sys.l -M=${STACK_KB}K -o=Z:\\tmp\\qcpp-selfhost\\q9_qcpp"
[ -f "$WORK/q9_qcpp" ] || {
	grep -iE "error|unresolved|undefined" "$WORK/wine.log" | head -20
	die "l68"
}
size="$(wc -c < "$WORK/q9_qcpp" | tr -d ' ')"
echo "  Modul: $size Byte"
# "ident" wird hier NICHT aufgerufen: ToolSheds ident stuerzt an einem Modul
# dieser Groesse ab (Exit 138, SIGBUS -- am 2026-09-02 gemessen). Der Kopf
# wird deshalb direkt gelesen: Sync $4AFC und M$Size == Dateigroesse.
# Kopflayout OS-9/68K: M$ID (2) | M$SysRev (2) | M$Size (4) -- M$Size steht
# also bei Offset 4, nicht bei 2. Acht Byte lesen, die letzten vier davon.
hdr="$(od -A n -t x1 -N 8 "$WORK/q9_qcpp" | tr -d ' \n')"
[ "${hdr:0:4}" = "4afc" ] || die "Modulkopf ohne Sync 4AFC: $hdr"
msize=$((0x${hdr:8:8}))
[ "$msize" = "$size" ] || die "M\$Size ($msize) passt nicht zur Dateigroesse ($size)"
echo "  Kopf ok (Sync 4AFC, M\$Size == Dateigroesse)"

echo "== 5/6 Image bestuecken und im Emulator fahren =="
cp -c "$BASE" "$IMAGE"
"$OS9" copy -r "$WORK/q9_qcpp" "$IMAGE,/CMDS/q9_qcpp" || die "ToolShed copy"
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/q9_qcpp" >/dev/null || die "ToolShed attr"
"$OS9" copy -l -r test/qcpptest.c "$IMAGE,/qcpptest.c" || die "ToolShed copy (Quelle)"

./build/qcpp test/qcpptest.c "$WORK/host.i" || die "Hostlauf"

Q9FLUX="$Q9FLUX" QCPP_IMAGE="local_images/$IMAGE_NAME" MWOS="$MWOS_UNIX" \
	expect -f "$REPO/test/run_selfhost_68k.exp" > "$WORK/run.log" 2>&1 || {
		echo "FEHLGESCHLAGEN -- letzte Zeilen:"
		tail -25 "$WORK/run.log"
		exit 1
	}
grep -E "<<<" "$WORK/run.log" | sed 's/^/  /' || true

echo "== 6/6 Ergebnis herausholen und vergleichen =="
rm -f "$WORK/target.i"
"$OS9" copy -l "$IMAGE,/self.i" "$WORK/target.i" || die "ToolShed copy zurueck"
if cmp -s "$WORK/host.i" "$WORK/target.i"; then
	echo "  BYTEIDENTISCH ($(wc -c < "$WORK/target.i" | tr -d ' ') Byte)"
	echo
	echo "qcpp uebersetzt sich selbst: von QCC gebaut, auf echtem 68030"
	echo "gelaufen, gleiches Ergebnis wie am Host."
	exit 0
fi
echo "  ABWEICHUNG:"
diff -u "$WORK/host.i" "$WORK/target.i" | head -30 | sed 's/^/  /'
exit 1
