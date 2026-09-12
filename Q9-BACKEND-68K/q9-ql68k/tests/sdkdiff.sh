#!/usr/bin/env bash
# Differenztest ueber den GANZEN SDK-Korpus -- mit den Aufrufen, die das
# SDK selbst benutzt, statt sie zu raten.
#
# Derselbe Hebel wie bei Q9-qr68/test/sdkdiff.sh:
#
#     MWMAKEOPTS=-u  os9make -nn -u
#
# DRUCKT die Kommandos, statt sie auszufuehren, und steigt in die
# Untermakes ab. Anders als beim Assembler braucht der Binder aber seine
# EINGABEN: die .r-Dateien entstehen erst durch die r68-Zeilen desselben
# Trockenlaufs. Das Skript faehrt deshalb beide Sorten Zeilen der Reihe
# nach -- erst r68, dann l68 -- und biegt alle Ausgaben in ein
# Temporaerverzeichnis um. In den SDK-Baum wird NICHTS geschrieben.
#
#   ./test/sdkdiff.sh                -- alle Verzeichnisse mit einem makefile
#   ./test/sdkdiff.sh <verz> ...     -- nur diese
#
#   KEEP=<verz>   Zwischendateien behalten
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QL68="$PWD/build/ql68"
TOOLS="$PWD/tools"
[ -x "$QL68" ] || { echo "FEHLER: build/ql68 fehlt -- vorher 'make'"; exit 2; }

: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[ -d "$MWOS/OS9" ] || { echo "uebersprungen: $MWOS/OS9 fehlt"; exit 0; }

TMP="${KEEP:-$(mktemp -d /tmp/ql68-sdk.XXXXXX)}"
mkdir -p "$TMP"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	{ echo "FEHLER: OS-9-Toolchain nicht ladbar"; exit 2; }
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all

winpath() {
	case "$1" in
	"$MWOS"/*) printf 'M:%s' "$(printf '%s' "${1#$MWOS}" | sed 's#/#\\#g')";;
	*)         printf 'Z:%s' "$(printf '%s' "$1" | sed 's#/#\\#g')";;
	esac
}
unwin() { printf '%s' "$1" | sed 's#\\#/#g'; }
TMPWIN="$(winpath "$TMP")"

# Ein Pfad aus dem Makefile, der in den BAUM schreiben wuerde, wird in das
# Temporaerverzeichnis umgebogen -- und zwar unter seinem eigenen Namen,
# damit die l68-Zeile ihre Eingaben wiederfindet.
# JE VERZEICHNIS ein eigener Unterordner -- sonst bilden alle Ports ihr
# "RELS\" auf dasselbe Ziel ab und ueberschreiben sich gegenseitig. Fuer
# den Lauf selbst waere das harmlos (je Verzeichnis erst r68, dann l68),
# aber die Zwischendateien waeren hinterher unbrauchbar.
mapout() {                      # $1 = Windows-Pfad aus dem Makefile
	printf '%s\\w\\%s\\%s' "$TMPWIN" "$ndirs" "$(printf '%s' "$1" | sed 's#^\.\.\\##g; s#^\.\\##')"
}
mapout_unix() {
	printf '%s/w/%s/%s' "$TMP" "$ndirs" "$(unwin "$(printf '%s' "$1" | sed 's#^\.\.\\##g; s#^\.\\##')")"
}

dirs=("$@")
if [ ${#dirs[@]} -eq 0 ]; then
	while IFS= read -r mf; do
		case "$mf" in *"("*) continue;; esac
		dirs+=("$(dirname "$mf")")
	done < <(find "$MWOS/OS9" -name makefile -not -path "*/(*" | sort)
fi

echo "=== SDK-Differenztest ql68 gegen l68 (${#dirs[@]} Verzeichnisse) ==="
ok=0; bad=0; skip=0; dup=0; ndirs=0; nl=0
: > "$TMP/abweichend"
: > "$TMP/uebersprungen"
: > "$TMP/doppelt"

for d in "${dirs[@]}"; do
	[ -f "$d/makefile" ] || continue
	ndirs=$((ndirs + 1))
	dwin="$(winpath "$d")"
	arch -x86_64 "$WINE_BIN" cmd /c \
		"${dwin%%:*}: && cd ${dwin#*:} && set PATH=M:\\DOS\\BIN;%PATH% && set MWMAKEOPTS=-u && M:\\DOS\\BIN\\os9make.exe -nn -u" \
		2>/dev/null | tr -d '\r' | grep -E '^[[:space:]]*(r68|l68)([[:space:]]|$)' > "$TMP/cmds" || true
	grep -q '^[[:space:]]*l68' "$TMP/cmds" || continue

	while IFS= read -r line; do
		# shellcheck disable=SC2086
		set -- $line
		tool="$1"; shift

		if [ "$tool" = "r68" ]; then
			# Die Eingaben des Binders erzeugen -- Ausgabe umgebogen.
			args=(); out=""
			for a in "$@"; do
				case "$a" in
				-o=*|-O=*) out="${a#*=}";;
				*)         args+=("$a");;
				esac
			done
			[ -n "$out" ] || continue
			mkdir -p "$(dirname "$(mapout_unix "$out")")"
			arch -x86_64 "$WINE_BIN" cmd /c \
				"${dwin%%:*}: && cd ${dwin#*:} && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\r68.exe ${args[*]+${args[*]}} -o=$(mapout "$out")" \
				>/dev/null 2>&1
			continue
		fi

		nl=$((nl + 1))
		# l68: Eingaben, die in RELS liegen, zeigen auf unsere Kopien.
		largs=(); qargs=(); out=""; bad_line=""
		for a in "$@"; do
			# Ab einer Shell-Umleitung gehoert nichts mehr zum
			# Aufruf -- manche Makefiles schreiben die Modulkarte
			# so in eine Datei.
			case "$a" in
			">"*) break;;
			esac
			case "$a" in
			-o=*|-O=*) out="${a#*=}";;
			-r=*)      bad_line="rohe Ausgabe (-r=)";;
			-g)        ;;   # STB-Modul -- aendert das Modul nicht
			-*)        largs+=("$a"); qargs+=("$(unwin "$a")");;
			*)
				if [ -f "$(mapout_unix "$a")" ]; then
					largs+=("$(mapout "$a")")
					qargs+=("$(mapout_unix "$a")")
				else
					largs+=("$a")
					qargs+=("$(unwin "$a")")
				fi;;
			esac
		done
		if [ -n "$bad_line" ]; then
			echo "  - $bad_line: $line" >> "$TMP/uebersprungen"
			skip=$((skip + 1)); continue
		fi
		[ -n "$out" ] || { skip=$((skip + 1)); continue; }
		# Der MODULNAME kommt aus dem Ausgabenamen. Beide Seiten
		# muessen deshalb unter DEMSELBEN Namen schreiben -- ein
		# angehaengter Zaehler wuerde den Namen und damit den CRC
		# veraendern. Stattdessen bekommt jedes Verzeichnis einen
		# eigenen Unterordner.
		base="$(basename "$(unwin "$out")")"
		tag="$ndirs/$base"
		mkdir -p "$TMP/l/$ndirs" "$TMP/q/$ndirs"
		rm -f "$TMP/l/$tag" "$TMP/q/$tag"
		arch -x86_64 "$WINE_BIN" cmd /c \
			"${dwin%%:*}: && cd ${dwin#*:} && set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe ${largs[*]+${largs[*]}} -O=$TMPWIN\\l\\$ndirs\\$base" \
			> "$TMP/l/$ndirs/$base.l68" 2>&1
		if [ ! -s "$TMP/l/$tag" ]; then
			# Fehlt die Eingabe, stammt die Zeile aus einem
			# Untermake und wurde mit DESSEN Arbeitsverzeichnis
			# gedruckt -- jenes Verzeichnis wird eigens angefahren.
			missing=0
			for a in "${qargs[@]}"; do
				case "$a" in -*) continue;; esac
				case "$a" in *.r|*.l) [ -f "$a" ] || [ -f "$d/$a" ] || missing=1;; esac
			done
			if [ "$missing" = 1 ]; then
				echo "  = $base: aus einem Untermake, dort selbst geprueft ($d)" >> "$TMP/doppelt"
				dup=$((dup + 1))
			else
				echo "  - $base: l68 kommt selbst nicht durch ($d)" >> "$TMP/uebersprungen"
				skip=$((skip + 1))
			fi
			continue
		fi
		if ! (cd "$d" && "$QL68" ${qargs[@]+"${qargs[@]}"} \
			"-O=$TMP/q/$tag") > "$TMP/q/$ndirs/$base.msg" 2>&1; then
			{ echo "  x $base ($d)"; sed 's/^/      /' "$TMP/q/$ndirs/$base.msg" | head -2; } \
				>> "$TMP/abweichend"
			bad=$((bad + 1)); continue
		fi
		if cmp -s "$TMP/l/$tag" "$TMP/q/$tag"; then
			ok=$((ok + 1))
		else
			{ echo "  x $base ($d) ABWEICHUNG:"
			  python3 "$TOOLS/modcmp.py" "$TMP/l/$tag" "$TMP/q/$tag" \
				| sed 's/^/      /' | head -6; } >> "$TMP/abweichend"
			bad=$((bad + 1))
		fi
	done < "$TMP/cmds"
	printf '\r  %d Verzeichnisse, %d l68-Aufrufe: %d gleich, %d abweichend, %d uebersprungen, %d doppelt ' \
		"$ndirs" "$nl" "$ok" "$bad" "$skip" "$dup"
done
echo

if [ -s "$TMP/abweichend" ]; then
	echo
	echo "=== Abweichungen ==="
	cat "$TMP/abweichend"
fi
echo
echo "  $ok gleich, $bad abweichend, $skip uebersprungen, $dup doppelt (aus $ndirs Verzeichnissen)"
echo "  (\"doppelt\" = Zeilen aus einem Untermake, dessen Verzeichnis eigens"
echo "   angefahren wird; \"uebersprungen\" ist ueberwiegend -r= (rohe Ausgabe).)"
[ -n "${KEEP:-}" ] && echo "  Zwischendateien: $TMP"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
