#!/usr/bin/env bash
# long long AUS C auf echtem 68030 -- der Durchstich durch die ganze Kette.
#
# Uebersetzt wird mit der eigenen Kette (qcpp, qcir, qir68k, qr68k, ql68k).
# Anders als double gibt es fuer long long KEINE FPU-Abhaengigkeit -- die
# Arithmetik laeuft komplett in reinen move.l/add.l/sub.l-Ketten (68000-
# faehig) plus einer Schiebe-und-Addieren-Division/Multiplikation ohne
# MULU.L (68020+), s. project_qcc_c89_stand.md. Genau DAS ist hier zu
# pruefen: die VM (Python, beliebige Praezision) und der reine vasm-
# Syntax-Check in runtests.sh sehen QMUL/QDIV/QMOD nie tatsaechlich
# ausgefuehrt auf 32-Bit-Registern -- nur echte Hardware/der Emulator tut
# das.
#
# WARNUNG (23.09.2026, beim ersten Lauf dieses Skripts gefunden, s.
# project_qcc_c89_stand.md): ein KOMPLETT FRISCHER `make` in q9-qclib
# gegen den aktuellen qcc_p/qir68k liefert ein qclib.l, das SCHON EIN
# LEERES "int main(){ return 0; }" zum Absturz bringt (680x0 PMMU:
# Unhandled Table A mode 0) -- nachweislich UNABHAENGIG von long long
# (ein Programm ohne jeden qclib-Aufruf trifft es genauso) und
# UNABHAENGIG von q9_cstart.r (byteidentisch zur Produktion). Ursache
# noch NICHT gefunden, nur eingegrenzt: liegt in mindestens einer der
# acht C-uebersetzten qclib-Dateien selbst (str/ctype/mem/file/printf/
# extra_*), nicht im Linker (ql68k) oder Cstart. Dieses Skript benutzt
# deshalb bewusst KEIN frisch gebautes build/qclib.l, sondern verlangt
# eines, das bereits bekannt funktioniert (z.B. das der Produktions-
# Umgebung) -- s. LONGLONG68K_QCLIB unten.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd ../../.. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux/Q9-Flux-68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"
# s. WARNUNG oben: standardmaessig NICHT $REPO/build/qclib.l (ein frischer
# `make` hier kann derzeit ein qclib.l liefern, das schon ein leeres main()
# abstuerzen laesst) -- LONGLONG68K_QCLIB/LONGLONG68K_CSTART zeigen auf ein
# bekannt funktionierendes Paar, z.B. das der Produktions-Umgebung.
: "${LONGLONG68K_QCLIB:=$REPO/build/qclib.l}"
: "${LONGLONG68K_CSTART:=$REPO/build/q9_cstart.r}"
die() { echo "FEHLER: $*" >&2; exit 2; }
[ -f "$LONGLONG68K_QCLIB" ] || die "$LONGLONG68K_QCLIB fehlt -- vorher 'make' oder LONGLONG68K_QCLIB setzen"
[ -f "$LONGLONG68K_CSTART" ] || die "$LONGLONG68K_CSTART fehlt -- vorher 'make' oder LONGLONG68K_CSTART setzen"
[ -f "$IMG_SRC" ] || die "Image fehlt: $IMG_SRC"
[ -f "$REPO/tests/longlong68k.c" ] || die "tests/longlong68k.c fehlt"
WORK=/tmp/longlong-68k
rm -rf "$WORK"; mkdir -p "$WORK"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"
MWOS_UNIX="$MWOS"
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 || die "Toolchain"
MWOS="$MWOS_UNIX"

echo "== 1/3 uebersetzen, assemblieren, binden (eigene Kette) =="
"$QCC/Q9-FRONTEND-C/q9-qcpp/build/qcpp" "$REPO/tests/longlong68k.c" "$WORK/ll.i" || die "qcpp"
"$QCC/Q9-FRONTEND-C/q9-qcir/build/qcir" "@$WORK/ll.i" > "$WORK/ll.ir" 2>"$WORK/qcir.err"
tail -1 "$WORK/ll.ir" | grep -q '^OK$' || { sed 's/^/    /' "$WORK/qcir.err" | head -5; die "qcir"; }
"$QCC/Q9-BACKEND-68K/q9-qir68k/build/qir68k" "$WORK/ll.ir" "$WORK/ll.s68" -os9 -largedata -remotedata >"$WORK/be.log" 2>&1 \
	|| { sed 's/^/    /' "$WORK/be.log" | head -5; die "qir68k"; }
grep -qE 'QMUL|Q64|tc_mul_i64|tc_div_i64' "$WORK/ll.s68" || echo "  warn: keine 64-Bit-Mul/Div-Hilfsroutine im Assembler gefunden -- pruefen, ob das Programm sie ueberhaupt braucht"
"$QCC/Q9-BACKEND-68K/q9-qr68k/build/qr68k" "$WORK/ll.s68" -o="$WORK/ll.r" >"$WORK/qr68.log" 2>&1 \
	|| { sed 's/^/    /' "$WORK/qr68.log" | head -5; die "qr68k"; }
cp "$LONGLONG68K_CSTART" "$WORK/q9_cstart.r"
cp "$LONGLONG68K_QCLIB" "$WORK/qclib.l"
"$QCC/Q9-BACKEND-68K/q9-ql68k/build/ql68k" -a "$WORK/q9_cstart.r" "$WORK/ll.r" \
	-l="$WORK/qclib.l" -M=8K "-O=$WORK/q9_longlong" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_longlong" ] || { sed 's/^/    /' "$WORK/link.log" | head -10; die "ql68k"; }
echo "  ok ($(wc -c < "$WORK/q9_longlong" | tr -d ' ') Byte)"

echo "== 2/3 ins Image =="
"$MWOS_TOOLSHED_OS9" copy -r "$WORK/q9_longlong" "$WORK/img.hda,/CMDS/q9_longlong" >/dev/null 2>&1 || die "copy"
"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/q9_longlong" -e -w -r -pe -pr >/dev/null 2>&1
echo "  ok"

echo "== 3/3 im Emulator ausfuehren =="
cat > "$WORK/run.exp" <<'EOF'
log_file -a LOGFILE
set timeout 300
set send_slow {1 .003}
set prompt {[#$] ?$}
spawn ./build/macos/q9.exe --rom MWOSDIR/OS9/68030/PORTS/Q9/CMDS/BOOTOBJS/ROMBUG/romimage.dev.running.BIN --cf IMAGE
expect "devices online"
send "\r"
set li 0
for {set i 0} {$i < 10 && !$li} {incr i} {
    expect {
        "User name?:" { send "super\r"; exp_continue }
        -re {Password[^\r\n]*:} { send "Al35uUbC\r"; exp_continue }
        -re $prompt { set li 1 }
        timeout { send "\r" }
    }
}
if {!$li} { send_log "\nTEST: LOGIN FAILED\n"; exit 1 }
send -s "/dd/CMDS/q9_longlong\r"
expect {
    -re {falsch[\r\n]}   { }
    -re {Stack Overflow} { send_log "\nTEST: STACK OVERFLOW\n" }
    -re {PMMU}           { send_log "\nTEST: PMMU\n" }
    eof                  { send_log "\nTEST: EMULATOR WEG\n"; exit 1 }
    timeout              { send_log "\nTEST: TIMEOUT\n"; exit 1 }
}
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$WORK/img.hda|" -e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log"
grep -E '^(ok|FALSCH) ' "$WORK/run.log" | sed 's/^/  /'
if grep -q 'longlong68k fertig: 0 falsch' "$WORK/run.log"; then
	echo
	echo "LONG LONG AUS C RECHNET AUF ECHTEM 68030 -- alle Faelle stimmen"
	exit 0
fi
echo; echo "TEST ROT -- Auszug:"; tail -30 "$WORK/run.log" | sed 's/^/    /'
exit 1
