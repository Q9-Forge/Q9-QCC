#!/usr/bin/env bash
# Differenztest fuer die ROHE Binaerausgabe (-r=<basis>).
#
# Die Proben unter test/raw/ werden mit beiden Bindern uebersetzt und
# byteweise verglichen. Sie decken die Faelle ab, an denen die Zeigerlisten
# haengen: keine Zeiger, nur Code-, nur Daten-, beide Arten, ein bis drei
# Stueck -- und die Basis einmal 0 und einmal $1000, damit sichtbar wird,
# dass sie NUR auf Codebezuege im Code wirkt.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QL68="$PWD/build/ql68"
[ -x "$QL68" ] || { echo "FEHLER: build/ql68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[ -d "$MWOS/OS9" ] || { echo "uebersprungen: $MWOS fehlt"; exit 0; }

TMP="${KEEP:-$(mktemp -d /tmp/ql68-raw.XXXXXX)}"
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

echo "=== ql68 gegen l68, rohe Binaerausgabe ==="
ok=0
bad=0
for f in test/raw/*.a; do
	b="$(basename "$f" .a)"
	cp "$f" "$TMP/"
	wrun "M:\\DOS\\BIN\\r68.exe $b.a -o=$b.r -q" >/dev/null
	[ -s "$TMP/$b.r" ] || { echo "  $b: r68 kommt nicht durch"; bad=$((bad + 1)); continue; }
	for base in 0 1000; do
		wrun "M:\\DOS\\BIN\\l68.exe -r=$base $b.r -O=l\\$b$base" >/dev/null
		if [ ! -s "$TMP/l/$b$base" ]; then
			echo "  $b -r=$base: l68 kommt nicht durch"
			bad=$((bad + 1)); continue
		fi
		if ! "$QL68" "-r=$base" "$TMP/$b.r" "-O=$TMP/q/$b$base" > "$TMP/$b.msg" 2>&1; then
			echo "  $(printf '%-8s' "$b -r=$base") ql68 bricht ab:"
			sed 's/^/      /' "$TMP/$b.msg" | head -2
			bad=$((bad + 1)); continue
		fi
		if cmp -s "$TMP/l/$b$base" "$TMP/q/$b$base"; then
			ok=$((ok + 1))
		else
			echo "  $(printf '%-14s' "$b -r=$base") ABWEICHUNG"
			echo "      l68 : $(xxd -p "$TMP/l/$b$base" | tr -d '\n')"
			echo "      ql68: $(xxd -p "$TMP/q/$b$base" | tr -d '\n')"
			bad=$((bad + 1))
		fi
	done
done
echo
echo "  $ok gleich, $bad abweichend"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
