#!/usr/bin/env bash
# Differential test on REAL material: sources produced by QCC's backend.
#
# Built-in probes in difftest.sh check individual cases, and test/insn.a checks
# the instruction table. This runs what actually arrives in the chain
# qir_68k -> qr68 -> l68: complete modules, up to 146,000 lines. This is the
# test that matters.
#
#   ./test/backend.sh              -- all discovered modules
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

# The small modules and one large module. Larger than SRC_MAX is not supported
# yet: the 18 MB sources require a streaming reader.
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

exec ./tests/difftest.sh "${files[@]}"
