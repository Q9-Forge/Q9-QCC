#!/bin/bash
# Syntax-checks the tool sources against the headers in Q9DEFS/include.
# usage: chk_headers.sh <dir with the *.c consumer sources> [<qcpp include dir>]
# (host compiler in syntax-only mode; K&R heads are accepted)
HERE=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:?consumer source dir}
QI=${2:-$HERE/../Q9-FRONTEND-C/q9-qcpp/include}
ok=0; bad=0
for f in $(find "$SRC" -name '*.c' ! -name '*_server.c'); do
  if clang -fsyntax-only -std=gnu89 -w -nostdinc -I"$HERE/include" -I"$QI" "$f" >/dev/null 2>&1; then ok=$((ok+1)); else bad=$((bad+1)); echo "FEHLER: $f"; fi
done
echo "ok=$ok bad=$bad"
