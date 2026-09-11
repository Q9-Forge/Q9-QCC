#!/usr/bin/env bash
# Differential test on MWOS SDK DRIVER SOURCES -- the most demanding
# material: includes, macros, conditional assembly, branch sizing, and
# Ausdrücke mit mehreren externen Anteilen, alles in einer Datei.
#
# Assemble as the SDK makefiles do: from a
# PORT-Verzeichnis heraus (dort liegt "defsfile", das "use defsfile" holt),
# mit den Schaltern -qb -u=. -u=<DEFS> -u=<MACROS>.
#
#   ./test/mwos.sh                 -- alle SCF-Treiber des Beispielports
#   ./test/mwos.sh datei.a ...     -- bestimmte Quellen
#
# UEXTRA adds further search directories (space-separated).
# Der ROM-Code braucht das: ROM_CBOOT/sysinit.a holt sich "systype.d" aus
# dem WURZELverzeichnis des Ports, nicht aus dem eigenen -- ohne den
# zusaetzlichen -u faellt r68 auf sein eingebautes \mwos\OS9\SRC\DEFS
# zurueck und bricht ab.
#
#   UEXTRA=$MWOS/OS9/68030/PORTS/Q9 PORTDIR=... ./test/mwos.sh .../sysinit.a
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QR68="$PWD/build/qr68k"
TOOLS="$PWD/tools"
[ -x "$QR68" ] || { echo "FEHLER: build/qr68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS_HOST:=/Volumes/SSD1TB/projects/MWOS}"
MWOS="$MWOS_HOST"
: "${PORTDIR:=$MWOS/OS9/68060/PORTS/MVME172/SCF}"
: "${DRVDIR:=$MWOS/OS9/SRC/IO/SCF/DRVR}"

if [ ! -f "$PORTDIR/defsfile" ] || [ ! -d "$DRVDIR" ]; then
	echo "uebersprungen: $PORTDIR/defsfile oder $DRVDIR fehlt"
	exit 0
fi

TMP="${KEEP:-$(mktemp -d /tmp/qr68-mwos.XXXXXX)}"
mkdir -p "$TMP"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

# os9-toolchain.sh redirects MWOS to the WINE path; save the Unix path first,
# vorher gerettet werden, sonst sucht qr68 seine Includes unter "M:\...".
MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all

files=("$@")
if [ ${#files[@]} -eq 0 ]; then
	for f in "$DRVDIR"/*.a; do
		# Skip the SDK "(2)" duplicates.
		case "$f" in *"("*) continue;; esac
		files+=("$f")
	done
fi

# Paths for Wine. Everything below $MWOS is mapped to drive M:
# -- mit einem "Z:"-Pfad findet r68 sein Suchverzeichnis NICHT (es faellt
# dann auf sein eingebautes \mwos\OS9\SRC\DEFS zurueck).
winpath() {
	case "$1" in
	"$MWOS"/*) printf 'M:%s' "$(printf '%s' "${1#$MWOS}" | sed 's#/#\\#g')";;
	*)         printf 'Z:%s' "$(printf '%s' "$1" | sed 's#/#\\#g')";;
	esac
}

PORTWIN="$(winpath "$PORTDIR")"
UDEFS="$(winpath "$MWOS/OS9/SRC/DEFS")"
UMACS="$(winpath "$MWOS/OS9/SRC/MACROS")"
TMPWIN="$(winpath "$TMP")"

# Additional search directories, once for r68 (Wine) and once for qr68 (Unix).
RXTRA=""
QXTRA=()
for d in ${UEXTRA:-}; do
	RXTRA="$RXTRA -u=$(winpath "$d")"
	QXTRA+=("-u=$d")
done

echo "=== MWOS-Treiber gegen r68 (${#files[@]} Quellen) ==="
ok=0
bad=0
skip=0
for f in "${files[@]}"; do
	base="$(basename "$f" .a)"
	src="$(winpath "$f")"
	arch -x86_64 "$WINE_BIN" cmd /c \
		"${PORTWIN%%:*}: && cd ${PORTWIN#*:} && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe $src -o=$TMPWIN\\$base.r -qb -u=. -u=$UDEFS -u=$UMACS$RXTRA -aNODATAPORT" \
		> "$TMP/$base.r68" 2>&1
	if [ ! -f "$TMP/$base.r" ]; then
		echo "  $(printf '%-20s' "$base") r68 kommt selbst nicht durch -- uebersprungen"
		skip=$((skip + 1))
		continue
	fi
	stamp="$(python3 -c 'import sys; d=open(sys.argv[1],"rb").read(); print(",".join(str(b) for b in d[12:18]))' "$TMP/$base.r")"
	if [ -z "$stamp" ]; then
		# r68 created the file but wrote nothing; it did not assemble it.
		echo "  $(printf '%-20s' "$base") r68 kommt selbst nicht durch: $(grep -m1 -o 'fatal:.*' "$TMP/$base.r68" | head -1)"
		skip=$((skip + 1))
		continue
	fi
	if ! (cd "$PORTDIR" && "$QR68" -qb -u=. "-u=$MWOS/OS9/SRC/DEFS" \
		"-u=$MWOS/OS9/SRC/MACROS" \
		${QXTRA[@]+"${QXTRA[@]}"} -aNODATAPORT "-fdate=$stamp" \
		"$f" "$TMP/$base.q") > "$TMP/$base.msg" 2>&1; then
		echo "  $(printf '%-20s' "$base") qr68 bricht ab:"
		sed 's/^/      /' "$TMP/$base.msg" | head -2
		bad=$((bad + 1))
		continue
	fi
	if python3 "$TOOLS/rofcmp.py" "$base:$TMP/$base.r:$TMP/$base.q" \
		| grep -q "gleich ("; then
		ok=$((ok + 1))
	else
		echo "  $(printf '%-20s' "$base") ABWEICHUNG:"
		python3 "$TOOLS/rofhunks.py" "$TMP/$base.r" "$TMP/$base.q" \
			"${HUNKS:-4}" | sed 's/^/    /'
		bad=$((bad + 1))
	fi
done

echo
echo "  $ok gleich, $bad abweichend, $skip uebersprungen"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
