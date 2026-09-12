#!/usr/bin/env bash
# Das vierte und letzte Werkzeug: QCC selbst gegen qclib.
#
# qr68, qcpp und ql68 sind schon nachgewiesen. QCC ist der Brocken -- und
# der eigentliche Zweck: sein Selbsthost-Test (Q9-QCC/tools/
# test_selfhost_68k.sh) band bis hierher mit r68 und l68 UEBER WINE gegen
# Microwares clib, os_lib und sys.l. Laeuft dieser Test durch, ist in der
# ganzen Kette kein fremdes Teil mehr.
#
# GEPRUEFT WIRD EIN FIXPUNKT, nicht "laeuft durch": das auf dem 68030
# erzeugte IR muss BYTEIDENTISCH zu dem sein, das der Host-Compiler aus
# derselben Quelle erzeugt. Ein Bibliotheksfehler in fread, in der
# Zahlenformatierung oder in realloc faellt damit auf -- 89.769 Zeilen IR
# geben keinem falschen Byte eine Deckung.
#
# Der Emulatorlauf kommt UNVERAENDERT aus Q9-QCC
# (test/expect/selfhost_68k.exp) und ist derselbe, den der clib-Weg
# faehrt. Nur so kann ein Unterschied an der Bibliothek liegen.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd .. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux/Q9-Flux-68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
# Dieses Abbild traegt /dd/bootstrap_probe.c fuer die Rauchprobe.
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"
: "${STACK_KB:=1024}"

WORK=/tmp/qclib-qcc
die() { echo "FEHLER: $*" >&2; exit 2; }

[ -f "$REPO/build/qclib.l" ]      || die "build/qclib.l fehlt -- vorher make"
[ -f "$REPO/build/q9_cstart.r" ]  || die "build/q9_cstart.r fehlt -- vorher make"
[ -x "$QCC/build/qcir" ]         || die "qcir fehlt"
[ -x "$QCC/build/qir_68k" ]   || die "qir_68k fehlt"
[ -f "$QCC/build/qcc_p.bootstrap.c" ] ||
	die "qcc_p.bootstrap.c fehlt (Q9-QCC/tools/build_xcc_bootstrap.sh)"
[ -f "$QCC/test/expect/selfhost_68k.exp" ] || die "der gemeinsame Emulatorlauf fehlt"
[ -f "$IMG_SRC" ]                 || die "Abbild fehlt: $IMG_SRC"
QR68="${QR68:-$FORGE/Q9-qr68/build/qr68}"
QL68="${QL68:-$FORGE/Q9-ql68/build/ql68}"
[ -x "$QR68" ] || die "qr68 fehlt: $QR68"
[ -x "$QL68" ] || die "ql68 fehlt: $QL68"

rm -rf "$WORK"; mkdir -p "$WORK"
cp "$REPO/build/qclib.l" "$REPO/build/q9_cstart.r" "$WORK/"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
OS9="$MWOS_TOOLSHED_OS9"

echo "== 1/6 Host: QCC uebersetzt seinen eigenen Parser =="
"$QCC/build/qcir" "@$QCC/build/qcc_p.bootstrap.c" \
	> "$WORK/stage1.ir" 2> "$WORK/stage1.err"
[ "$(tail -1 "$WORK/stage1.ir")" = OK ] || {
	head -5 "$WORK/stage1.err"; die "schon am Host nicht uebersetzbar"; }
echo "  ok ($(wc -l < "$WORK/stage1.ir" | tr -d " ") IR-Zeilen, $(wc -c < "$WORK/stage1.ir" | tr -d " ") Byte)"

# -largedata ist Pflicht: der Parser hat weit mehr als 32 KB globalen
# Zustand, ohne die Indirektionstabelle reicht die PC-relative
# Adressierung des 68000 nicht.
echo "== 2/6 die eigene Kette: Backend, qr68, ql68 =="
"$QCC/build/qir_68k" "$WORK/stage1.ir" "$WORK/stage2.s68" -os9 -largedata -remotedata \
	>/dev/null || die "qir_68k"
"$QR68" "$WORK/stage2.s68" "-o=$WORK/stage2.r" >"$WORK/asm.log" 2>&1 || {
	head -10 "$WORK/asm.log"; die "qr68"; }
# -M=1024K: der Parser steigt rekursiv ab; mit dem Standardstack
# (3072 Byte) bricht schon die Rauchprobe mit Stack Overflow ab.
"$QL68" -a "$WORK/q9_cstart.r" "$WORK/stage2.r" -l="$WORK/qclib.l" \
	-M="${STACK_KB}K" "-O=$WORK/q9_qcc_qclib" >"$WORK/link.log" 2>&1 || {
	head -10 "$WORK/link.log"; die "ql68"; }
[ -f "$WORK/q9_qcc_qclib" ] || die "ql68 hat kein Modul geschrieben"
echo "  ok ($(wc -c < "$WORK/stage2.r" | tr -d " ") Byte ROF -> $(wc -c < "$WORK/q9_qcc_qclib" | tr -d " ") Byte Modul)"

# Lieber hier abbrechen als nach dem Emulatorlauf ratlos sein.
"$OS9" ident "$WORK/q9_qcc_qclib" | grep -q "Stack size: *\\\$C00\\b" &&
	die "Stack size ist der Standardwert -- -M= hat nicht gegriffen"

echo "== 3/6 Abbild bestuecken =="
IMAGE="$FLUX/local_images/OS9SYS.qclib-qcc.hda"
rm -f "$IMAGE"
cp -c "$IMG_SRC" "$IMAGE" 2>/dev/null || cp "$IMG_SRC" "$IMAGE" ||
	die "Abbild kopieren"
"$OS9" copy -r "$WORK/q9_qcc_qclib" "$IMAGE,/CMDS/q9_qcc_qclib" >/dev/null 2>&1 ||
	die "copy Modul"
# ToolShed copy -r setzt das e-Attribut NICHT -- ohne das startet OS-9 das
# Modul nicht (Fehler 214).
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/q9_qcc_qclib" >/dev/null 2>&1 ||
	die "attr -e"
"$OS9" copy -l -r "$QCC/build/qcc_p.bootstrap.c" "$IMAGE,/qclib_src.c" >/dev/null 2>&1 ||
	die "copy Quelle"
echo "  ok"

echo "== 4/6 im Emulator: Rauchprobe, dann der eigene Parser =="
rm -f "$WORK/run.log"
Q9FLUX="$FLUX" MWOS="$MWOS_UNIX" QCC_IMAGE="$IMAGE" \
	QCC_MODULE=q9_qcc_qclib QCC_PROBE=/dd/bootstrap_probe.c \
	QCC_SRC=/dd/qclib_src.c QCC_OUT=/dd/qclib_stage2.ir \
	QCC_LOG="$WORK/run.log" \
	expect -f "$QCC/test/expect/selfhost_68k.exp" >/dev/null 2>&1
[ -f "$WORK/run.log" ] || die "kein Emulator-Log -- lief der Emulator?"
grep -q "SELFHOST: SMOKE OK" "$WORK/run.log" || {
	echo "  Rauchprobe rot:"; tail -20 "$WORK/run.log" | sed "s/^/    /"; exit 1; }
grep -q "SELFHOST: LAUF ZU ENDE" "$WORK/run.log" || {
	echo "  Lauf nicht regulaer beendet:"
	grep "SELFHOST:" "$WORK/run.log" | sed "s/^/    /"; exit 1; }
echo "  ok"

echo "== 5/6 Ergebnis zurueckholen =="
# Der Emulator muss weg, bevor ToolShed dasselbe Abbild liest.
pkill -f "q9.exe.*OS9SYS.qclib-qcc.hda" 2>/dev/null
sleep 2
rm -f "$WORK/stage2.ir"
"$OS9" copy -l -r "$IMAGE,/qclib_stage2.ir" "$WORK/stage2.ir" >/dev/null 2>&1 ||
	die "IR nicht aus dem Abbild lesbar"
echo "  $(wc -c < "$WORK/stage2.ir" | tr -d " ") Byte"

echo "== 6/6 Fixpunkt pruefen =="
errs=$(tr -d "\r" < "$WORK/run.log" | grep -cE "qcc: |SEMERR" || true)
last=$(tail -1 "$WORK/stage2.ir")
printf "  Fehlermeldungen im Lauf: %s\n" "$errs"
printf "  Schlusswort:             %s\n" "$last"
printf "  IR-Zeilen 68k / Host:    %s / %s\n" \
	"$(wc -l < "$WORK/stage2.ir" | tr -d " ")" \
	"$(wc -l < "$WORK/stage1.ir" | tr -d " ")"

fail=0
[ "$last" = OK ]  || { echo "  -> Schlusswort ist nicht OK"; fail=1; }
[ "$errs" -eq 0 ] || { echo "  -> es gab Semantikfehler"; fail=1; }
if cmp -s "$WORK/stage2.ir" "$WORK/stage1.ir"; then
	echo "  -> IR byteidentisch zum Host-Lauf"
else
	echo "  -> IR WEICHT AB:"
	cmp "$WORK/stage2.ir" "$WORK/stage1.ir" | head -3 | sed "s/^/    /"
	diff "$WORK/stage2.ir" "$WORK/stage1.ir" | head -10 | sed "s/^/    /"
	fail=1
fi

echo
if [ $fail -eq 0 ]; then
	echo "QCC LAEUFT GEGEN qclib AUF ECHTEM 68030 -- die Kette ist frei"
	echo "von fremden Teilen: qcpp, qcc, qr68, ql68, qclib."
else
	echo "ROT (Log: $WORK/run.log)"
fi
exit $fail
