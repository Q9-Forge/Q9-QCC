#!/usr/bin/env bash
# Build the OS-9 qcc driver with QCC; use MWOS l68 for its libgen libraries.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${Q9SDK:?Set Q9SDK to the SDK root (for example /Volumes/SSD1TB/projects/Q9-Forge/Q9-SDK)}"
: "${MWOS_ENV:=/Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh}"
[ -f "$MWOS_ENV" ] || { echo "Missing OS-9 toolchain environment: $MWOS_ENV" >&2; exit 2; }
# shellcheck disable=SC1090
source "$MWOS_ENV"
[ -x "$WINE_APP" ] || { echo "Wine is unavailable: $WINE_APP" >&2; exit 2; }
[ -x "$Q9SDK/macOS/CMDS_CLANG/qcc" ] || { echo "Build the Mac qcc first" >&2; exit 2; }

STAGE="$(mktemp -d /tmp/qcc-selfhost.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
STAGE_WIN="Z:$(printf '%s' "$STAGE" | sed 's#/#\\#g')"
REPO_WIN="Z:$(printf '%s' "$REPO" | sed 's#/#\\#g')"
OUT="$Q9SDK/Q9/68k/CMDS_QCC/qcc"
mkdir -p "$(dirname "$OUT")"

# qcc_q9.c selects the native OS-9 process-launch branch while the Mac-hosted
# QCC performs translation and emits a 68000 ROF.
Q9SDK="$Q9SDK" QCC_CONFIG="$Q9SDK/macOS/SYS/qcc.conf" \
PATH="$Q9SDK/macOS/CMDS_CLANG:/usr/bin:/bin:/usr/sbin:/sbin" \
	"$Q9SDK/macOS/CMDS_CLANG/qcc" --keep -c --tmpdir "$STAGE/work" \
	-o "$STAGE/qcc.r" "$REPO/Q9-QCC/src/qcc_q9.c"
[ -s "$STAGE/qcc.r" ] || { echo "QCC did not produce qcc.r" >&2; exit 1; }

# os9exec/os9forkc/wait and stdio redirection deliberately use the Microware
# ABI; keep that small bridge XCC-built, then link both ROFs with MWOS l68.
cmd="M: && cd \\MWOS\\TMP && set MWOS=M:\\MWOS && set PATH=M:\\MWOS\\DOS\\BIN;%PATH% && M:\\MWOS\\DOS\\BIN\\xcc.exe -mw=M:\\MWOS -tp=68030 -e=as -f=$STAGE_WIN\\qcc_os9_bridge.r $REPO_WIN\\Q9-QCC\\src\\qcc_os9_bridge.c"
if ! arch -x86_64 "$WINE_APP" cmd /c "$cmd" >"$STAGE/xcc-bridge.log" 2>&1 || [ ! -s "$STAGE/qcc_os9_bridge.r" ]; then
	echo "XCC failed to build the OS-9 ABI bridge:" >&2
	tail -60 "$STAGE/xcc-bridge.log" >&2
	exit 1
fi

# l68 resolves libraries in command-line order.  sys_clib pulls in routines
# such as os9forkc/munlink and modloadp, which themselves reference F$Fork,
# F$UnLink, F$Wait, and _os_loadp.  Put the providers (os_lib/sys.l) after
# sys_clib so l68's one-pass archive scan can resolve those references.
cmd="Z: && cd \\tmp && set PATH=M:\\MWOS\\DOS\\BIN;%PATH% && M:\\MWOS\\DOS\\BIN\\l68.exe -a -M=512 M:\\MWOS\\OS9\\68000\\LIB\\ansi_cstart.r $STAGE_WIN\\qcc.r $STAGE_WIN\\qcc_os9_bridge.r -l=M:\\MWOS\\OS9\\68020\\LIB\\clib.l -l=M:\\MWOS\\OS9\\68020\\LIB\\sys_clib.l -l=M:\\MWOS\\OS9\\68020\\LIB\\os_lib.l -l=M:\\MWOS\\OS9\\68000\\LIB\\sys.l -gu=0.0 -p=577 -O=$STAGE_WIN\\qcc"
if ! arch -x86_64 "$WINE_APP" cmd /c "$cmd" >"$STAGE/l68.log" 2>&1 || [ ! -s "$STAGE/qcc" ]; then
	echo "MWOS l68 failed to link the QCC-generated driver:" >&2
	tail -60 "$STAGE/l68.log" >&2
	exit 1
fi

IDENT="$MWOS_TOOLSHED_OS9"
"$IDENT" ident "$STAGE/qcc" >"$STAGE/ident.log"
grep -q 'Good CRC' "$STAGE/ident.log"
grep -q 'Good parity' "$STAGE/ident.log"
cp "$STAGE/qcc" "$OUT"
cat "$STAGE/ident.log" | grep -E 'Module size|Data size|Stack size|CRC|parity'
echo "QCC-built OS-9 driver staged at $OUT"
