#!/usr/bin/env bash
# SELBSTHOST-TEST: QCC baut sich selbst und uebersetzt sich auf echtem 68k.
#
# Anders als tools/build_xcc_bootstrap.sh kommt hier KEINE fremde Toolchain
# vor: das 68k-Modul entsteht aus QCCs eigenem IR.  Der Test gilt als
# bestanden, wenn das auf dem 68030 erzeugte IR BYTEIDENTISCH zu dem ist, das
# der Host-Compiler aus derselben Quelle erzeugt -- ein Fixpunkt, nicht nur
# "laeuft durch".
#
#   Stufe 0  build/qcir (Host)       @build/qcc_p.bootstrap.c  -> stage1.ir
#   Stufe 2  stage1.ir -> Backend -> r68 -> l68                -> 68k-Modul
#   Stufe 2' das Modul im Emulator   @qcc_p.bootstrap.c        -> stage2.ir
#   Pruefung stage2.ir == stage1.ir  (byteweise)
#
# Erstmals gruen am 2026-09-01, nachdem die acht Struct-Emissionsstellen
# repariert waren (s. Commit "Struct-Kopie auf echtem 68k repariert").  Vorher
# meldete dieser Lauf ~9700 Semantikfehler.
#
# Aufruf:  tools/test_selfhost_68k.sh [-k]
#   -k   Zwischendateien in $WORK behalten
# Exit:    0 = Fixpunkt erreicht, 1 = abweichend/Fehler, 2 = Aufbauproblem
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FLUX="${FLUX:-$REPO/../Q9-Flux/Q9-Flux-68k}"
SRCIMG="${SRCIMG:-$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"
WORK="${WORK:-/tmp/qcc-selfhost}"
STACK_KB="${STACK_KB:-1024}"
KEEP=0
[ "${1:-}" = "-k" ] && KEEP=1

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -x "$REPO/build/qcir" ]     || die "build/qcir fehlt"
[ -x "$REPO/build/qir68k" ]  || die "build/qir68k fehlt"
[ -f "$REPO/build/qcc_p.bootstrap.c" ] || die "build/qcc_p.bootstrap.c fehlt (tools/build_xcc_bootstrap.sh)"
[ -f "$REPO/test/bootstrap_probe.c" ] || die "test/bootstrap_probe.c fehlt"
[ -f "$SRCIMG" ]                 || die "Image nicht gefunden: $SRCIMG"
[ -d "$FLUX" ]                   || die "Q9-Flux nicht gefunden: $FLUX"

# Auf einer KOPIE des Images arbeiten.  Zwei Gruende: ToolShed schreibt am
# Emulator vorbei direkt in die Imagedatei -- laeuft parallel ein Emulator auf
# demselben Image, kann das Dateisystem beschaedigt werden; und auf diesem Mac
# laufen oefter mehrere Emulatoren gleichzeitig.  Auf APFS ist cp -c ein
# Copy-on-Write-Klon, also praktisch kostenlos.
IMG="$FLUX/local_images/OS9SYS.qcc-selfhost.hda"

rm -rf "$WORK"; mkdir -p "$WORK"
cd "$WORK" || die "cd $WORK"
cp "$REPO/runtime/os9/q9_cstart.a" "$REPO/runtime/os9/q9defs.d" .

# shellcheck disable=SC1091
source /Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh >/dev/null 2>&1 \
	|| die "OS-9-Toolchain nicht ladbar"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
# r68/l68 erwarten das Prefix mit M: = MWOS (nicht das os9-Prefix, wo M: auf
# projects/ zeigt) -- siehe tools/build_xcc_bootstrap.sh.
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
w() { arch -x86_64 "$WINE_BIN" cmd /c "$1" >>wine.log 2>&1; }
TS="$MWOS_TOOLSHED_OS9"

echo "== 1/6 Host: QCC uebersetzt seinen eigenen Parser =="
"$REPO/build/qcir" "@$REPO/build/qcc_p.bootstrap.c" > stage1.ir 2> stage1.err
rc=$?
if [ $rc -ne 0 ] || [ "$(tail -1 stage1.ir)" != "OK" ]; then
	echo "  rc=$rc Schlusswort=$(tail -1 stage1.ir)"; head -5 stage1.err
	die "schon am Host nicht uebersetzbar -- das ist selbst der Befund"
fi
echo "  ok ($(wc -l < stage1.ir | tr -d ' ') IR-Zeilen, $(wc -c < stage1.ir | tr -d ' ') Byte)"

# -largedata ist Pflicht: der Parser hat weit mehr als 32 KB globalen Zustand,
# ohne die Indirektionstabelle meldet r68 "value out of range".
echo "== 2/6 Backend (-os9 -largedata -remotedata) =="
"$REPO/build/qir68k" stage1.ir stage2.s68k -os9 -largedata -remotedata >/dev/null \
	|| die "qir68k"
echo "  ok ($(wc -l < stage2.s68k | tr -d ' ') Assemblerzeilen)"

echo "== 3/6 r68 + l68 =="
w 'Z: && cd \tmp\qcc-selfhost && set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\r68.exe q9_cstart.a -o=q9_cstart.r'
[ -f q9_cstart.r ] || die "r68 auf q9_cstart.a (liegt q9defs.d daneben?)"
w 'Z: && cd \tmp\qcc-selfhost && set PATH=M:\DOS\BIN;%PATH% && M:\DOS\BIN\r68.exe stage2.s68k -o=stage2.r'
[ -f stage2.r ] && [ -s stage2.r ] || { grep -iE "error|out of range" wine.log | head -10; die "r68 auf stage2.s68k"; }
# -M=1024K: der Parser steigt rekursiv ab.  Mit dem Standard-Stack (3072 Byte)
# bricht schon die Rauchprobe mit "**** Stack Overflow ****" ab.
w "set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe -a Z:\\tmp\\qcc-selfhost\\q9_cstart.r Z:\\tmp\\qcc-selfhost\\stage2.r -l=M:\\OS9\\68020\\LIB\\clib.l -l=M:\\OS9\\68020\\LIB\\os_lib.l -l=M:\\OS9\\68000\\LIB\\sys.l -M=${STACK_KB}K -o=Z:\\tmp\\qcc-selfhost\\q9_qcc_stage2"
[ -f q9_qcc_stage2 ] || { grep -iE "error|unresolved" wine.log | head -20; die "l68"; }
echo "  ok ($(wc -c < q9_qcc_stage2 | tr -d ' ') Byte Modul)"

# Lieber hier abbrechen als nach dem Emulatorlauf ratlos sein.
if "$TS" ident q9_qcc_stage2 | grep -q 'Stack size: *\$C00\b'; then
	die "Stack size ist der Standardwert -- -M= hat nicht gegriffen"
fi

echo "== 4/6 Image-Klon vorbereiten =="
rm -f "$IMG"
cp -c "$SRCIMG" "$IMG" 2>/dev/null || cp "$SRCIMG" "$IMG" || die "Image-Kopie"
"$TS" copy -r q9_qcc_stage2 "$IMG,/CMDS/q9_qcc_stage2" >/dev/null 2>&1 \
	|| die "ToolShed-copy des Moduls"
"$TS" copy -l -r "$REPO/build/qcc_p.bootstrap.c" "$IMG,/selfhost_src.c" >/dev/null 2>&1 \
	|| die "ToolShed-copy der Quelle"
# ToolShed 'copy -r' setzt das e-Attribut NICHT -- ohne das laesst OS-9 das
# Modul nicht starten (Fehler 214).  Kostet sonst eine Runde Ratlosigkeit.
"$TS" attr -e -w -r -pe -pr "$IMG,/CMDS/q9_qcc_stage2" >/dev/null 2>&1 \
	|| die "attr -e"
"$TS" copy -l -r "$REPO/test/bootstrap_probe.c" "$IMG,/bootstrap_probe.c" >/dev/null 2>&1 \
	|| die "ToolShed-copy der Rauchprobe"
echo "  ok"

echo "== 5/6 im Emulator: Rauchprobe, dann der eigene Parser =="
# DER EMULATORLAUF STEHT IN EINER EIGENEN DATEI (test/expect/selfhost_68k.exp).
# Ausgelagert wurde er, damit der qclib-Pruefstand (Q9-qclib/test/qcc_68k.sh)
# GENAU DENSELBEN Lauf fahren kann -- gebunden mit qr68/ql68 gegen qclib
# statt mit r68/l68 gegen clib. Nur dann kann ein Unterschied zwischen den
# beiden Ergebnissen an der Bibliothek liegen und nicht daran, dass zwei
# Kopien des Emulatorlaufs auseinandergelaufen sind.
rm -f run.log
Q9FLUX="$FLUX" MWOS=/Volumes/SSD1TB/projects/MWOS QCC_IMAGE="$IMG" \
	QCC_MODULE=q9_qcc_stage2 QCC_PROBE=/dd/bootstrap_probe.c \
	QCC_SRC=/dd/selfhost_src.c QCC_OUT=/dd/stage2.ir \
	QCC_LOG="$WORK/run.log" \
	expect -f "$REPO/test/expect/selfhost_68k.exp" >/dev/null 2>&1

[ -f run.log ] || die "kein Emulator-Log -- lief der Emulator?"

grep -q 'SELFHOST: SMOKE OK'   run.log || { echo "  Rauchprobe rot"; tail -20 run.log; exit 1; }
grep -q 'SELFHOST: LAUF ZU ENDE' run.log || {
	echo "  Lauf nicht regulaer beendet:"; grep 'SELFHOST:' run.log; exit 1; }
echo "  ok"

echo "== 6/6 Auswertung =="
# Der Emulator muss weg, bevor ToolShed dasselbe Image liest.
pkill -f "q9.exe.*OS9SYS.qcc-selfhost.hda" 2>/dev/null
sleep 2
"$TS" copy -l -r "$IMG,/stage2.ir" stage2.ir >/dev/null 2>&1 \
	|| die "stage2.ir nicht aus dem Image lesbar"

errs=$(tr -d '\r' < run.log | grep -cE 'qcc: |SEMERR' || true)
last=$(tail -1 stage2.ir)
printf '  Fehlermeldungen im Lauf: %s\n' "$errs"
printf '  Schlusswort:             %s\n' "$last"
printf '  IR-Zeilen 68k / Host:    %s / %s\n' \
	"$(wc -l < stage2.ir | tr -d ' ')" "$(wc -l < stage1.ir | tr -d ' ')"

fail=0
[ "$last" = "OK" ] || { echo "  -> Schlusswort ist nicht OK"; fail=1; }
[ "$errs" -eq 0 ]  || { echo "  -> es gab Semantikfehler"; fail=1; }
if cmp -s stage2.ir stage1.ir; then
	echo "  -> IR byteidentisch zum Host-Lauf"
else
	echo "  -> IR WEICHT AB:"; cmp stage2.ir stage1.ir | head -3
	diff stage2.ir stage1.ir | head -10
	fail=1
fi

echo
[ $KEEP -eq 1 ] && echo "Zwischendateien: $WORK"
if [ $fail -eq 0 ]; then
	echo "SELBSTHOST GRUEN -- Fixpunkt auf echtem 68030 erreicht"
else
	echo "SELBSTHOST ROT"
fi
exit $fail
