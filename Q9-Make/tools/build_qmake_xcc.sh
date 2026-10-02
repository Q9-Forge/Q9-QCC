#!/usr/bin/env bash
# Build qmake as an OS-9/68030 module with the Microware XCC toolchain.
#
# qmake.c and platform_posix.c compile UNCHANGED here: Microware's own
# UNIX-compatibility layer (unix.l + sys_clib.l) provides a real
# struct-stat-based stat()/chdir()/popen(), so the same host platform
# adapter used on macOS/Linux applies on Q9/68K too, through XCC's C89
# compiler -- unlike a self-hosted QCC build, which cannot parse qmake.c's
# structs/sizeof/malloc yet (see docs/SELFHOSTING_LUECKENLISTE.md in
# Q9-QCC). Only the #include search path differs, bridged by the three
# tiny headers in os9-xcc-include/ (this SDK spells POSIX headers
# differently: DEFS/UNIX/stat.h instead of sys/stat.h, and so on).
set -euo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${STACK_KB:=512}"

source /Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh
STAGE="$(mktemp -d /tmp/qmake-xcc-build.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
cp "$PROJECT/src/qmake.c" "$PROJECT/src/platform_posix.c" "$PROJECT/src/platform.h" "$STAGE/"
STAGE_WIN="Z:$(printf '%s' "$STAGE" | sed 's#/#\\#g')"
SHIM_WIN="Z:$(printf '%s' "$PROJECT/src/os9-xcc-include" | sed 's#/#\\#g')"
DEFS_WIN="Z:$(printf '%s' "$MWOS_HOST/SRC/DEFS" | sed 's#/#\\#g')"

arch -x86_64 "$WINE_APP" cmd /c \
  "M: && cd \\MWOS\\TMP && set MWOS=M:\\MWOS && set PATH=M:\\MWOS\\DOS\\BIN;%PATH% && M:\\MWOS\\DOS\\BIN\\xcc.exe -mw=M:\\MWOS -tp=68030,ld -v=${SHIM_WIN} -v=${STAGE_WIN} -v=${DEFS_WIN} -olM=${STACK_KB}K -l=M:\\MWOS\\OS9\\68020\\LIB\\unix.l -l=M:\\MWOS\\OS9\\68020\\LIB\\sys_clib.l -f=${STAGE_WIN}\\qmake ${STAGE_WIN}\\qmake.c ${STAGE_WIN}\\platform_posix.c"

[ -f "$STAGE/qmake" ] || { echo "error: qmake was not created" >&2; exit 1; }
mkdir -p "$PROJECT/build"
cp "$STAGE/qmake" "$PROJECT/build/qmake.68k"
"$MWOS_TOOLSHED_OS9" ident "$PROJECT/build/qmake.68k" | grep -E 'Module size|Data size|Stack size|CRC|parity'
echo "qmake (XCC, Q9/68K): $PROJECT/build/qmake.68k"
