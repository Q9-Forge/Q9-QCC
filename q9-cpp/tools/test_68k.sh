#!/usr/bin/env bash
# qcpp auf echtem 68030 pruefen: Modul bauen, ins OS-9-Image legen, im
# Emulator laufen lassen, Ergebnis herausholen und BYTEWEISE gegen den
# Hostlauf derselben Quelle vergleichen.
#
# Der Vergleich ist der Punkt. Dass das Modul im Emulator ohne Absturz
# durchlaeuft, sagt fuer sich genommen wenig -- die Frage ist, ob es DASSELBE
# herausbekommt wie am Host.
#
# Umgebungsvariablen (alle mit brauchbarem Standard):
#   Q9FLUX  Verzeichnis mit build/macos/q9.exe und local_images/
#   BASE    Ausgangsimage, das bis zur Shell bootet
#   MWOS    SDK-Wurzel (fuer das Boot-ROM)
#
# Fallstricke, die hier schon Zeit gekostet haben und deshalb eingebaut sind:
#   - Auf einer KOPIE des Images arbeiten. ToolShed schreibt an einem
#     laufenden Emulator vorbei direkt in die Datei; auf dem Mac laufen oft
#     mehrere Emulatoren, auch aus anderen Sitzungen. "cp -c" ist ein
#     CoW-Klon und kostet nichts.
#   - ToolShed "copy -r" setzt KEIN e-Attribut -> OS-9 startet das Modul
#     nicht (Fehler 214). Deshalb danach "attr -e".
#   - Die Pruefquelle mit "copy -l" kopieren (Zeilenenden), das Modul mit
#     "copy -r" (roh).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${Q9FLUX:=/Volumes/SSD1TB/projects/Q9-Forge/Q9-Flux-68k}"
: "${BASE:=$Q9FLUX/local_images/OS9SYS.hda}"

IMAGE_NAME=OS9SYS.qcpp-test.hda
IMAGE="$Q9FLUX/local_images/$IMAGE_NAME"

[ -x "$Q9FLUX/build/macos/q9.exe" ] || {
	echo "FEHLER: $Q9FLUX/build/macos/q9.exe fehlt (in Q9-Flux: make host)"
	exit 2
}
[ -f "$BASE" ] || { echo "FEHLER: Ausgangsimage $BASE fehlt"; exit 2; }

# Den Unix-Pfad VOR dem Sourcen retten: os9-toolchain.sh setzt MWOS auf den
# Wine-Pfad um (Z:...), und mit dem findet der Emulator sein Boot-ROM nicht.
MWOS_UNIX="$MWOS"
source "$MWOS/tools/macos/env/os9-toolchain.sh"
OS9="$MWOS_TOOLSHED_OS9"

echo "== 1/5 OS-9-Modul bauen =="
[ -f build/qcpp.68k ] || ./tools/build_os9.sh
"$OS9" ident build/qcpp.68k | grep -E "Module size|Data size|Stack size"

echo "== 2/5 Hostlauf als Referenz =="
make -s build/qcpp
./build/qcpp test/qcpptest.c build/qcpptest.host.i
echo "  $(wc -c < build/qcpptest.host.i | tr -d ' ') Byte"

echo "== 3/5 Image klonen und bestuecken =="
cp -c "$BASE" "$IMAGE"
"$OS9" copy -r build/qcpp.68k "$IMAGE,/CMDS/qcpp"
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/qcpp" > /dev/null
"$OS9" copy -l -r test/qcpptest.c "$IMAGE,/qcpptest.c"
echo "  $IMAGE_NAME fertig"

echo "== 4/5 Emulatorlauf =="
Q9FLUX="$Q9FLUX" QCPP_IMAGE="local_images/$IMAGE_NAME" MWOS="$MWOS_UNIX" \
	expect -f "$REPO/test/run_68k.exp" > build/run_68k.log 2>&1 || {
		echo "FEHLGESCHLAGEN -- letzte Zeilen des Protokolls:"
		tail -25 build/run_68k.log
		exit 1
	}
# "|| true": ohne Treffer wuerde grep unter set -e das Skript beenden
grep -E "<<<" build/run_68k.log | sed 's/^/  /' || true

echo "== 5/5 Ergebnis herausholen und vergleichen =="
rm -f build/qcpptest.68k.i
"$OS9" copy -l "$IMAGE,/qcpptest.i" build/qcpptest.68k.i
if cmp -s build/qcpptest.host.i build/qcpptest.68k.i; then
	echo "  BYTEIDENTISCH zum Hostlauf ($(wc -c < build/qcpptest.68k.i | tr -d ' ') Byte)"
	echo
	echo "qcpp laeuft auf echtem 68030 und liefert dasselbe wie am Host."
	exit 0
fi
echo "  ABWEICHUNG:"
diff -u build/qcpptest.host.i build/qcpptest.68k.i | head -30 | sed 's/^/  /'
exit 1
