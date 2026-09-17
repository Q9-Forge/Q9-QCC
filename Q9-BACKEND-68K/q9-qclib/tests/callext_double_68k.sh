#!/usr/bin/env bash
# CALLEXT/CALLEXTP mit double-Argumenten auf echtem 68030 -- prueft die
# Microware-ABI-Platzierung (Byte-Offset-Modell mit Sticky-Spill, an echtem
# xcc gemessen, s. docs/FLOAT_PLAN_de.md) gegen fuenf Mock-Stubs, die die
# rohen Bitmuster in d0/d1/Stack lesen. Nach dem Muster von double68k.sh.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd ../../.. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux/Q9-Flux-68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"
die() { echo "FEHLER: $*" >&2; exit 2; }
[ -f "$REPO/build/qclib.l" ] || die "build/qclib.l fehlt -- vorher 'make'"
[ -f "$IMG_SRC" ] || die "Image fehlt: $IMG_SRC"
[ -f "$REPO/tests/callext_double_68k.c" ] || die "tests/callext_double_68k.c fehlt"
[ -f "$REPO/tests/callext_double_mocks.a" ] || die "tests/callext_double_mocks.a fehlt"
WORK=/tmp/callext-double-68k
rm -rf "$WORK"; mkdir -p "$WORK"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"
MWOS_UNIX="$MWOS"
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 || die "Toolchain"
MWOS="$MWOS_UNIX"

echo "== 1/3 uebersetzen, assemblieren, binden (eigene Kette + Mock-Stubs) =="
"$QCC/Q9-FRONTEND-C/q9-qcpp/build/qcpp" "$REPO/tests/callext_double_68k.c" "$WORK/t.i" || die "qcpp"
"$QCC/Q9-FRONTEND-C/q9-qcir/build/qcir" "@$WORK/t.i" > "$WORK/t.ir" 2>"$WORK/qcir.err"
tail -1 "$WORK/t.ir" | grep -q '^OK$' || { sed 's/^/    /' "$WORK/qcir.err" | head -10; die "qcir"; }
"$QCC/Q9-BACKEND-68K/q9-qir68k/build/qir68k" "$WORK/t.ir" "$WORK/t.s68" -os9 -largedata -remotedata >"$WORK/be.log" 2>&1 \
	|| { sed 's/^/    /' "$WORK/be.log" | head -10; die "qir68k"; }
"$QCC/Q9-BACKEND-68K/q9-qr68k/build/qr68k" "$WORK/t.s68" -o="$WORK/t.r" >"$WORK/qr68_t.log" 2>&1 \
	|| { sed 's/^/    /' "$WORK/qr68_t.log" | head -10; die "qr68k (t.s68)"; }
"$QCC/Q9-BACKEND-68K/q9-qr68k/build/qr68k" "$REPO/tests/callext_double_mocks.a" -o="$WORK/mocks.r" >"$WORK/qr68_m.log" 2>&1 \
	|| { sed 's/^/    /' "$WORK/qr68_m.log" | head -10; die "qr68k (mocks.a)"; }
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"
"$QCC/Q9-BACKEND-68K/q9-ql68k/build/ql68k" -a "$WORK/q9_cstart.r" "$WORK/t.r" "$WORK/mocks.r" \
	-l="$WORK/qclib.l" -M=8K "-O=$WORK/q9_callext_dbl" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_callext_dbl" ] || { sed 's/^/    /' "$WORK/link.log" | head -10; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/q9_callext_dbl" | tr -d ' ') Byte)"

echo "== 2/3 ins Image =="
"$MWOS_TOOLSHED_OS9" copy -r "$WORK/q9_callext_dbl" "$WORK/img.hda,/CMDS/q9_callext_dbl" >/dev/null 2>&1 || die "copy"
"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/q9_callext_dbl" -e -w -r -pe -pr >/dev/null 2>&1
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
send -s "/dd/CMDS/q9_callext_dbl\r"
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
if grep -q 'callext_dbl_abi fertig: 0 von 5 falsch' "$WORK/run.log"; then
	echo
	echo "CALLEXT-double-ABI KORREKT -- alle 5 Faelle stimmen"
	exit 0
fi
echo; echo "TEST ROT -- Auszug:"; tail -20 "$WORK/run.log" | sed 's/^/    /'
exit 1
