#!/usr/bin/env bash
# Build qcir as an OS-9/68030 module with the Microware toolchain.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${STACK_KB:=1024}"
MODULE="${1:-qcir}"

source /Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh
STAGE_ROOT="$(mktemp -d /tmp/qcir-build.XXXXXX)"
STAGE="$STAGE_ROOT/source"
mkdir -p "$STAGE"
trap 'rm -rf "$STAGE_ROOT"' EXIT
cp "$REPO/data/qcc_p.c" "$STAGE/"
# qcc_p.c keeps the shared QCC headers as a repository-relative include.
# Recreate that small layout next to the temporary source tree for xcc.
mkdir -p "$STAGE/../../q9-qcpp"
cp -R "$(cd "$REPO/../q9-qcpp" && pwd)/include" "$STAGE/../../q9-qcpp/"
STAGE_WIN="Z:$(printf '%s' "$STAGE" | sed 's#/#\\#g')"

arch -x86_64 "$WINE_APP" cmd /c \
  "M: && cd \\MWOS\\TMP && set MWOS=M:\\MWOS && set PATH=M:\\MWOS\\DOS\\BIN;%PATH% && M:\\MWOS\\DOS\\BIN\\xcc.exe -mw=M:\\MWOS -tp=68030,ld -DQCC_BUFFERED_OUTPUT -olM=${STACK_KB}K -f=${STAGE_WIN}\\${MODULE} ${STAGE_WIN}\\qcc_p.c"

[ -f "$STAGE/$MODULE" ] || { echo "error: $MODULE was not created" >&2; exit 1; }
mkdir -p "$REPO/build"
cp "$STAGE/$MODULE" "$REPO/build/$MODULE.68k"
"$MWOS_TOOLSHED_OS9" ident "$REPO/build/$MODULE.68k" | grep -E 'Module size|Data size|Stack size|CRC|parity'
