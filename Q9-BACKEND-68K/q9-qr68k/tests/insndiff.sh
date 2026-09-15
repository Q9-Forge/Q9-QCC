#!/usr/bin/env bash
# Instruction comparison against r68, source line by source line.
#
# Unlike difftest.sh, which compares the complete ROF byte-for-byte and stops
# at the first difference, this runs r68 with a listing and maps every
# difference to its source line. This allows the instruction table to be
# processed in one pass.
#
#   ./tests/insndiff.sh                 -- tests/insn.a
#   ./tests/insndiff.sh file.a ...
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QR68=build/qr68k
[ -x "$QR68" ] || { echo "FEHLER: $QR68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS_HOST:=/Volumes/SSD1TB/projects/MWOS}"
MWOS="$MWOS_HOST"
TMP="$(mktemp -d /tmp/qr68-insn.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
TMPWIN="$(printf '%s' "$TMP" | sed 's#/#\\#g')"

# Additional flags for BOTH sides, for example RFLAGS=-b.
: "${RFLAGS:=}"

files=("$@")
[ ${#files[@]} -gt 0 ] || files=(tests/insn.a tests/dir.a tests/mac.a tests/fpu.a)

fail=0
for f in "${files[@]}"; do
	base="$(basename "$f" .a)"
	cp "$f" "$TMP/$base.a"
	echo "=== $f ==="
	arch -x86_64 "$WINE_BIN" cmd /c \
		"Z: && cd $TMPWIN && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe $base.a -o=$base.r -l $RFLAGS" \
		> "$TMP/$base.lst" 2>&1
	tr -d '\r' < "$TMP/$base.lst" > "$TMP/$base.lst2"
	mv "$TMP/$base.lst2" "$TMP/$base.lst"
	if grep -q "^\*\*\* error" "$TMP/$base.lst"; then
		echo "  r68 meldet Fehler:"
		grep -A2 "^\*\*\* error" "$TMP/$base.lst" | head -12 | sed 's/^/    /'
		fail=1
		continue
	fi
	stamp="$(python3 - "$TMP/$base.r" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
print(",".join(str(b) for b in d[12:18]))
PY
)"
	if ! "$QR68" $RFLAGS "-fdate=$stamp" "$TMP/$base.a" "$TMP/$base.q" > "$TMP/$base.msg" 2>&1; then
		echo "  qr68 bricht ab:"
		sed 's/^/    /' "$TMP/$base.msg" | head -5
		fail=1
		continue
	fi
	python3 tools/insncmp.py "$TMP/$base.lst" "$TMP/$base.r" "$TMP/$base.q" \
		"$TMP/$base.a" || fail=1
	# Also compare the complete file so references and the header are covered.
	python3 tools/rofcmp.py "$base:$TMP/$base.r:$TMP/$base.q" > "$TMP/$base.full" ||
		{ sed 's/^/  /' "$TMP/$base.full" | head -6; fail=1; }
done
exit $fail
