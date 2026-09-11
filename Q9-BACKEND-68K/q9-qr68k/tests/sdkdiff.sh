#!/usr/bin/env bash
# Differential test over the ENTIRE corpus, using the invocations from the SDK
# actually uses, instead of guessing them per port.
#
# The mechanism: "os9make -nn -u" PRINTS commands instead of executing them,
# and descends into sub-makes ("-nn" = like "-n", but also changes directories
# and invokes sub-makes with "-nn"). MWMAKEOPTS=-u makes sub-makes rebuild
# everything; otherwise they remain silent because .r files already exist.
# This fixes every
# r68 command line with its actual flags, search directories, and
# -a definitions.
#
# NOTHING is written to the SDK tree: os9make executes nothing,
# and redirects the -o= output to a temporary directory.
#
# Unlike test/mwos.sh, which receives PORTDIR/DRVDIR/UEXTRA manually, this gets
# its configuration from the makefile.
#
#   ./test/sdkdiff.sh                -- all directories with a makefile
#   ./test/sdkdiff.sh <dir> ...      -- only these
#
#   KEEP=<dir>    keep temporary files
#   HUNKS=<n>     number of differences shown per mismatch
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QR68="$PWD/build/qr68k"
TOOLS="$PWD/tools"
[ -x "$QR68" ] || { echo "FEHLER: build/qr68k fehlt -- vorher 'make'"; exit 2; }

: "${MWOS_HOST:=/Volumes/SSD1TB/projects/MWOS}"
MWOS="$MWOS_HOST"
[ -d "$MWOS/OS9" ] || { echo "uebersprungen: $MWOS/OS9 fehlt"; exit 0; }

TMP="${KEEP:-$(mktemp -d /tmp/qr68-sdk.XXXXXX)}"
mkdir -p "$TMP"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

# os9-toolchain.sh redirects MWOS to the WINE path; save the Unix path first.
MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all

# Everything below $MWOS is mapped to drive M: under Wine. With a
# a "Z:" path does NOT let r68 find its search directory (it falls back to its
# built-in \mwos\OS9\SRC\DEFS).
winpath() {
	case "$1" in
	"$MWOS"/*) printf 'M:%s' "$(printf '%s' "${1#$MWOS}" | sed 's#/#\\#g')";;
	*)         printf 'Z:%s' "$(printf '%s' "$1" | sed 's#/#\\#g')";;
	esac
}
# Windows path from the makefile -> Unix, for qr68.
unwin() { printf '%s' "$1" | sed 's#\\#/#g'; }

dirs=("$@")
if [ ${#dirs[@]} -eq 0 ]; then
	while IFS= read -r mf; do
		case "$mf" in *"("*) continue;; esac
		dirs+=("$(dirname "$mf")")
	done < <(find "$MWOS/OS9" -name makefile -not -path "*/(*" | sort)
	# Two SDK directories have only "*.make" and no "makefile"
	# beside it (SRC/SYSMODS/GCLOCK and PORTS/common/RBF/cfide). Without them,
	# half of the clock group would be omitted.
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
	# If there is no "makefile", run the individual "*.make" files.
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
	# Collect commands. Dry-run errors do not matter; only extracted r68 lines
	# count.
	: > "$TMP/cmds"
	for mkf in "${mkfiles[@]}"; do
		arch -x86_64 "$WINE_BIN" cmd /c \
			"${dwin%%:*}: && cd ${dwin#*:} && set PATH=M:\\DOS\\BIN;%PATH% && set MWMAKEOPTS=-u && M:\\DOS\\BIN\\os9make.exe -nn -u $mkf" \
			2>/dev/null | tr -d '\r' | grep -E '^[[:space:]]*r68([[:space:]]|$)' >> "$TMP/cmds" || true
	done
	[ -s "$TMP/cmds" ] || continue
	# The same line may come from multiple "*.make" files.
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
			# "os9make -nn" descends into sub-makes and prints
			# their commands with THE CHILD'S WORKING DIRECTORY.
			# The paths are then invalid from here. Since each
			# directory with a makefile is processed separately,
			# this is duplication rather than a gap,
			# as proven by searching for the source relative to a subdirectory.
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

		# r68 -- original flags, with only the output redirected.
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
			# ONE invocation is source PLUS flags; the same source
			# with -m3 and -m4 represents two different tests. Also record
			# which SOURCE FILES are covered; paths are normalized at the end.
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
