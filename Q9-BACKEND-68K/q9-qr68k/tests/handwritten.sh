#!/usr/bin/env bash
# Differential test on HANDWRITTEN material: assembler sources of the
# Q9-OS-Kernels. Anders als die Backend-Ausgabe nutzen die den vollen
# Sprachumfang -- movem mit Registerlisten, Bitbefehle, ccr/sr, exg, movec,
# Kommentare ohne Semikolon hinter operandenlosen Befehlen.
#
# MWOS driver sources are not included yet: they require expressions with
# MEHREREN verschiebbaren Anteilen ("PD_PAR-PD_OPT+M$DTyp(a1)", alle drei
# extern), was qr68 noch nicht kann.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
: "${Q9OS:=/Volumes/SSD1TB/projects/Q9-Forge/Q9-OS}"

if [ ! -d "$Q9OS/src/kernel" ]; then
	echo "uebersgesprungen: $Q9OS/src/kernel gibt es nicht (Q9OS setzen)"
	exit 0
fi

files=()
for f in "$Q9OS"/src/kernel/*.a; do
	[ -f "$f" ] || continue
	files+=("$f")
done

if [ ${#files[@]} -eq 0 ]; then
	echo "uebersprungen: keine .a in $Q9OS/src/kernel"
	exit 0
fi

exec ./test/insndiff.sh "${files[@]}"
