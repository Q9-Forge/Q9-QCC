#!/usr/bin/env bash
# Build QCC's OS-9/68K modules with Microware XCC/Wine.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${Q9SDK:?Set Q9SDK to the SDK root (for example /Volumes/SSD1TB/projects/Q9-Forge/Q9-SDK)}"
: "${MWOS_ENV:=/Volumes/SSD1TB/projects/MWOS/tools/macos/env/os9-toolchain.sh}"
[ -f "$MWOS_ENV" ] || { echo "Missing OS-9 toolchain environment: $MWOS_ENV" >&2; exit 2; }
# shellcheck disable=SC1090
source "$MWOS_ENV"
[ -x "$WINE_APP" ] || { echo "Wine is unavailable: $WINE_APP" >&2; exit 2; }
MAC_CMDS="$Q9SDK/macOS/CMDS_CLANG"
[ -x "$MAC_CMDS/qcpp" ] || { echo "Build the Mac qcpp first: $MAC_CMDS/qcpp" >&2; exit 2; }

COMPONENT="${1:-all}"
case "$COMPONENT" in
	all|qcc|qcpp|qcir|qir68k|qo68k|qost|qr68k|ql68k) ;;
	*) echo "Unknown QCC component: $COMPONENT" >&2; exit 2 ;;
esac

STAGE="$(mktemp -d /tmp/q9-xcc-sdk.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/src" "$STAGE/log"
SRC="$STAGE/src"
WINROOT="Z:$(printf '%s' "$STAGE" | sed 's#/#\\#g')"
WINSRC="$WINROOT\\src"
OUT="$Q9SDK/Q9/68k/CMDS_XCC"

cp "$REPO/Q9-FRONTEND-C/q9-qcpp/src/qcpp.c" "$SRC/"
cp "$REPO/Q9-FRONTEND-C/q9-qcir/data/qcc_p.c" "$SRC/qcir.c"
cp "$REPO/Q9-BACKEND-68K/q9-qir68k/src/qcc_backend_c.cpp" "$SRC/qir68k.c"
cp "$REPO/Q9-BACKEND-68K/q9-qir68k/src/qcc_backend_peephole.c" "$SRC/"
cp "$REPO/Q9-BACKEND-68K/q9-qo68k/src/main.c" "$REPO/Q9-BACKEND-68K/q9-qo68k/src/qo68.c" "$SRC/"
cp "$REPO/Q9-OPTIMIZER/q9-qost/src/qost.c" \
	"$REPO/Q9-BACKEND-68K/q9-qr68k/src/qr68.c" \
	"$REPO/Q9-BACKEND-68K/q9-ql68k/src/ql68.c" "$SRC/"

xcc_module() {
	name="$1"; flags="$2"; input="$3"; log="$STAGE/log/$name.log"
	command="M: && cd \\MWOS\\TMP && set MWOS=M:\\MWOS && set PATH=M:\\MWOS\\DOS\\BIN;%PATH% && M:\\MWOS\\DOS\\BIN\\xcc.exe $flags -f=$WINROOT\\$name $WINSRC\\$input"
	if ! arch -x86_64 "$WINE_APP" cmd /c "$command" >"$log" 2>&1 || [ ! -s "$STAGE/$name" ]; then
		echo "XCC failed to build $name:" >&2
		tail -60 "$log" >&2
		return 1
	fi
	echo "XCC $name: $(wc -c <"$STAGE/$name" | tr -d ' ') bytes"
}

build_qir68k() {
	# XCC predates // comments; qcpp resolves headers and strips comments.
	"$MAC_CMDS/qcpp" "-I$MWOS_HOST/SRC/DEFS" "$SRC/qir68k.c" "$STAGE/qir68k-pre.c"
	command="M: && cd \\MWOS\\TMP && set MWOS=M:\\MWOS && set PATH=M:\\MWOS\\DOS\\BIN;%PATH% && M:\\MWOS\\DOS\\BIN\\xcc.exe -mw=M:\\MWOS -tp=68030,ld -olM=512K -f=$WINROOT\\qir68k $WINROOT\\qir68k-pre.c"
	if ! arch -x86_64 "$WINE_APP" cmd /c "$command" >"$STAGE/log/qir68k.log" 2>&1 || [ ! -s "$STAGE/qir68k" ]; then
		echo "XCC failed to build qir68k:" >&2
		tail -60 "$STAGE/log/qir68k.log" >&2
		return 1
	fi
	echo "XCC qir68k: $(wc -c <"$STAGE/qir68k" | tr -d ' ') bytes"
}

build_component() {
	case "$1" in
		qcc)
			(cd "$REPO" && STACK_KB=512 ./tools/build_os9.sh qcc)
			cp "$REPO/Q9-QCC/build/qcc.68k" "$STAGE/qcc" ;;
		qcpp) xcc_module qcpp '-mw=M:\\MWOS -tp=68030,ld -olM=512K' qcpp.c ;;
		qcir) xcc_module qcir '-mw=M:\\MWOS -tp=68030,ld -DQCC_BUFFERED_OUTPUT -olM=1024K' qcir.c ;;
		qir68k) build_qir68k ;;
		qo68k) xcc_module qo68k '-mw=M:\\MWOS -tp=68030,ld -olM=512K' main.c ;;
		qost) xcc_module qost '-mw=M:\\MWOS -tp=68030,ld -olM=512K' qost.c ;;
		qr68k) xcc_module qr68k '-mw=M:\\MWOS -tp=68030,ld -D_Q9OS -olM=512K' qr68.c ;;
		ql68k) xcc_module ql68k '-mw=M:\\MWOS -tp=68030,ld -D_Q9OS -olM=512K' ql68.c ;;
	esac
}

cd "$SRC"
if [ "$COMPONENT" = all ]; then
	for name in qcpp qcir qir68k qo68k qost qr68k ql68k qcc; do build_component "$name"; done
else
	build_component "$COMPONENT"
fi

mkdir -p "$OUT"
if [ "$COMPONENT" = all ]; then
	for name in qcc qcpp qcir qir68k qo68k qost qr68k ql68k; do cp "$STAGE/$name" "$OUT/$name"; done
else
	cp "$STAGE/$COMPONENT" "$OUT/$COMPONENT"
fi
echo "XCC module(s) staged in $OUT"
