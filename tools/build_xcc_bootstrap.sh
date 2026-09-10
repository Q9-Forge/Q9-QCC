#!/usr/bin/env bash
# Baut den QCC-Parser mit der Microware-Toolchain (xcc via Wine) zu einem
# echten OS-9/68K-Modul und erzeugt den passenden Bootstrap-Quelltext.
#
# Ergebnis:
#   build/q9_qcc_p_xcc_bootstrap   68k-Modul (der Compiler selbst)
#   build/qcc_p.xcc.i              xcc-vorverarbeiteter Parser
#   build/qcc_p.bootstrap.c        daraus der QCC-lesbare Bootstrap-Quelltext
#
# Die drei Optionen, ohne die es NICHT geht (2026-08-31 rekonstruiert, vorher
# nirgends aufgeschrieben):
#
#   -mw=M:\MWOS            Das Wine-Laufwerk m: zeigt auf .../projects, nicht
#                          auf MWOS selbst. Ohne -mw findet xcc 'acstart.r'
#                          nicht. Die alten .bat-Dateien unter MWOS/ gehen noch
#                          von "M: = MWOS" aus und sind insofern veraltet.
#   -dQCC_BUFFERED_OUTPUT  Eigener 8-KB-Writer statt Microware-printf pro
#                          IR-Zeile -- sonst ist der Lauf im Emulator
#                          unbrauchbar langsam.
#   -olM=1024K             1 MB zusaetzlicher Stack IM MODUL. Der Parser ist
#                          rekursiv absteigend; mit dem Standard-Stack (3072
#                          Byte) bricht schon die Rauchprobe mit
#                          "**** Stack Overflow ****" ab. Syntax: -<phase><opt>,
#                          hier Phase 'ol' (Object Code Linker) + dessen
#                          -M=<n>K. Ein blankes -M=/-m= lehnt xcc mit
#                          "invalid phase for stack space option" ab.
#
# Ausserdem: xcc braucht denselben MWOS/PATH-Kontext wie os9make, sonst
# scheitert es an "executive received argument list length error while forking
# 'cpfe'". Deshalb der Aufruf via cmd /c mit gesetztem PATH.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

: "${STACK_KB:=1024}"
MODULE="${1:-q9_qcc_p_xcc_bootstrap}"

source /Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh

STAGE="$(mktemp -d /tmp/qcc-xcc-build.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
cp Data/qcc_p.c "$STAGE/"
STAGE_WIN="Z:$(printf '%s' "$STAGE" | sed 's#/#\\#g')"

wine_xcc() {
    arch -x86_64 "$WINE_APP" cmd /c \
        "M: && cd \\MWOS\\TMP && set MWOS=M:\\MWOS && set PATH=M:\\MWOS\\DOS\\BIN;%PATH% && M:\\MWOS\\DOS\\BIN\\xcc.exe $*"
}

echo "== 1/3 Parser zu einem OS-9-Modul kompilieren+linken =="
wine_xcc "-mw=M:\\MWOS -tp=68030,ld -dQCC_BUFFERED_OUTPUT -olM=${STACK_KB}K" \
         "-f=${STAGE_WIN}\\${MODULE} ${STAGE_WIN}\\qcc_p.c"
[ -f "$STAGE/$MODULE" ] || { echo "FEHLER: $MODULE nicht erzeugt"; exit 1; }
cp "$STAGE/$MODULE" "build/$MODULE"

echo "== 2/3 Modulkopf pruefen =="
"$MWOS_TOOLSHED_OS9" ident "build/$MODULE" |
    grep -E "Module size|Data size|Stack size|CRC|parity"
# Ein Modul mit Standard-Stack laeuft im Emulator sofort in einen Stack
# Overflow -- lieber hier abbrechen als 20 Minuten Emulatorlauf verschwenden.
if "$MWOS_TOOLSHED_OS9" ident "build/$MODULE" | grep -q 'Stack size: *\$C00\b'; then
    echo "FEHLER: Stack size ist der Standardwert -- -olM= hat nicht gegriffen"
    exit 1
fi

echo "== 3/3 Bootstrap-Quelltext erzeugen =="
wine_xcc "-pp -mw=M:\\MWOS ${STAGE_WIN}\\qcc_p.c" > build/qcc_p.xcc.i
python3 tools/bootstrap_prepare.py build/qcc_p.xcc.i build/qcc_p.bootstrap.c

# ---------------------------------------------------------------------------
# Selbsttest: QCC muss die eben erzeugte Bootstrap-Quelle fehlerfrei
# uebersetzen. Ohne diesen Schritt faellt hier nichts auf -- die Suite in
# Q9-Parsec uebersetzt immer Data/qcc_p.c direkt, nie die xcc-vorverarbeitete
# Fassung. Genau daran ist am 2026-09-01 eine fehlende realloc-Deklaration
# unbemerkt durchgegangen: sie stand im Kopf des erzeugten Parsers und damit
# VOR dem Marker, an dem bootstrap_prepare.py den Header-Vorspann abschneidet.
echo "== Selbsttest: QCC uebersetzt die Bootstrap-Quelle =="
if ! ./build/qcir "@build/qcc_p.bootstrap.c" > build/qcc_p.bootstrap.ir 2> build/qcc_p.bootstrap.err; then
	echo "FEHLGESCHLAGEN -- Meldungen:" >&2
	cat build/qcc_p.bootstrap.err >&2
	exit 1
fi
echo "  ok ($(wc -l < build/qcc_p.bootstrap.ir | tr -d ' ') IR-Zeilen, Schlusswort $(tail -1 build/qcc_p.bootstrap.ir))"

echo
echo "Fertig."
echo "Im Emulator (Q9-Flux), Modul + Quelltext vorher per ToolShed ins Image:"
echo "  os9 copy -r build/$MODULE   IMG,/CMDS/$MODULE"
echo "  os9 copy -l -r build/qcc_p.bootstrap.c IMG,/qcc_p.bootstrap.c"
echo "  expect -f test/expect/test_qcc_xcc_bootstrap_ok.exp   # in Q9-Flux"
