#!/usr/bin/env bash
# MESSUNG: wie trennt Microwares clib Zeilen? (siehe test/lineend.c)
#
# Kein Soll-Ist-Vergleich -- das Ergebnis ist die VORGABE fuer qclibs fgets.
# Gebunden wird gegen clib (mit l68 ueber Wine), weil qclib noch kein fgets
# hat; uebersetzt wird mit der eigenen Kette.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd .. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux-68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -f "$IMG_SRC" ] || die "Abbild fehlt: $IMG_SRC"

WORK=/tmp/qclib-lineend
rm -rf "$WORK"; mkdir -p "$WORK"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
WINE_BIN="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
export WINEPREFIX="$HOME/.wine" WINEDEBUG=-all
TW="Z:$(printf '%s' "$WORK" | sed 's#/#\\#g')"

echo "== 1/3 uebersetzen (eigene Kette) und gegen clib binden =="
"$QCC/q9-cpp/build/qcpp" -I"$QCC/q9-cpp/include" test/lineend.c "$WORK/le.i" || die "qcpp"
"$QCC/build/qcir" "@$WORK/le.i" > "$WORK/le.ir" 2> "$WORK/le.err"
[ "$(tail -1 "$WORK/le.ir")" = OK ] || { head -5 "$WORK/le.err"; die "qcir"; }
"$QCC/build/qir_68k" "$WORK/le.ir" "$WORK/le.s68" -os9 -largedata >/dev/null || die "Backend"
"$FORGE/Q9-qr68/build/qr68" "$WORK/le.s68" "-o=$WORK/le.r" || die "qr68"
cp "$QCC/runtime/os9/q9_cstart.a" "$QCC/runtime/os9/q9defs.d" "$WORK/"
( cd "$WORK" && "$FORGE/Q9-qr68/build/qr68" q9_cstart.a -o=q9_cstart.r ) || die "qr68 cstart"
arch -x86_64 "$WINE_BIN" cmd /c \
	"set PATH=M:\\DOS\\BIN;%PATH% && M:\\DOS\\BIN\\l68.exe -a $TW\\q9_cstart.r $TW\\le.r -l=M:\\OS9\\68020\\LIB\\clib.l -l=M:\\OS9\\68020\\LIB\\os_lib.l -l=M:\\OS9\\68000\\LIB\\sys.l -M=8K -o=$TW\\q9_lineend" \
	> "$WORK/link.log" 2>&1
[ -f "$WORK/q9_lineend" ] || { grep -iE "error|unresolved" "$WORK/link.log" | head -6; die "l68 gegen clib"; }
echo "  ok ($(wc -c < "$WORK/q9_lineend" | tr -d ' ') Byte)"

echo "== 2/3 ins Abbild und laufen lassen =="
"$MWOS_TOOLSHED_OS9" copy -r "$WORK/q9_lineend" "$WORK/img.hda,/CMDS/q9_lineend" >/dev/null 2>&1 ||
	die "copy"
"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/q9_lineend" -e -w -r -pe -pr >/dev/null 2>&1 ||
	die "attr"
cat > "$WORK/run.exp" <<'EOF'
log_file -a LOGFILE
set timeout 240
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
if {!$li} { send_log "\nMESS: LOGIN FAILED\n"; exit 1 }
send -s "/dd/CMDS/q9_lineend\r"
expect {
    -re {zeilen insgesamt}  { send_log "\nMESS: DURCH\n" }
    -re {PMMU}              { send_log "\nMESS: PMMU\n" }
    timeout                 { send_log "\nMESS: TIMEOUT\n" }
}
expect -re $prompt
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$WORK/img.hda|" \
	-e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log"

echo "== 3/3 Messwerte =="
tr -d '\r' < "$WORK/run.log" | grep -E "geschrieben|zeile|MESS:" | sed 's/^/  /'
echo
echo "(Log: $WORK/run.log)"
