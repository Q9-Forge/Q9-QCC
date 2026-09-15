#!/usr/bin/env bash
# Zeigertabelle mit String-Literalen (char *tab[] = {"a","b"}) auf echtem
# 68030. Das ist der einzige Pruefstein, der zaehlt: die Adressen in so
# einer Tabelle stehen erst zur LADEZEIT fest -- OS-9 zieht sie beim F$Fork
# ueber die M$IRefs-Liste auf die tatsaechliche Ladeadresse. Ob das Modul
# richtig gebaut ist, sieht man weder am IR noch am Assembler, sondern erst
# daran, dass das Programm die Zeichenketten findet.
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
WORK=/tmp/ptrtab-68k
rm -rf "$WORK"; mkdir -p "$WORK"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"
MWOS_UNIX="$MWOS"
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 || die "Toolchain"
MWOS="$MWOS_UNIX"

cat > "$WORK/ptrtab.c" <<'EOF'
extern int printf(char*, ...);
char *namen[3] = {"eins", "zwei", "drei"};
int zahlen[3] = {10, 20, 30};
int main()
{
	char *p;
	int i;
	i = 0;
	while (i < 3) {
		p = namen[i];
		printf("n %d %s %d\n", i, p, zahlen[i]);
		i = i + 1;
	}
	printf("ptrtab fertig\n");
	return 0;
}
EOF

echo "== 1/4 uebersetzen und binden (eigene Kette) =="
"$QCC/Q9-FRONTEND-C/q9-qcir/build/qcir" "@$WORK/ptrtab.c" > "$WORK/ptrtab.ir" 2>"$WORK/qcir.err"
tail -1 "$WORK/ptrtab.ir" | grep -q '^OK$' || { sed 's/^/    /' "$WORK/qcir.err" | head -5; die "qcir"; }
"$QCC/Q9-BACKEND-68K/q9-qir68k/build/qir68k" "$WORK/ptrtab.ir" "$WORK/ptrtab.s68" -os9 -largedata -remotedata >/dev/null || die "qir68k"
grep -q 'vsect' "$WORK/ptrtab.s68" || die "kein vsect im Assembler -- die Tabelle laege im psect"
"$QCC/Q9-BACKEND-68K/q9-qr68k/build/qr68k" "$WORK/ptrtab.s68" -o="$WORK/ptrtab.r" >"$WORK/qr68.log" 2>&1 || { sed 's/^/    /' "$WORK/qr68.log"|head -5; die "qr68"; }
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/"
"$QCC/Q9-BACKEND-68K/q9-ql68k/build/ql68k" -a "$WORK/q9_cstart.r" "$WORK/ptrtab.r" \
	-l="$WORK/qclib.l" -M=8K "-O=$WORK/q9_ptrtab" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_ptrtab" ] || { sed 's/^/    /' "$WORK/link.log"|head -10; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/q9_ptrtab" | tr -d ' ') Byte)"

echo "== 2/4 IRefs im Modul pruefen =="
python3 - "$WORK/q9_ptrtab" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
irefs = int.from_bytes(d[0x44:0x48], 'big')
idata = int.from_bytes(d[0x40:0x44], 'big')
if irefs == 0:
    print("  FEHLER: M$IRefs ist 0 -- der Lader relokiert nichts"); sys.exit(1)
n = int.from_bytes(d[irefs+2:irefs+4], 'big')
print("  ok (M$IData=$%x, M$IRefs=$%x, %d Zeiger angemeldet)" % (idata, irefs, n))
if n < 3:
    print("  FEHLER: weniger als drei Zeiger angemeldet"); sys.exit(1)
EOF
[ $? -eq 0 ] || die "IRefs"

echo "== 3/4 ins Image =="
"$MWOS_TOOLSHED_OS9" copy -r "$WORK/q9_ptrtab" "$WORK/img.hda,/CMDS/q9_ptrtab" >/dev/null 2>&1 || die "copy"
"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/q9_ptrtab" -e -w -r -pe -pr >/dev/null 2>&1
echo "  ok"

echo "== 4/4 im Emulator ausfuehren =="
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
if {!$li} { send_log "\nTEST: LOGIN FAILED\n"; exit 1 }
send -s "/dd/CMDS/q9_ptrtab\r"
expect {
    -re {ptrtab fertig} { }
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
fail=0
for soll in "n 0 eins 10" "n 1 zwei 20" "n 2 drei 30"; do
	if grep -qF "$soll" "$WORK/run.log"; then echo "  ok    $soll"
	else echo "  FEHLT $soll"; fail=1; fi
done
if [ $fail -eq 0 ]; then
	echo; echo "ZEIGERTABELLE LAEUFT AUF ECHTEM 68030 -- die Adressen sind korrekt relokiert"
	exit 0
fi
echo; echo "TEST ROT -- Auszug:"; tail -20 "$WORK/run.log" | sed 's/^/    /'
exit 1
