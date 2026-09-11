#!/usr/bin/env bash
# Differenztest gegen l68: dieselbe ROF-Datei durch beide Binder, dann
# byteweise vergleichen.
#
# l68 ist OHNE JEDE AUSNAHME reproduzierbar -- ein OS-9-Modul enthaelt kein
# Datum. Es braucht deshalb kein Gegenstueck zu "qr68 -fdate=".
#
# ACHTUNG: der Modulname kommt aus dem AUSGABENAMEN. Beide Seiten muessen
# denselben benutzen, sonst unterscheiden sich Name und CRC.
#
#   ./test/difftest.sh                -- alle Proben in test/
#   ./test/difftest.sh probe.a ...    -- bestimmte
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QL68="$PWD/build/ql68"
[ -x "$QL68" ] || { echo "FEHLER: build/ql68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[ -d "$MWOS/OS9" ] || { echo "uebersprungen: $MWOS fehlt"; exit 0; }

TMP="${KEEP:-$(mktemp -d /tmp/ql68-diff.XXXXXX)}"
mkdir -p "$TMP/l" "$TMP/q"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
TMPWIN="$(printf '%s' "$TMP" | sed 's#/#\\#g')"

wrun() {
	arch -x86_64 "$WINE_BIN" cmd /c \
		"Z: && cd $TMPWIN && set PATH=M:\\DOS\\BIN;%PATH% && $*" 2>&1 | tr -d '\r'
}

files=("$@")
[ ${#files[@]} -gt 0 ] || files=(test/*.a)

echo "=== ql68 gegen l68 (${#files[@]} Proben) ==="
ok=0
bad=0
for f in "${files[@]}"; do
	base="$(basename "$f" .a)"
	cp "$f" "$TMP/$base.a"
	if ! wrun "M:\\DOS\\BIN\\r68.exe $base.a -o=$base.r -q" | grep -q .; then :; fi
	if [ ! -s "$TMP/$base.r" ]; then
		echo "  $(printf '%-14s' "$base") r68 kommt nicht durch -- uebersprungen"
		continue
	fi
	wrun "M:\\DOS\\BIN\\l68.exe $base.r -O=l\\$base" > "$TMP/$base.l68" 2>&1
	if [ ! -s "$TMP/l/$base" ]; then
		echo "  $(printf '%-14s' "$base") l68 kommt nicht durch:"
		sed 's/^/      /' "$TMP/$base.l68" | head -3
		continue
	fi
	if ! "$QL68" "$TMP/$base.r" "-O=$TMP/q/$base" > "$TMP/$base.msg" 2>&1; then
		echo "  $(printf '%-14s' "$base") ql68 bricht ab:"
		sed 's/^/      /' "$TMP/$base.msg" | head -3
		bad=$((bad + 1))
		continue
	fi
	if cmp -s "$TMP/l/$base" "$TMP/q/$base"; then
		echo "  $(printf '%-14s' "$base") gleich ($(wc -c < "$TMP/l/$base" | tr -d ' ') Byte)"
		ok=$((ok + 1))
	else
		echo "  $(printf '%-14s' "$base") ABWEICHUNG:"
		python3 tools/modcmp.py "$TMP/l/$base" "$TMP/q/$base" | sed 's/^/      /'
		bad=$((bad + 1))
	fi
done
echo
echo "  $ok gleich, $bad abweichend"
[ -n "${KEEP:-}" ] && echo "  Zwischendateien: $TMP"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
