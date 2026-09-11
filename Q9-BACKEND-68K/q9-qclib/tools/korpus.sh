#!/usr/bin/env bash
# Misst den ZIELKORPUS: welche Symbole muss qclib liefern?
#
# Nicht geschaetzt, sondern aus den ROFs der eigenen Kette gelesen. Wer
# spaeter Funktionen streicht oder hinzufuegt, faehrt das hier erneut --
# eine Liste im README veraltet, eine Messung nicht.
#
#   tools/korpus.sh [<verzeichnis mit den .r>]
#
# Ohne Argument wird die Kette frisch gebaut (Q9-qr68/tools/build_os9.sh).
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
: "${FORGE:=$(cd .. && pwd)}"

WORK="${1:-}"
if [ -z "$WORK" ]; then
	WORK=/tmp/qclib-korpus
	echo "== Kette bauen (nach $WORK) =="
	"$FORGE/Q9-qr68/tools/build_os9.sh" "$WORK" >/dev/null 2>&1 ||
		{ echo "FEHLER: build_os9.sh kam nicht durch"; exit 2; }
fi

shopt -s nullglob
rofs=("$WORK"/*.r)
[ ${#rofs[@]} -gt 0 ] || { echo "FEHLER: keine .r in $WORK"; exit 2; }

tmp="$(mktemp)"
trap 'rm -f "$tmp" "$tmp.u"' EXIT

echo "=== Externe Namen der eigenen ROFs ==="
for f in "${rofs[@]}"; do
	names="$(python3 tools/libdump.py "$f" --extern)"
	printf '  %-16s %s\n' "$(basename "$f")" "$(printf '%s' "$names" | tr '\n' ' ')"
	printf '%s\n' "$names" >> "$tmp"
done

# main definiert das Programm selbst, end setzt der Binder, und die
# _os_*-Namen sind OS-9-Systemaufrufe aus os_lib.l -- keiner davon ist
# Sache einer C-Bibliothek.
sort -u "$tmp" | grep -vxE 'main|end' > "$tmp.u"
aus_clib="$(grep -vxE '_os_.*' "$tmp.u" || true)"
aus_oslib="$(grep -xE '_os_.*' "$tmp.u" || true)"

echo
echo "=== Vereinigt, ohne was keine C-Bibliothek liefert ==="
echo "  aus clib:   $(printf '%s' "$aus_clib" | tr '\n' ' ')"
echo "  aus os_lib: $(printf '%s' "$aus_oslib" | tr '\n' ' ')"
echo
echo "  $(printf '%s\n' "$aus_clib" | grep -c .) Symbole muss qclib liefern."
