#!/usr/bin/env bash
# Differenztest ueber den GANZEN Korpus -- mit den Aufrufen, die das SDK
# selbst benutzt, statt sie je Port zu raten.
#
# Der Hebel: "os9make -nn -u" DRUCKT die Kommandos, statt sie auszufuehren,
# und steigt dabei in die Untermakes ab ("-nn" = wie "-n", aber Verzeichnisse
# wechseln und Untermakes ebenfalls mit "-nn" aufrufen). MWMAKEOPTS=-u sorgt
# dafuer, dass auch die Untermakes alles neu bauen wollen -- sonst schweigen
# sie, weil die .r-Dateien im Baum schon liegen. Damit steht jede
# r68-Kommandozeile mit ihren echten Schaltern, Suchverzeichnissen und
# -a-Definitionen fest.
#
# In den SDK-Baum wird dabei NICHTS geschrieben: os9make fuehrt nichts aus,
# und die -o=-Angabe wird auf ein Temporaerverzeichnis umgebogen.
#
# Das ist der Unterschied zu test/mwos.sh, das PORTDIR/DRVDIR/UEXTRA von Hand
# gesetzt bekommt: hier kommt die Konfiguration aus dem Makefile.
#
#   ./test/sdkdiff.sh                -- alle Verzeichnisse mit einem makefile
#   ./test/sdkdiff.sh <verz> ...     -- nur diese
#
#   KEEP=<verz>   Zwischendateien behalten
#   HUNKS=<n>     wie viele Unterschiede je Abweichung gezeigt werden
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QR68="$PWD/build/qr68"
TOOLS="$PWD/tools"
[ -x "$QR68" ] || { echo "FEHLER: build/qr68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[ -d "$MWOS/OS9" ] || { echo "uebersprungen: $MWOS/OS9 fehlt"; exit 0; }

TMP="${KEEP:-$(mktemp -d /tmp/qr68-sdk.XXXXXX)}"
mkdir -p "$TMP"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

# os9-toolchain.sh biegt MWOS auf den WINE-Pfad um -- der Unix-Pfad muss
# vorher gerettet werden.
MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all

# Alles unterhalb von $MWOS liegt unter Wine auf dem Laufwerk M:. Mit einem
# "Z:"-Pfad findet r68 sein Suchverzeichnis NICHT (es faellt dann auf sein
# eingebautes \mwos\OS9\SRC\DEFS zurueck).
winpath() {
	case "$1" in
	"$MWOS"/*) printf 'M:%s' "$(printf '%s' "${1#$MWOS}" | sed 's#/#\\#g')";;
	*)         printf 'Z:%s' "$(printf '%s' "$1" | sed 's#/#\\#g')";;
	esac
}
# Windows-Pfad aus dem Makefile -> Unix, fuer qr68.
unwin() { printf '%s' "$1" | sed 's#\\#/#g'; }

dirs=("$@")
if [ ${#dirs[@]} -eq 0 ]; then
	while IFS= read -r mf; do
		case "$mf" in *"("*) continue;; esac
		dirs+=("$(dirname "$mf")")
	done < <(find "$MWOS/OS9" -name makefile -not -path "*/(*" | sort)
	# Zwei Verzeichnisse des SDK haben nur "*.make" und kein "makefile"
	# daneben (SRC/SYSMODS/GCLOCK und PORTS/common/RBF/cfide). Ohne sie
	# fiele die halbe Uhrengruppe unter den Tisch.
	while IFS= read -r mk; do
		md="$(dirname "$mk")"
		[ -f "$md/makefile" ] && continue
		case " ${dirs[*]} " in *" $md "*) continue;; esac
		dirs+=("$md")
	done < <(find "$MWOS/OS9" -name "*.make" -not -path "*/(*" \
		-not -name "*(*" | sort)
fi

echo "=== SDK-Differenztest, Aufrufe aus den Makefiles (${#dirs[@]} Verzeichnisse) ==="

ok=0; bad=0; skip=0; dup=0; ndirs=0; ncmd=0
: > "$TMP/abweichend"
: > "$TMP/uebersprungen"
: > "$TMP/doppelt"
: > "$TMP/geprueft"

for d in "${dirs[@]}"; do
	# Ohne "makefile" die einzelnen "*.make" fahren.
	mkfiles=("")
	if [ ! -f "$d/makefile" ]; then
		mkfiles=()
		for mk in "$d"/*.make; do
			[ -f "$mk" ] || continue
			case "$mk" in *"("*) continue;; esac
			mkfiles+=("-f=$(basename "$mk")")
		done
		[ ${#mkfiles[@]} -gt 0 ] || continue
	fi
	ndirs=$((ndirs + 1))
	dwin="$(winpath "$d")"
	# Die Kommandos holen. Fehler des Trockenlaufs sind egal -- was an
	# r68-Zeilen herauskommt, zaehlt.
	: > "$TMP/cmds"
	for mkf in "${mkfiles[@]}"; do
		arch -x86_64 "$WINE_BIN" cmd /c \
			"${dwin%%:*}: && cd ${dwin#*:} && set PATH=M:\\DOS\\BIN;%PATH% && set MWMAKEOPTS=-u && M:\\DOS\\BIN\\os9make.exe -nn -u $mkf" \
			2>/dev/null | tr -d '\r' | grep -E '^[[:space:]]*r68([[:space:]]|$)' >> "$TMP/cmds" || true
	done
	[ -s "$TMP/cmds" ] || continue
	# Dieselbe Zeile kann aus mehreren "*.make" kommen.
	sort -u "$TMP/cmds" -o "$TMP/cmds"

	while IFS= read -r line; do
		ncmd=$((ncmd + 1))
		# shellcheck disable=SC2086
		set -- $line
		shift                       # das "r68" selbst
		rargs=(); qargs=(); src=""; out=""
		for a in "$@"; do
			case "$a" in
			-o=*|-O=*) out="${a#*=}";;
			-u=*)      rargs+=("$a"); qargs+=("-u=$(unwin "${a#-u=}")");;
			-*)        rargs+=("$a"); qargs+=("$a");;
			*)         if [ -z "$src" ]; then src="$a"; else src="KAPUTT"; fi;;
			esac
		done
		if [ -z "$src" ] || [ "$src" = "KAPUTT" ] || [ -z "$out" ]; then
			echo "  ? Zeile nicht auswertbar: $line" >> "$TMP/uebersprungen"
			skip=$((skip + 1)); continue
		fi
		base="$(basename "$(unwin "$out")" .r)"
		tag="$ncmd-$base"
		usrc="$(unwin "$src")"
		if [ ! -f "$d/$usrc" ]; then
			# "os9make -nn" steigt in die Untermakes ab und druckt
			# deren Kommandos MIT DEM ARBEITSVERZEICHNIS DES KINDES.
			# Die Pfade zeigen dann von hier aus ins Leere. Da jedes
			# Verzeichnis mit einem makefile ohnehin einzeln
			# angefahren wird, ist das keine Luecke, sondern eine
			# Doppelung -- nachgewiesen, indem die Quelle relativ zu
			# einem Unterverzeichnis gesucht wird.
			found=""
			for sub in "$d"/*/; do
				[ -f "$sub$usrc" ] && { found="$sub"; break; }
			done
			if [ -n "$found" ]; then
				echo "  = $base: aus einem Untermake ($found), dort selbst geprueft" \
					>> "$TMP/doppelt"
				dup=$((dup + 1))
			else
				echo "  ? Quelle nicht gefunden: $d/$usrc" >> "$TMP/uebersprungen"
				skip=$((skip + 1))
			fi
			continue
		fi

		# r68 -- mit den Original-Schaltern, nur die Ausgabe umgebogen.
		arch -x86_64 "$WINE_BIN" cmd /c \
			"${dwin%%:*}: && cd ${dwin#*:} && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe ${rargs[*]+${rargs[*]}} $src -o=$(winpath "$TMP")\\$tag.r" \
			> "$TMP/$tag.r68" 2>&1
		if [ ! -s "$TMP/$tag.r" ]; then
			echo "  - $base: r68 kommt selbst nicht durch ($d)" >> "$TMP/uebersprungen"
			skip=$((skip + 1)); continue
		fi
		stamp="$(python3 -c 'import sys; d=open(sys.argv[1],"rb").read(); print(",".join(str(b) for b in d[12:18]))' "$TMP/$tag.r")"

		if ! (cd "$d" && "$QR68" ${qargs[@]+"${qargs[@]}"} "-fdate=$stamp" \
			"$usrc" "$TMP/$tag.q") > "$TMP/$tag.msg" 2>&1; then
			{ echo "  x $base ($d)"; sed 's/^/      /' "$TMP/$tag.msg" | head -2; } \
				>> "$TMP/abweichend"
			bad=$((bad + 1)); continue
		fi
		if python3 "$TOOLS/rofcmp.py" "$base:$TMP/$tag.r:$TMP/$tag.q" | grep -q "gleich ("; then
			ok=$((ok + 1))
			# EIN Aufruf ist Quelle PLUS Schalter -- dieselbe Quelle
			# mit -m3 und -m4 sind zwei verschiedene Tests. Daneben
			# wird mitgeschrieben, welche QUELLDATEIEN abgedeckt
			# sind; die Pfade werden am Ende normalisiert.
			printf '%s\n' "$d/$usrc" >> "$TMP/geprueft"
		else
			{ echo "  x $base ($d) ABWEICHUNG:"
			  python3 "$TOOLS/rofhunks.py" "$TMP/$tag.r" "$TMP/$tag.q" \
				"${HUNKS:-3}" | sed 's/^/      /'; } >> "$TMP/abweichend"
			bad=$((bad + 1))
		fi
	done < "$TMP/cmds"
	printf '\r  %d Verzeichnisse, %d Aufrufe: %d gleich, %d abweichend, %d uebersprungen, %d doppelt ' \
		"$ndirs" "$ncmd" "$ok" "$bad" "$skip" "$dup"
done
echo

if [ -s "$TMP/abweichend" ]; then
	echo
	echo "=== Abweichungen ==="
	cat "$TMP/abweichend"
fi
echo
echo "  $ok gleich, $bad abweichend, $skip uebersprungen, $dup doppelt (aus $ndirs Verzeichnissen)"
nsrc=$(python3 - "$TMP/geprueft" <<'PYEOF'
import os, sys
print(len({os.path.realpath(l.strip()) for l in open(sys.argv[1]) if l.strip()}))
PYEOF
)
echo "  $nsrc verschiedene Quelldateien (ein Aufruf = Quelle plus Schalter)"
echo "  (\"doppelt\" = Kommandos, die os9make aus einem Untermake gedruckt hat;"
echo "   ihr Verzeichnis wird eigens angefahren, dort sind sie gezaehlt.)"
[ -n "${KEEP:-}" ] && echo "  Zwischendateien: $TMP"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
