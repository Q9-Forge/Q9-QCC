#!/usr/bin/env bash
# Baut qcpp mit der Microware-Toolchain (xcc via Wine) zu einem OS-9/68K-Modul.
#
# Die Optionen sind dieselben, die bei Q9-QCC/tools/build_xcc_bootstrap.sh
# muehsam rekonstruiert wurden -- deshalb hier gleich mit derselben
# Begruendung:
#
#   -mw=M:\MWOS      Das Wine-Laufwerk m: zeigt auf .../projects, nicht auf
#                    MWOS selbst; ohne -mw findet xcc 'acstart.r' nicht.
#   -tp=68030,ld     Zielprozessor, wie beim Compiler selbst.
#   -olM=<n>K        Stack IM MODUL. qcpps #if-Auswerter und die
#                    Makroexpansion steigen rekursiv ab; der Standardstack
#                    (3072 Byte) reicht dafuer nicht.
#   -k=<n>K          Datenbereich: qcpp haelt seine Tabellen als feste
#                    globale Felder (kein malloc, s. README) -- das sind
#                    ueber 3 MB, die der Linker als Datengroesse einplanen
#                    muss.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

: "${STACK_KB:=512}"
MODULE="${1:-qcpp}"

source /Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh

STAGE="$(mktemp -d /tmp/qcpp-xcc-build.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
cp src/qcpp.c "$STAGE/"
STAGE_WIN="Z:$(printf '%s' "$STAGE" | sed 's#/#\\#g')"

wine_xcc() {
    arch -x86_64 "$WINE_APP" cmd /c \
        "M: && cd \\MWOS\\TMP && set MWOS=M:\\MWOS && set PATH=M:\\MWOS\\DOS\\BIN;%PATH% && M:\\MWOS\\DOS\\BIN\\xcc.exe $*"
}

echo "== qcpp zu einem OS-9-Modul kompilieren+linken =="
wine_xcc "-mw=M:\\MWOS -tp=68030,ld -olM=${STACK_KB}K" \
         "-f=${STAGE_WIN}\\${MODULE} ${STAGE_WIN}\\qcpp.c"
[ -f "$STAGE/$MODULE" ] || { echo "FEHLER: $MODULE nicht erzeugt"; exit 1; }
mkdir -p build
cp "$STAGE/$MODULE" "build/$MODULE.68k"

echo "== Modulkopf =="
"$MWOS_TOOLSHED_OS9" ident "build/$MODULE.68k" |
    grep -E "Module size|Data size|Stack size|CRC|parity"
if "$MWOS_TOOLSHED_OS9" ident "build/$MODULE.68k" | grep -q 'Stack size: *\$C00\b'; then
    echo "FEHLER: Stack size ist der Standardwert -- -olM= hat nicht gegriffen"
    exit 1
fi

echo
echo "Fertig: build/$MODULE.68k"
echo "Ins Image und im Emulator (Q9-Flux) fahren:"
echo "  os9 copy -r build/$MODULE.68k IMG,/CMDS/$MODULE"
echo "  os9 attr -e -w -r -pe -pr IMG,/CMDS/$MODULE"
