#!/usr/bin/env bash
# Differenztest fuer die Sprungtabelle (-a).
#
# Geprueft wird dort, wo l68 selbst EXAKT rechnet: bei gewoehnlichen
# ROF-Eingaben ist "guess == Actual" (an 1, 2, 3 und 5 fernen Aufrufen
# nachgemessen). Nur mit Bibliotheken schaetzt l68 hoch und laesst
# ueberzaehlige Eintraege als "4ef9 00000000" stehen -- dort ist
# Byteidentitaet weder erreichbar noch erstrebenswert, s. README.
#
# tools/genjt.py erzeugt die Proben: der erste psect ruft N Ziele im
# zweiten, dazwischen liegen 20.000 Fuellworte -- damit reicht die
# 16-Bit-Reichweite von bsr nicht mehr.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QL68="$PWD/build/ql68"
[ -x "$QL68" ] || { echo "FEHLER: build/ql68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[ -d "$MWOS/OS9" ] || { echo "uebersprungen: $MWOS fehlt"; exit 0; }

TMP="${KEEP:-$(mktemp -d /tmp/ql68-jt.XXXXXX)}"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all

echo "=== ql68 gegen l68, Sprungtabelle (-a) ==="
ok=0
bad=0
for n in 1 2 3 5; do
	W="$TMP/n$n"
	python3 tools/genjt.py "$W" "$n" 20000 >/dev/null
	mkdir -p "$W/l" "$W/q"
	TW="Z:$(printf '%s' "$W" | sed 's#/#\\#g')"
	wrun() { arch -x86_64 "$WINE_BIN" cmd /c \
		"Z: && cd ${TW#*:} && set PATH=M:\\DOS\\BIN;%PATH% && $*" 2>&1 | tr -d '\r'; }

	wrun "M:\\DOS\\BIN\\r68.exe ja.a -o=ja.r -q" >/dev/null
	wrun "M:\\DOS\\BIN\\r68.exe jb.a -o=jb.r -q" >/dev/null
	[ -s "$W/ja.r" ] && [ -s "$W/jb.r" ] ||
		{ echo "  N=$n: r68 kommt nicht durch"; bad=$((bad + 1)); continue; }

	# Der MODULNAME kommt aus -O=: beide Seiten schreiben denselben
	# Basisnamen in verschiedene Verzeichnisse.
	wrun "M:\\DOS\\BIN\\l68.exe -a ja.r jb.r -O=l\\jmod" >/dev/null
	[ -s "$W/l/jmod" ] ||
		{ echo "  N=$n: l68 kommt nicht durch"; bad=$((bad + 1)); continue; }

	if ! "$QL68" -a "$W/ja.r" "$W/jb.r" "-O=$W/q/jmod" > "$W/msg" 2>&1; then
		echo "  N=$n: ql68 bricht ab:"
		sed 's/^/      /' "$W/msg" | head -2
		bad=$((bad + 1)); continue
	fi
	if cmp -s "$W/l/jmod" "$W/q/jmod"; then
		echo "  $(printf '%-16s' "N=$n ($n Eintraege)") gleich ($(wc -c < "$W/l/jmod" | tr -d ' ') Byte)"
		ok=$((ok + 1))
	else
		echo "  N=$n ABWEICHUNG"
		python3 tools/modcmp.py "$W/l/jmod" "$W/q/jmod" 2>/dev/null |
			sed 's/^/      /' | head -6
		bad=$((bad + 1))
	fi
done

# Ohne -a MUSS ql68 abbrechen -- ein stilles falsches Displacement waere
# der schlimmere Fehler. l68 verhaelt sich genauso.
W="$TMP/n1"
if "$QL68" "$W/ja.r" "$W/jb.r" "-O=$W/q/ohne" >/dev/null 2>&1; then
	echo "  ohne -a: ql68 bindet KLAGLOS -- das waere falscher Code"
	bad=$((bad + 1))
else
	echo "  ohne -a: bricht ab, wie l68"
	ok=$((ok + 1))
fi

echo
echo "  $ok gleich, $bad abweichend"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
