#!/usr/bin/env bash
# DIE KETTE AUF DEM ZIEL: qcpp verarbeitet seine eigene Quelle auf echtem
# 68030 vor, und QCC uebersetzt das Ergebnis dort. Beide Werkzeuge sind
# selbstgebaut -- qcpp von QCC, QCC aus QCCs eigenem IR.
#
#   Vorbereitung (Host):  qcpp -> QCC -> Backend -> r68 -> l68  =>  zwei Module
#   Stufe 1  (68030):     qcpp  /dd/qcpp.c       -> /dd/qcpp.self.c
#   Stufe 2  (68030):     qcc  @/dd/qcpp.self.c  -> /dd/qcpp.ir
#   Pruefung (Host):      beide Ergebnisse byteweise gegen den Hostlauf
#
# Der Unterschied zu tools/selfhost_68k.sh: dort laufen Vorverarbeitung und
# Uebersetzung am HOST und nur das fertige Modul auf dem Ziel. Hier laeuft
# die Sprachverarbeitung selbst auf dem Ziel.
#
# Was in dieser Kette noch NICHT auf dem Ziel laeuft: qir_68k, r68 und
# l68 -- also der Weg vom IR zum Modul.
#
# Aufruf:  tools/target_chain_68k.sh
# Laufzeit rund 20 Minuten (zwei Module bauen, 4,3 MB davon, plus zwei Laeufe
# im Emulator).
# Exit:    0 = beide Stufen byteidentisch, 1 = abweichend/Fehler, 2 = Aufbau
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"     # .../Q9-FRONTEND-C/q9-qcpp
QCC="$(cd "$REPO/../.." && pwd)"                             # .../Q9-QCC

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${Q9FLUX:=$QCC/../Q9-Flux/Q9-Flux-68k}"
: "${BASE:=$Q9FLUX/local_images/OS9SYS.hda}"
WORK="${WORK:-/tmp/qcpp-target-chain}"

die() { echo "FEHLER: $*" >&2; exit 2; }

[ -x "$REPO/build/qcpp" ]       || die "q9-cpp/build/qcpp fehlt (make)"
[ -x "$QCC/build/qcir" ]       || die "build/qcir fehlt"
[ -x "$QCC/build/qir68k" ] || die "build/qir68k fehlt"
[ -f "$QCC/build/qcc_p.bootstrap.c" ] ||
	die "build/qcc_p.bootstrap.c fehlt (tools/build_xcc_bootstrap.sh oder q9-cpp/tools/bootstrap.sh)"
[ -f "$BASE" ]                  || die "Ausgangsimage fehlt: $BASE"
[ -x "$Q9FLUX/build/macos/q9.exe" ] || die "q9.exe fehlt (in Q9-Flux: make host)"

MWOS_UNIX="$MWOS"
IMAGE_NAME=OS9SYS.qcpp-chain.hda
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
WORKWIN="$(printf '%s' "$WORK" | sed 's#/#\\#g')"

# Modulkopf selbst pruefen: ToolSheds "ident" stuerzt an mehrere MB grossen
# Modulen ab (Exit 138). M$Size steht bei Offset 4, davor liegt M$SysRev.
check_module() {
	local f="$1" size hdr msize

	size="$(wc -c < "$f" | tr -d ' ')"
	hdr="$(od -A n -t x1 -N 8 "$f" | tr -d ' \n')"
	[ "${hdr:0:4}" = "4afc" ] || die "$f: Modulkopf ohne Sync 4AFC"
	msize=$((0x${hdr:8:8}))
	[ "$msize" = "$size" ] || die "$f: M\$Size ($msize) != Dateigroesse ($size)"
	echo "  $f: $size Byte, Kopf ok"
}

# build_module <ir> <ausgabename> <stack-kb>
build_module() {
	local ir="$1" name="$2" stack="$3"

	"$QCC/build/qir68k" "$ir" "$WORK/$name.s68k" -os9 -largedata >/dev/null ||
		die "qir68k fuer $name"
	w "Z: && cd $WORKWIN && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe $name.s68k -o=$name.r"
	[ -f "$WORK/$name.r" ] || {
		grep -iE "error|out of range" "$WORK/wine.log" | head -5
		die "r68 fuer $name"
	}
	w "set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe -a Z:$WORKWIN\\q9_cstart.r Z:$WORKWIN\\$name.r -l=M:\\OS9\\68020\\LIB\\clib.l -l=M:\\OS9\\68020\\LIB\\os_lib.l -l=M:\\OS9\\68000\\LIB\\sys.l -M=${stack}K -o=Z:$WORKWIN\\$name"
	[ -f "$WORK/$name" ] || {
		grep -iE "error|unresolved|undefined" "$WORK/wine.log" | head -10
		die "l68 fuer $name"
	}
	check_module "$WORK/$name"
}

cd "$REPO"

echo "== 1/7 Hostlauf als Referenz =="
./build/qcpp src/qcpp.c "$WORK/host.self.c" || die "qcpp am Host"
"$QCC/build/qcir" "@$WORK/host.self.c" > "$WORK/host.self.ir" 2> "$WORK/host.self.err"
[ "$(tail -1 "$WORK/host.self.ir")" = OK ] || die "QCC am Host lehnt es ab"
[ ! -s "$WORK/host.self.err" ] || { head -5 "$WORK/host.self.err"; die "Meldungen am Host"; }
echo "  vorverarbeitet $(wc -c < "$WORK/host.self.c" | tr -d ' ') Byte, IR $(wc -l < "$WORK/host.self.ir" | tr -d ' ') Zeilen"

echo "== 2/7 Startcode assemblieren =="
w "Z: && cd $WORKWIN && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe q9_cstart.a -o=q9_cstart.r"
[ -f "$WORK/q9_cstart.r" ] || die "r68 auf q9_cstart.a (liegt q9defs.d daneben?)"

echo "== 3/7 qcpp-Modul aus qcpps eigenem IR =="
build_module "$WORK/host.self.ir" qcpp 512

echo "== 4/7 QCC-Modul aus QCCs eigenem IR =="
"$QCC/build/qcir" "@$QCC/build/qcc_p.bootstrap.c" > "$WORK/qcc.ir" 2>/dev/null
[ "$(tail -1 "$WORK/qcc.ir")" = OK ] || die "QCC uebersetzt seinen eigenen Parser nicht"
# 1 MB Stack: der Parser steigt rekursiv ab.
build_module "$WORK/qcc.ir" qcc 1024

echo "== 5/7 Image bestuecken =="
cp -c "$BASE" "$IMAGE"
"$OS9" copy -r "$WORK/qcpp" "$IMAGE,/CMDS/qcpp" || die "ToolShed copy qcpp"
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/qcpp" >/dev/null || die "attr qcpp"
"$OS9" copy -r "$WORK/qcc" "$IMAGE,/CMDS/qcc" || die "ToolShed copy qcc"
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/qcc" >/dev/null || die "attr qcc"
"$OS9" copy -l -r src/qcpp.c "$IMAGE,/qcpp.c" || die "ToolShed copy Quelle"
echo "  $IMAGE_NAME fertig"

echo "== 6/7 Beide Stufen im Emulator =="
Q9FLUX="$Q9FLUX" QCPP_IMAGE="local_images/$IMAGE_NAME" MWOS="$MWOS_UNIX" \
	expect -f "$REPO/test/run_target_chain.exp" > "$WORK/run.log" 2>&1 || {
		echo "FEHLGESCHLAGEN -- letzte Zeilen:"
		tail -25 "$WORK/run.log"
		exit 1
	}
grep -E "<<<" "$WORK/run.log" | sed 's/^/  /' || true

echo "== 7/7 Beides herausholen und byteweise vergleichen =="
rc=0
"$OS9" copy -l "$IMAGE,/qcpp.self.c" "$WORK/target.self.c" || die "copy zurueck (Stufe 1)"
if cmp -s "$WORK/host.self.c" "$WORK/target.self.c"; then
	echo "  Stufe 1 (qcpp auf dem Ziel):  BYTEIDENTISCH ($(wc -c < "$WORK/target.self.c" | tr -d ' ') Byte)"
else
	echo "  Stufe 1: ABWEICHUNG"
	cmp "$WORK/host.self.c" "$WORK/target.self.c" | head -3 | sed 's/^/    /'
	rc=1
fi

"$OS9" copy -l "$IMAGE,/qcpp.ir" "$WORK/target.self.ir" || die "copy zurueck (Stufe 2)"
if cmp -s "$WORK/host.self.ir" "$WORK/target.self.ir"; then
	echo "  Stufe 2 (QCC auf dem Ziel):   BYTEIDENTISCH ($(wc -l < "$WORK/target.self.ir" | tr -d ' ') IR-Zeilen)"
else
	echo "  Stufe 2: ABWEICHUNG"
	cmp "$WORK/host.self.ir" "$WORK/target.self.ir" | head -3 | sed 's/^/    /'
	rc=1
fi

if [ $rc -eq 0 ]; then
	echo
	echo "qcpp verarbeitet seine eigene Quelle AUF DEM ZIEL vor, und QCC"
	echo "uebersetzt das dort -- beides mit demselben Ergebnis wie am Host."
fi
exit $rc
