#!/usr/bin/env bash
# Copy one or more OS-9/68K modules onto a Q9 disk image and immediately fix
# their RBF filesystem execute bit.
#
# 2026-10-03: every module we build (via XCC or via our own ql68k) ends up
# on the image with a correct module-header permission ($555, "e-r-e-r-e-r"
# as shown by `ident`) but WITHOUT the separate RBF filesystem execute bit
# ("os9 attr" shows "----r-wr" instead of "--e-rewr"). F$Fork (and therefore
# os9exec()/os9forkc()/system()) checks that filesystem bit strictly and
# fails with E$PNNF/-1; a plain interactive shell start only needs read
# permission, so the bug stays invisible until something tries to fork the
# module programmatically. Root cause of the copy itself is still open
# (suspected: ToolShed's "os9 copy" on macOS, matching the older,
# driver-module-specific finding in the "Q9 ToolShed Execute-Bit-Bug" memory)
# -- this script is the practical workaround until that is fixed upstream.
#
# IMPORTANT: never run this while the target image is mounted in a running
# q9.exe -- concurrent host + emulator writes can corrupt the RBF image
# (observed once this session on /dd/PROJECTS/hello). Stop the emulator
# first.
set -euo pipefail

OS9=/Volumes/SSD1TB/projects/MWOS/tools/macos/bin/os9

usage() {
	echo "Usage: $0 <image-path> <source-dir> <dest-dir-on-image> <module> [<module> ...]" >&2
	echo "Example: $0 \$IMG /path/to/CMDS_XCC CMDS qcc qcpp qcir qir68k qo68k qr68k ql68k qost" >&2
	exit 2
}

[ $# -ge 4 ] || usage

IMAGE="$1"; shift
SRCDIR="$1"; shift
DESTDIR="$1"; shift

if pgrep -f "build/macos/q9.exe" >/dev/null 2>&1; then
	echo "Refusing to run: a q9.exe emulator process is currently running." >&2
	echo "Stop it first (host writes while the emulator holds the image can corrupt it)." >&2
	exit 1
fi

for module in "$@"; do
	src="$SRCDIR/$module"
	if [ ! -f "$src" ]; then
		echo "Skipping $module: not found at $src" >&2
		continue
	fi
	"$OS9" copy -r "$src" "$IMAGE,$DESTDIR/$module"
	"$OS9" attr -e -pe "$IMAGE,$DESTDIR/$module"
	echo "Deployed + execute-fixed: $DESTDIR/$module"
done
