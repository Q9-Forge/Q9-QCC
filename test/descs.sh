#!/usr/bin/env bash
# Differenztest an den DESCRIPTOREN des MWOS-SDK -- den einfachsten echten
# Modulen: kein eigener Code, dafuer die volle mod_dev-Kopferweiterung,
# eine Bibliothek (sys.l) und rund zwei Dutzend externe Referenzen je Datei.
#
# Aufgerufen wird so, wie es das SDK-Makefile tut:
#   l68 <desc>.r -l=<sys.l> -gu=0.0 -p=577 -O=<modul>
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QL68="$PWD/build/ql68"
TOOLS="$PWD/tools"
[ -x "$QL68" ] || { echo "FEHLER: build/ql68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${PORTDIR:=$MWOS/OS9/68060/PORTS/MVME172/SCF}"
: "${DESCDIR:=$MWOS/OS9/SRC/IO/SCF/DESC}"
[ -d "$DESCDIR" ] || { echo "uebersprungen: $DESCDIR fehlt"; exit 0; }

TMP="${KEEP:-$(mktemp -d /tmp/ql68-desc.XXXXXX)}"
mkdir -p "$TMP/l" "$TMP/q"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
# Ohne Laufwerksbuchstaben landet der Pfad auf M:, wo er nicht existiert.
TMPWIN="Z:$(printf '%s' "$TMP" | sed 's#/#\\#g')"
winpath() { printf 'M:%s' "$(printf '%s' "${1#$MWOS}" | sed 's#/#\\#g')"; }
PORTWIN="$(winpath "$PORTDIR")"
DESCWIN="$(winpath "$DESCDIR")"
LIB="$MWOS/OS9/68000/LIB/sys.l"
LIBWIN="$(winpath "$LIB")"

wrun() { arch -x86_64 "$WINE_BIN" cmd /c "$*" 2>&1 | tr -d '\r'; }

names=("$@")
if [ ${#names[@]} -eq 0 ]; then
	for f in "$DESCDIR"/*.a; do
		case "$f" in *"("*) continue;; esac
		names+=("$(basename "$f" .a)")
	done
fi

echo "=== ql68 gegen l68, SDK-Descriptoren (${#names[@]}) ==="
ok=0; bad=0; skip=0
for d in "${names[@]}"; do
	rm -f "$TMP/$d.r" "$TMP/l/$d" "$TMP/q/$d"
	wrun "M: && cd ${PORTWIN#*:} && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe $DESCWIN\\$d.a -o=$TMPWIN\\$d.r -q -u=. -u=M:\\OS9\\SRC\\DEFS" >/dev/null
	if [ ! -s "$TMP/$d.r" ]; then
		skip=$((skip + 1)); continue
	fi
	wrun "Z: && cd ${TMPWIN#*:} && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe $d.r -l=$LIBWIN -gu=0.0 -p=577 -O=l\\$d" >/dev/null
	if [ ! -s "$TMP/l/$d" ]; then
		skip=$((skip + 1)); continue
	fi
	if ! "$QL68" "$TMP/$d.r" "-l=$LIB" -gu=0.0 -p=577 "-O=$TMP/q/$d" > "$TMP/$d.msg" 2>&1; then
		echo "  $(printf '%-12s' "$d") ql68 bricht ab:"
		sed 's/^/      /' "$TMP/$d.msg" | head -2
		bad=$((bad + 1)); continue
	fi
	if cmp -s "$TMP/l/$d" "$TMP/q/$d"; then
		ok=$((ok + 1))
	else
		echo "  $(printf '%-12s' "$d") ABWEICHUNG:"
		python3 "$TOOLS/modcmp.py" "$TMP/l/$d" "$TMP/q/$d" | sed 's/^/      /' | head -6
		bad=$((bad + 1))
	fi
done
echo
echo "  $ok gleich, $bad abweichend, $skip uebersprungen"
[ -n "${KEEP:-}" ] && echo "  Zwischendateien: $TMP"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
