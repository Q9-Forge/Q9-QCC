#!/usr/bin/env bash
# Zaehlt, WELCHE l68-Schalter der SDK-Korpus benutzt -- derselbe
# Trockenlauf wie test/sdkdiff.sh, aber ohne zu binden.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 || exit 2
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all

winpath() {
	case "$1" in
	"$MWOS"/*) printf 'M:%s' "$(printf '%s' "${1#$MWOS}" | sed 's#/#\\#g')";;
	*)         printf 'Z:%s' "$(printf '%s' "$1" | sed 's#/#\\#g')";;
	esac
}

TMP=$(mktemp -d /tmp/ql68-cens.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
: > "$TMP/opts"
: > "$TMP/lines"

n=0
while IFS= read -r mf; do
	case "$mf" in *"("*) continue;; esac
	d="$(dirname "$mf")"
	dwin="$(winpath "$d")"
	n=$((n + 1))
	printf '\r  %d Verzeichnisse' "$n" >&2
	arch -x86_64 "$WINE_BIN" cmd /c \
		"${dwin%%:*}: && cd ${dwin#*:} && set PATH=M:\\DOS\\BIN;%PATH% && set MWMAKEOPTS=-u && M:\\DOS\\BIN\\os9make.exe -nn -u" \
		2>/dev/null | tr -d '\r' | grep -E '^[[:space:]]*l68([[:space:]]|$)' >> "$TMP/lines" || true
done < <(find "$MWOS/OS9" -name makefile -not -path "*/(*" | sort)
echo >&2

# Jeden Schalter auf seine Grundform bringen: -x=wert -> "-x=", -abc -> -a -b -c
while IFS= read -r line; do
	# shellcheck disable=SC2086
	set -- $line
	shift
	for a in "$@"; do
		case "$a" in
		">"*) break;;
		-*=*) printf '%s=\n' "${a%%=*}" >> "$TMP/opts";;
		-mt*) echo "-mt<x>" >> "$TMP/opts";;
		-?)   echo "$a" >> "$TMP/opts";;
		-*)   # Buendel: jeden Buchstaben einzeln zaehlen
		      s="${a#-}"
		      i=0
		      while [ $i -lt ${#s} ]; do
			      printf -- '-%s (aus Buendel %s)\n' "${s:$i:1}" "$a" >> "$TMP/opts"
			      i=$((i + 1))
		      done;;
		esac
	done
done < "$TMP/lines"

echo "=== l68-Aufrufe im Korpus: $(wc -l < "$TMP/lines" | tr -d ' ') ==="
echo
echo "=== Benutzte Schalter, nach Haeufigkeit ==="
sort "$TMP/opts" | uniq -c | sort -rn
