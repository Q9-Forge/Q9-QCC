#!/usr/bin/env bash
# Differenztest fuer MEHRERE psects in einem Modul: die Quellen unter
# test/multi/ werden gemeinsam gebunden -- mmain.a ist der Wurzel-psect,
# msub.a ein Unterprogramm mit eigenen Daten und einem globalen Namen.
#
# Sie liegen bewusst nicht in test/, weil difftest.sh dort jede Datei
# EINZELN bindet -- msub.a hat keinen Wurzel-psect und mmain.a allein
# einen unaufgeloesten Namen.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QL68="$PWD/build/ql68"
[ -x "$QL68" ] || { echo "FEHLER: build/ql68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[ -d "$MWOS/OS9" ] || { echo "uebersprungen: $MWOS fehlt"; exit 0; }

TMP="${KEEP:-$(mktemp -d /tmp/ql68-multi.XXXXXX)}"
mkdir -p "$TMP/l" "$TMP/q"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
TMPWIN="Z:$(printf '%s' "$TMP" | sed 's#/#\\#g')"
wrun() { arch -x86_64 "$WINE_BIN" cmd /c "Z: && cd ${TMPWIN#*:} && set PATH=M:\\DOS\\BIN;%PATH% && $*" 2>&1 | tr -d '\r'; }

cp test/multi/*.a "$TMP/"
rofs=()
for f in test/multi/*.a; do
	b="$(basename "$f" .a)"
	wrun "M:\\DOS\\BIN\\r68.exe $b.a -o=$b.r -q" >/dev/null
	[ -s "$TMP/$b.r" ] || { echo "  r68 kommt bei $b nicht durch"; exit 1; }
	rofs+=("$b")
done

# mmain zuerst -- der Wurzel-psect muss die erste Eingabe sein.
order=(mmain)
for b in "${rofs[@]}"; do
	[ "$b" = "mmain" ] || order+=("$b")
done

wl=""; ql=()
for b in "${order[@]}"; do wl="$wl $b.r"; ql+=("$TMP/$b.r"); done

echo "=== ql68 gegen l68, mehrere psects (${#order[@]}) ==="
wrun "M:\\DOS\\BIN\\l68.exe$wl -O=l\\multi" >/dev/null
[ -s "$TMP/l/multi" ] || { echo "  l68 kommt nicht durch"; exit 1; }
if ! "$QL68" "${ql[@]}" "-O=$TMP/q/multi" > "$TMP/msg" 2>&1; then
	echo "  ql68 bricht ab:"; sed 's/^/      /' "$TMP/msg" | head -3; exit 1
fi
if cmp -s "$TMP/l/multi" "$TMP/q/multi"; then
	echo "  multi          gleich ($(wc -c < "$TMP/l/multi" | tr -d ' ') Byte)"
	echo
	echo "  1 gleich, 0 abweichend"
	exit 0
fi
echo "  multi          ABWEICHUNG:"
python3 tools/modcmp.py "$TMP/l/multi" "$TMP/q/multi" | sed 's/^/      /'
exit 1
