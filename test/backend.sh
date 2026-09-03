#!/usr/bin/env bash
# Differenztest an ECHTEM Material: den Quellen, die QCCs Backend erzeugt.
#
# Die eingebauten Proben in difftest.sh pruefen Einzelfaelle, test/insn.a die
# Befehlstabelle. Hier laeuft dagegen das, was in der Kette
# qcc_backend -> qr68 -> l68 tatsaechlich ankommt: ganze Module, bis zu
# 146.000 Zeilen. Das ist der Test, der zaehlt.
#
#   ./test/backend.sh              -- alle gefundenen Module
#   QCC_BUILD=... ./test/backend.sh
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
: "${QCC_BUILD:=/Volumes/SSD1TB/projects/Q9-Forge/Q9-QCC/build}"

if [ ! -d "$QCC_BUILD" ]; then
	echo "uebersprungen: $QCC_BUILD gibt es nicht (QCC_BUILD setzen)"
	exit 0
fi

TMP="$(mktemp -d /tmp/qr68-backend.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# Die kleinen Module und eines der grossen. Groesser als SRC_MAX geht
# (noch) nicht: die 18-MB-Quellen brauchen einen stroemenden Leser.
files=()
for f in "$QCC_BUILD"/os9_*.s68 "$QCC_BUILD"/qcc_fullprobe.s68; do
	[ -f "$f" ] || continue
	sz=$(wc -c < "$f")
	[ "$sz" -lt 4000000 ] || continue
	base="$(basename "$f" .s68)"
	cp "$f" "$TMP/$base.a"
	files+=("$TMP/$base.a")
done

if [ ${#files[@]} -eq 0 ]; then
	echo "uebersprungen: keine .s68 in $QCC_BUILD"
	exit 0
fi

exec ./test/difftest.sh "${files[@]}"
