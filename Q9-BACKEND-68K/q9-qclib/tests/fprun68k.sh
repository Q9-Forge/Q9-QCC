#!/usr/bin/env bash
# Rechnet die FPU auf dem Ziel wirklich? -- Gegenprobe zu tests/fpu.a.
#
# tests/fpu.a zeigt, dass qr68 dieselben BYTES erzeugt wie r68. Ob diese
# Bytes auf dem Ziel das Richtige TUN, ist eine andere Frage: der 68030 hat
# keine FPU, OS-9 faengt die F-Line-Befehle ueber das Modul
# /CMDS/BOOTOBJS/fpu ab und rechnet sie nach. Erst dieser Lauf schliesst
# die Luecke zwischen "byteidentisch" und "laeuft".
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
[ -f "$REPO/tests/fprun.a" ] || die "tests/fprun.a fehlt"
WORK=/tmp/fprun-68k
rm -rf "$WORK"; mkdir -p "$WORK"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"
MWOS_UNIX="$MWOS"
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 || die "Toolchain"
MWOS="$MWOS_UNIX"

echo "== 1/3 uebersetzen, assemblieren, binden (eigene Kette) =="
"$QCC/Q9-FRONTEND-C/q9-qcpp/build/qcpp" "$REPO/tests/fprun.c" "$WORK/fprun.i" || die "qcpp"
"$QCC/Q9-FRONTEND-C/q9-qcir/build/qcir" "@$WORK/fprun.i" > "$WORK/fprun.ir" 2>"$WORK/qcir.err"
tail -1 "$WORK/fprun.ir" | grep -q '^OK$' || { sed 's/^/    /' "$WORK/qcir.err" | head -5; die "qcir"; }
"$QCC/Q9-BACKEND-68K/q9-qir68k/build/qir68k" "$WORK/fprun.ir" "$WORK/fprun_c.s68" -os9 -largedata -remotedata >/dev/null || die "qir68k"
"$QCC/Q9-BACKEND-68K/q9-qr68k/build/qr68k" "$WORK/fprun_c.s68" -o="$WORK/fprun_c.r" >"$WORK/qr68c.log" 2>&1 \
	|| { sed 's/^/    /' "$WORK/qr68c.log" | head -5; die "qr68 (C-Teil)"; }
# Der Gleitkommateil -- von qr68 assembliert, das ist hier der Prueffall.
"$QCC/Q9-BACKEND-68K/q9-qr68k/build/qr68k" "$REPO/tests/fprun.a" -o="$WORK/fprun_a.r" >"$WORK/qr68a.log" 2>&1 \
	|| { sed 's/^/    /' "$WORK/qr68a.log" | head -5; die "qr68 (Gleitkommateil)"; }
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"
"$QCC/Q9-BACKEND-68K/q9-ql68k/build/ql68k" -a "$WORK/q9_cstart.r" "$WORK/fprun_c.r" "$WORK/fprun_a.r" \
	-l="$WORK/qclib.l" -M=8K "-O=$WORK/q9_fprun" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_fprun" ] || { sed 's/^/    /' "$WORK/link.log" | head -10; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/q9_fprun" | tr -d ' ') Byte)"

echo "== 2/3 ins Image =="
"$MWOS_TOOLSHED_OS9" copy -r "$WORK/q9_fprun" "$WORK/img.hda,/CMDS/q9_fprun" >/dev/null 2>&1 || die "copy"
"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/q9_fprun" -e -w -r -pe -pr >/dev/null 2>&1
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
send -s "/dd/CMDS/q9_fprun\r"
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
if grep -q 'fprun fertig: 0 von 6 falsch' "$WORK/run.log"; then
	echo
	echo "DIE FPU RECHNET AUF ECHTEM 68030 -- alle sechs Rechnungen stimmen"
	exit 0
fi
echo; echo "TEST ROT -- Auszug:"; tail -20 "$WORK/run.log" | sed 's/^/    /'
exit 1
