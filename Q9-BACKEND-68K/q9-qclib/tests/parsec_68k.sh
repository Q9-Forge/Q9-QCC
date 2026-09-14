#!/usr/bin/env bash
# PARSEC AUF DEM 68030: der Parsergenerator uebersetzt die echte QCC-Grammatik
# auf dem Ziel.
#
# Der QCC-Port von parsec.cpp/codegen.cpp (src-qcc/ebnf.tc + codegen.tc) war
# bis hierher nur STRUKTURELL nachgewiesen: er uebersetzt und linkt sauber,
# aber gelaufen ist er auf dem Ziel nie (Schritt 3 des Vollport-Plans). Dieser
# Pruefstand schliesst das.
#
# GEPRUEFT WIRD EIN FIXPUNKT, nicht "laeuft durch": die auf dem 68030 erzeugte
# Arbeitsdatei muss byteidentisch zu der sein, die der Host-parsec aus
# derselben Grammatik erzeugt -- bis auf den Erzeugernamen in Zeile 2, den das
# Original bewusst anders schreibt ("ebnf" statt "parsec").
#
# ZWEI HAELFTEN, GETRENNT AUSGEWIESEN (Stand 2026-09-14):
#   Grammatik -> Arbeitsdatei   laeuft auf dem 68030 und ist BYTEIDENTISCH.
#   Codegenerierung             faultet dort (PMMU, A0=$00000001 -- ein
#                               Datenwert als Adresse, dieselbe Bauform wie der
#                               am selben Tag behobene tc_derefref-Fehler).
# Der Lauf ist deshalb erst gruen, wenn BEIDE Haelften stimmen; welche haelt und
# welche nicht, sagt die Ausgabe.
#
# main() im Port hat keinen argc/argv-Mechanismus und traegt den Basisnamen
# "qcc" fest einprogrammiert; die Grammatik heisst auf dem Ziel deshalb
# /dd/HOME/ROOT/qcc.ebnf.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd ../../.. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${PARSEC:=$QCC/Q9-PARSEC}"
: "${FLUX:=$FORGE/Q9-Flux/Q9-Flux-68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
[[ "$MWOS" == Z:* ]] && MWOS=/Volumes/SSD1TB/projects/MWOS
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"
: "${STACK_KB:=1024}"

WORK=/tmp/qclib-parsec
die() { echo "FEHLER: $*" >&2; exit 2; }

[ -f "$REPO/build/qclib.l" ]     || die "build/qclib.l fehlt -- vorher make"
[ -f "$REPO/build/q9_cstart.r" ] || die "build/q9_cstart.r fehlt -- vorher make"
[ -x "$PARSEC/build/parsec" ]    || die "parsec fehlt -- vorher bauen"
[ -f "$IMG_SRC" ]                || die "Abbild fehlt: $IMG_SRC"
QCIR="${QCIR:-$QCC/Q9-FRONTEND-C/q9-qcir/build/qcir}"
QIR68K="${QIR68K:-$QCC/Q9-BACKEND-68K/q9-qir68k/build/qir68k}"
QR68="${QR68:-$QCC/Q9-BACKEND-68K/q9-qr68k/build/qr68k}"
QL68="${QL68:-$QCC/Q9-BACKEND-68K/q9-ql68k/build/ql68k}"
for t in "$QCIR" "$QIR68K" "$QR68" "$QL68"; do
	[ -x "$t" ] || die "Werkzeug fehlt: $t"
done

rm -rf "$WORK"; mkdir -p "$WORK/host" "$WORK/ziel"
cp "$REPO/build/qclib.l" "$REPO/build/q9_cstart.r" "$WORK/"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
OS9="$MWOS_TOOLSHED_OS9"

echo "== 1/6 das Orakel: Host-parsec auf dieselbe Grammatik =="
cp "$PARSEC/data/qcc.ebnf" "$PARSEC/data/qcc.lextab" "$WORK/host/"
( cd "$WORK/host" && "$PARSEC/build/parsec" qcc ) > "$WORK/host/lauf.txt" 2>&1 ||
	die "Host-parsec"
[ -f "$WORK/host/qcc.lextab" ] || die "Host-parsec hat keine Arbeitsdatei geschrieben"
echo "  ok ($(wc -c < "$WORK/host/qcc.lextab" | tr -d ' ') Byte Arbeitsdatei)"

echo "== 2/6 den Port mit der eigenen Kette zum 68k-Modul =="
# ebnf.tc traegt main() und den gemeinsamen Laufzeitkern (-runtime),
# codegen.tc ist reines Beiwerk (-part).
"$QCIR" "@$PARSEC/src-qcc/ebnf.tc"    > "$WORK/ebnf.ir" 2> "$WORK/ebnf.err"
[ "$(tail -1 "$WORK/ebnf.ir")" = OK ] || { head -5 "$WORK/ebnf.err"; die "qcir auf ebnf.tc"; }
"$QCIR" "@$PARSEC/src-qcc/codegen.tc" > "$WORK/cgen.ir" 2> "$WORK/cgen.err"
[ "$(tail -1 "$WORK/cgen.ir")" = OK ] || { head -5 "$WORK/cgen.err"; die "qcir auf codegen.tc"; }
"$QIR68K" "$WORK/ebnf.ir" "$WORK/ebnf.s68" -os9 -largedata -remotedata -part -runtime >/dev/null ||
	die "qir68k auf ebnf"
"$QIR68K" "$WORK/cgen.ir" "$WORK/cgen.s68" -os9 -largedata -remotedata -part >/dev/null ||
	die "qir68k auf codegen"
"$QR68" "$WORK/ebnf.s68" "-o=$WORK/ebnf.r" >"$WORK/asm1.log" 2>&1 || { head -5 "$WORK/asm1.log"; die "qr68 auf ebnf"; }
"$QR68" "$WORK/cgen.s68" "-o=$WORK/cgen.r" >"$WORK/asm2.log" 2>&1 || { head -5 "$WORK/asm2.log"; die "qr68 auf codegen"; }
"$QL68" -a "$WORK/q9_cstart.r" "$WORK/ebnf.r" "$WORK/cgen.r" -l="$WORK/qclib.l" \
	-M="${STACK_KB}K" "-O=$WORK/q9_parsec" >"$WORK/link.log" 2>&1 ||
	{ head -10 "$WORK/link.log"; die "ql68"; }
[ -f "$WORK/q9_parsec" ] || die "ql68 hat kein Modul geschrieben"
echo "  ok ($(wc -c < "$WORK/q9_parsec" | tr -d ' ') Byte Modul)"

"$OS9" ident "$WORK/q9_parsec" | grep -q 'Stack size: *\$C00\b' &&
	die "Stack size ist der Standardwert -- -M= hat nicht gegriffen"

echo "== 3/6 Abbild bestuecken =="
IMAGE="$FLUX/local_images/OS9SYS.parsec68k.hda"
rm -f "$IMAGE"
cp -c "$IMG_SRC" "$IMAGE" 2>/dev/null || cp "$IMG_SRC" "$IMAGE" || die "Abbild kopieren"
"$OS9" copy -r "$WORK/q9_parsec" "$IMAGE,/CMDS/q9_parsec" >/dev/null 2>&1 || die "copy Modul"
"$OS9" attr -e -w -r -pe -pr "$IMAGE,/CMDS/q9_parsec" >/dev/null 2>&1 || die "attr -e"
# Roh kopieren: qclibs fgets trennt an $0a, "copy -l" wuerde $0d schreiben.
"$OS9" copy -r "$PARSEC/data/qcc.ebnf"   "$IMAGE,/HOME/ROOT/qcc.ebnf"   >/dev/null 2>&1 || die "copy qcc.ebnf"
"$OS9" copy -r "$PARSEC/data/qcc.lextab" "$IMAGE,/HOME/ROOT/qcc.lextab" >/dev/null 2>&1 || die "copy qcc.lextab"
echo "  ok"

echo "== 4/6 im Emulator: parsec uebersetzt die Grammatik =="
rm -f "$WORK/run.log"
cat > "$WORK/run.exp" <<'EOF'
log_file -a LOGFILE
set timeout 1800
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
if {!$li} { send_log "\nPARSEC: LOGIN FAILED\n"; exit 1 }
send_log "\nPARSEC: LAUF BEGINNT\n"
send -s "/dd/CMDS/q9_parsec\r"
# Bei einem Fault NICHT abbrechen: die Arbeitsdatei ist dann schon geschrieben
# und wird unten trotzdem verglichen -- genau daran haengt die Aussage, welche
# Haelfte des Ports auf dem Ziel traegt.
expect {
    -re {CODEGEN:[^\r\n]*}  { send_log "\nPARSEC: LAUF ZU ENDE\n" }
    -re {Grammatik fehlerhaft} { send_log "\nPARSEC: GRAMMATIK FEHLERHAFT\n" }
    -re {PMMU}              { send_log "\nPARSEC: PMMU FAULT\n" }
    -re {Stack Overflow}    { send_log "\nPARSEC: STACK OVERFLOW\n" }
    eof                     { send_log "\nPARSEC: EMULATOR WEG\n" }
    timeout                 { send_log "\nPARSEC: TIMEOUT\n" }
}
catch { expect -re $prompt }
catch { send "\x1d" }
catch { expect eof }
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$IMAGE|" \
	-e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log -- lief der Emulator?"
if grep -q "PARSEC: LAUF ZU ENDE" "$WORK/run.log"; then
	codegenOk=1
	echo "  ok (bis einschliesslich Codegenerierung)"
else
	codegenOk=0
	echo "  Codegenerierung nicht erreicht:"
	grep "PARSEC:" "$WORK/run.log" | tail -1 | sed 's/^/    /'
	grep -m1 "A0-A7" "$WORK/run.log" | sed 's/^/    /'
fi

echo "== 5/6 Ergebnis zurueckholen =="
pkill -f "q9.exe.*OS9SYS.parsec68k.hda" 2>/dev/null
sleep 2
"$OS9" copy -l -r "$IMAGE,/HOME/ROOT/qcc.lextab" "$WORK/ziel/qcc.lextab" >/dev/null 2>&1 ||
	die "Arbeitsdatei nicht aus dem Abbild lesbar"
echo "  $(wc -c < "$WORK/ziel/qcc.lextab" | tr -d ' ') Byte"

echo "== 6/6 Fixpunkt pruefen =="
# Der Erzeugername ist der EINZIGE bewusste Unterschied: das Original schreibt
# "parsec", der Port "ebnf" (er heisst auf dem Ziel so).
norm() { sed -e 's/erzeugt von .*/erzeugt von X/' "$1"; }
if diff <(norm "$WORK/host/qcc.lextab") <(norm "$WORK/ziel/qcc.lextab") > "$WORK/diff.txt"; then
	tabOk=1
	echo "  Arbeitsdatei BYTEIDENTISCH zum Hostlauf"
else
	tabOk=0
	echo "  Arbeitsdatei WEICHT AB ($(grep -c '^[<>]' "$WORK/diff.txt") Zeilen):"
	head -20 "$WORK/diff.txt" | sed 's/^/    /'
fi

echo
if [ "$tabOk" -eq 1 ] && [ "$codegenOk" -eq 1 ]; then
	echo "PARSEC LAEUFT AUF DEM 68030: der Parsergenerator uebersetzt dort die"
	echo "echte QCC-Grammatik zu derselben Arbeitsdatei wie am Host, und die"
	echo "Codegenerierung laeuft mit durch."
	exit 0
fi
if [ "$tabOk" -eq 1 ]; then
	echo "HALB: Grammatik -> Arbeitsdatei traegt auf dem 68030 (byteidentisch),"
	echo "die Codegenerierung nicht (siehe Stufe 4). Das ist der offene Punkt."
else
	echo "ROT: schon die Arbeitsdatei weicht ab."
fi
exit 1
