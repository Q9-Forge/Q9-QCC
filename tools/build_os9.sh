#!/usr/bin/env bash
# Build the QCC driver as an OS-9/68030 module with the Microware toolchain.
set -euo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../Q9-QCC" && pwd)"
: "${STACK_KB:=256}"
MODULE="${1:-qcc}"

source /Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh
STAGE="$(mktemp -d /tmp/qcc-driver-build.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
cp "$PROJECT/src/qcc.c" "$STAGE/"
STAGE_WIN="Z:$(printf '%s' "$STAGE" | sed 's#/#\\#g')"

arch -x86_64 "$WINE_APP" cmd /c \
  "M: && cd \\MWOS\\TMP && set MWOS=M:\\MWOS && set PATH=M:\\MWOS\\DOS\\BIN;%PATH% && M:\\MWOS\\DOS\\BIN\\xcc.exe -mw=M:\\MWOS -tp=68030,ld -olM=${STACK_KB}K -l=M:\\MWOS\\OS9\\68020\\LIB\\sys_clib.l -f=${STAGE_WIN}\\${MODULE} ${STAGE_WIN}\\qcc.c"

[ -f "$STAGE/$MODULE" ] || { echo "error: $MODULE was not created" >&2; exit 1; }
mkdir -p "$PROJECT/build"
cp "$STAGE/$MODULE" "$PROJECT/build/$MODULE.68k"
"$MWOS_TOOLSHED_OS9" ident "$PROJECT/build/$MODULE.68k" | grep -E 'Module size|Data size|Stack size|CRC|parity'
