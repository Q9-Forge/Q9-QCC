#!/usr/bin/env bash
# Compare "use" against r68; a dedicated script is needed because both sides
# require the same directory tree and -u= options.
#
# Also verify the search rule measured against r68:
#   "use datei" und "use \"datei\"" -> relativ zum ARBEITSverzeichnis,
#   "use <datei>"                   -> nur die -u=-Verzeichnisse.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QR68="$PWD/build/qr68k"
TOOLS="$PWD/tools"
[ -x "$QR68" ] || { echo "FEHLER: build/qr68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS_HOST:=/Volumes/SSD1TB/projects/MWOS}"
MWOS="$MWOS_HOST"
TMP="$(mktemp -d /tmp/qr68-use.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all

mkdir -p "$TMP/inc"
cat > "$TMP/gleichauf.a" <<'EOF'
WERT     equ     $20
tabelle: dc.l    WERT,WERT*2
EOF
cat > "$TMP/inc/tief.a" <<'EOF'
* Eine eingeschlossene Datei, die selbst einschliesst.
         use     gleichauf.a
ZWEITER  equ     WERT+1
EOF
cat > "$TMP/main.a" <<'EOF'
         psect   usetest,0,0,1,0,0
         use     <tief.a>
         use     "leer.a"
start:   move.l  #WERT,d0
         move.l  #ZWEITER,d1
         lea     tabelle(pc),a0
         bsr     woanders
         rts
         use     woanders.a
         ends
EOF
cat > "$TMP/leer.a" <<'EOF'
* Nur ein Kommentar -- prueft, dass eine leere Datei den Stapel nicht stoert.
EOF
cat > "$TMP/woanders.a" <<'EOF'
woanders: moveq  #1,d0
         rts
EOF

TMPWIN="$(printf '%s' "$TMP" | sed 's#/#\\#g')"
arch -x86_64 "$WINE_BIN" cmd /c \
	"Z: && cd $TMPWIN && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe main.a -o=main.r -l -u=inc" \
	> "$TMP/main.lst" 2>&1
tr -d '\r' < "$TMP/main.lst" > "$TMP/main.lst2" && mv "$TMP/main.lst2" "$TMP/main.lst"
if grep -q "^\*\*\* error" "$TMP/main.lst" || [ ! -f "$TMP/main.r" ]; then
	echo "FEHLER: r68 kommt mit der Probe nicht durch:"
	sed 's/^/    /' "$TMP/main.lst" | head -12
	exit 2
fi

stamp="$(python3 - "$TMP/main.r" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
print(",".join(str(b) for b in d[12:18]))
PY
)"

echo "=== use gegen r68 ==="
if ! (cd "$TMP" && "$QR68" "-fdate=$stamp" -u=inc main.a main.q) > "$TMP/msg" 2>&1; then
	echo "  qr68 bricht ab:"
	sed 's/^/    /' "$TMP/msg" | head -5
	exit 1
fi
python3 "$TOOLS/rofcmp.py" "use:$TMP/main.r:$TMP/main.q"
