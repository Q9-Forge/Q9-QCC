#!/usr/bin/env bash
# DER RINGSCHLUSS: die ganze Kette laeuft auf dem 68030.
#
# Bis hierher war jedes Werkzeug EINZELN auf dem Ziel nachgewiesen -- qcpp,
# qcc, qcc_backend, qr68, ql68, jedes gegen qclib gebunden. Was fehlte, war
# der Lauf, der sie HINTEREINANDER schaltet: von der C-Quelle bis zum
# lauffaehigen Modul, ohne dass zwischendurch etwas am Host passiert.
#
#   Vorbereitung (Host)   die fuenf Werkzeuge als 68k-Module, mit der
#                         eigenen Kette gebaut und gegen qclib gebunden
#   Stufe 1  (68030)      qcpp        hello.c    -> hello.i
#   Stufe 2  (68030)      qcc        @hello.i    -> hello.ir
#   Stufe 3  (68030)      qcc_backend hello.ir   -> hello.s68
#   Stufe 4  (68030)      qr68        hello.s68  -> hello.r
#   Stufe 5  (68030)      ql68        hello.r    -> q9_hk (Modul)
#   Stufe 6  (68030)      attr q9_hk -e -pe  (sonst startet OS-9 es nicht)
#   Stufe 7  (68030)      /dd/HOME/ROOT/q9_hk ausfuehren
#   Pruefung (Host)       das auf dem Ziel gebaute Modul BYTEIDENTISCH zum
#                         am Host gebauten, und die 24 Ausgabezeilen richtig
#
# DIE NAMEN MUESSEN AUF BEIDEN SEITEN GLEICH SEIN: qcc_backend leitet bei
# -os9 den psect-Namen aus dem Ausgabenamen ab, ql68 den Modulnamen aus -O=.
# Verschiedene Namen geben immer einen Unterschied -- diese Falle steht
# mehrfach in den Projektnotizen.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd .. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux-68k}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -f "$REPO/build/qclib.l" ]     || die "build/qclib.l fehlt -- vorher make"
[ -f "$REPO/build/q9_cstart.r" ] || die "build/q9_cstart.r fehlt -- vorher make"
[ -f "$IMG_SRC" ]                || die "Abbild fehlt: $IMG_SRC"
[ -f "$QCC/build/qcc_p.bootstrap.c" ] ||
	die "build/qcc_p.bootstrap.c fehlt (Q9-QCC/tools/build_xcc_bootstrap.sh)"

QCPP_H="$QCC/q9-cpp/build/qcpp"
QCCP="$QCC/build/qcc_p"
QCCB="$QCC/build/qcc_backend"
QR68="$FORGE/Q9-qr68/build/qr68"
QL68="$FORGE/Q9-ql68/build/ql68"
for t in "$QCPP_H" "$QCCP" "$QCCB" "$QR68" "$QL68"; do
	[ -x "$t" ] || die "Werkzeug fehlt: $t"
done

WORK=/tmp/qclib-kette
rm -rf "$WORK"; mkdir -p "$WORK/host" "$WORK/ziel" "$WORK/mod"
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"
OS9="$MWOS_TOOLSHED_OS9"

# Ein Werkzeug als 68k-Modul: qcpp -> qcc -> Backend -> qr68 -> ql68, alles
# eigene Werkzeuge, gebunden gegen qclib. $1 = Modulname, $2 = Quelle,
# $3 = Stack in KB, ab $4 zusaetzliche qcpp-Schalter (etwa -D_Q9OS).
baue_modul() {
	local name="$1" quelle="$2" stack="$3"; shift 3
	local d="$WORK/mod/$name"
	mkdir -p "$d"
	"$QCPP_H" "$@" -I"$QCC/q9-cpp/include" "$quelle" "$d/x.i" ||
		die "$name: qcpp"
	"$QCCP" "@$d/x.i" > "$d/x.ir" 2> "$d/x.err"
	[ "$(tail -1 "$d/x.ir")" = OK ] || { head -6 "$d/x.err"; die "$name: qcc"; }
	[ ! -s "$d/x.err" ] || { head -6 "$d/x.err"; die "$name: Semantikmeldungen"; }
	"$QCCB" "$d/x.ir" "$d/x.s68" -os9 -largedata -remotedata >/dev/null || die "$name: Backend"
	"$QR68" "$d/x.s68" "-o=$d/x.r" >/dev/null || die "$name: qr68"
	cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$d/"
	"$QL68" -a "$d/q9_cstart.r" "$d/x.r" -l="$d/qclib.l" -M="${stack}K" \
		"-O=$WORK/mod/$name.mod" >"$d/link.log" 2>&1
	[ -f "$WORK/mod/$name.mod" ] || { head -6 "$d/link.log"; die "$name: ql68"; }
	printf '  %-8s %9s Byte\n' "$name" "$(wc -c < "$WORK/mod/$name.mod" | tr -d ' ')"
}

# Mit REUSE=1 werden schon gebaute Werkzeugmodule aus einem frueheren Lauf
# weiterverwendet (sie liegen in /tmp/qclib-kette-mod). Das ist NUR fuer das
# Nacharbeiten am Emulatorteil gedacht -- der regulaere Lauf baut alles neu,
# sonst prueft er eine alte Kette.
: "${REUSE:=0}"
CACHE=/tmp/qclib-kette-mod
if [ "$REUSE" = 1 ] && [ -d "$CACHE" ]; then
	echo "== 1/6 Werkzeugmodule aus $CACHE (REUSE=1) =="
	cp "$CACHE"/*.mod "$WORK/mod/" || die "Zwischenlager unvollstaendig"
	for m in "$WORK"/mod/*.mod; do
		printf '  %-12s %9s Byte\n' "$(basename "$m")" "$(wc -c < "$m" | tr -d ' ')"
	done
else
echo "== 1/6 die fuenf Werkzeuge als 68k-Module (eigene Kette) =="
# Der Modulname kommt aus -O= und muss der sein, den der Emulatorlauf ruft.
# qr68 und ql68 mit -D_Q9OS: das setzt ihre Feldgroessen auf Zielmass, sonst
# waere jedes genullte Feld ein Kilobyte Modul.
baue_modul q9_qcpp "$QCC/q9-cpp/src/qcpp.c"          512 -D_Q9OS
baue_modul q9_qcc  "$QCC/build/qcc_p.bootstrap.c"   1024
baue_modul q9_qccb "$QCC/Source/qcc_backend_c.cpp"    64
baue_modul q9_qr68 "$FORGE/Q9-qr68/src/qr68.c"       512 -D_Q9OS
baue_modul q9_ql68 "$FORGE/Q9-ql68/src/ql68.c"       512 -D_Q9OS
mkdir -p "$CACHE" && cp "$WORK"/mod/*.mod "$CACHE"/
fi

echo "== 2/6 dieselbe Aufgabe am Host, als Vergleich =="
# GLEICHE NAMEN wie auf dem Ziel: hello.i, hello.ir, hello.s68, hello.r, q9_hk.
cp test/hello.c "$WORK/host/hello.c"
cp "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/host/"
( cd "$WORK/host" &&
  "$QCPP_H" hello.c hello.i &&
  "$QCCP" "@hello.i" > hello.ir 2> hello.err &&
  [ "$(tail -1 hello.ir)" = OK ] &&
  "$QCCB" hello.ir hello.s68 -os9 -largedata -remotedata >/dev/null &&
  "$QR68" hello.s68 -o=hello.r >/dev/null &&
  "$QL68" -a q9_cstart.r hello.r -l=qclib.l -M=8K -O=q9_hk > link.log 2>&1
) || die "Hostlauf"
[ -f "$WORK/host/q9_hk" ] || die "Hostlauf hat kein Modul geschrieben"
echo "  hello.i $(wc -c < "$WORK/host/hello.i" | tr -d ' ')  hello.ir $(wc -c < "$WORK/host/hello.ir" | tr -d ' ')  hello.s68 $(wc -c < "$WORK/host/hello.s68" | tr -d ' ')  hello.r $(wc -c < "$WORK/host/hello.r" | tr -d ' ')  Modul $(wc -c < "$WORK/host/q9_hk" | tr -d ' ')"

echo "== 3/6 Abbild bestuecken =="
for m in q9_qcpp q9_qcc q9_qccb q9_qr68 q9_ql68; do
	"$OS9" copy -r "$WORK/mod/$m.mod" "$WORK/img.hda,/CMDS/$m" >/dev/null 2>&1 ||
		die "copy $m"
	"$OS9" attr "$WORK/img.hda,/CMDS/$m" -e -w -r -pe -pr >/dev/null 2>&1 ||
		die "attr $m"
done
# Die Eingaben ROH kopieren: "copy -l" setzt OS-9-Zeilenenden ($0d), und
# qclibs fgets trennt an $0a (test/lineend68k.sh, src/file.c).
"$OS9" copy -r test/hello.c "$WORK/img.hda,/HOME/ROOT/hello.c" >/dev/null 2>&1 || die "copy hello.c"
"$OS9" copy -r "$REPO/build/q9_cstart.r" "$WORK/img.hda,/HOME/ROOT/q9_cstart.r" >/dev/null 2>&1 || die "copy cstart"
"$OS9" copy -r "$REPO/build/qclib.l" "$WORK/img.hda,/HOME/ROOT/qclib.l" >/dev/null 2>&1 || die "copy qclib"
echo "  ok"

echo "== 4/6 im Emulator: fuenf Bauschritte, Attribut, Lauf =="
cat > "$WORK/run.exp" <<'EOF'
log_file -a LOGFILE
# Weit gefasst: qcc_backend allein ist ein 9,6-MB-Modul und will vom
# CF-Abbild geladen werden.
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
if {!$li} { send_log "\nKETTE: LOGIN FAILED\n"; exit 1 }

# Jede Stufe einzeln, und nach jeder auf den Prompt warten. Die Marken
# stehen im Log, damit ein Abbruch die STUFE benennt und nicht nur
# "irgendwas ging schief".
proc stufe {nr cmd} {
    global prompt
    send_log "\nKETTE: STUFE $nr BEGINNT\n"
    send -s "$cmd\r"
    expect {
        -re $prompt          { send_log "\nKETTE: STUFE $nr ZU ENDE\n" }
        -re {PMMU}           { send_log "\nKETTE: STUFE $nr PMMU\n"; exit 1 }
        -re {Stack Overflow} { send_log "\nKETTE: STUFE $nr STACK\n"; exit 1 }
        eof                  { send_log "\nKETTE: STUFE $nr EMULATOR WEG\n"; exit 1 }
        timeout              { send_log "\nKETTE: STUFE $nr TIMEOUT\n"; exit 1 }
    }
}

stufe 1 "/dd/CMDS/q9_qcpp hello.c hello.i"
stufe 2 "/dd/CMDS/q9_qcc @hello.i >hello.ir"
stufe 3 "/dd/CMDS/q9_qccb hello.ir hello.s68 -os9 -largedata -remotedata"
stufe 4 "/dd/CMDS/q9_qr68 hello.s68 -o=hello.r"
stufe 5 "/dd/CMDS/q9_ql68 -a q9_cstart.r hello.r -l=qclib.l -M=8K -O=q9_hk"
# Ein frisch geschriebenes Modul hat NUR Besitzer-Lesen/Schreiben -- OS-9
# startet es so nicht. Die Form steht im Benutzerhandbuch (68k_use.pdf,
# "Examining File Attributes with attr"): "attr <datei> -e -pe", und ein
# vorangestelltes n ENTFERNT eine Berechtigung. Am Host macht ToolShed
# dasselbe, dort ist es in allen Pruefstaenden vermerkt.
stufe 6 "attr q9_hk -e -pe"
# MIT PFAD aufrufen: die Shell sucht Kommandos im AUSFUEHRUNGSverzeichnis
# (/dd/CMDS), nicht im Datenverzeichnis -- ohne Schraegstrich kommt
# "q9_hk: command not found", und das sagt nichts ueber das Modul.
stufe 7 "/dd/HOME/ROOT/q9_hk"

catch { send "\x1d" }
catch { expect eof }
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$WORK/img.hda|" \
	-e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log"
for n in 1 2 3 4 5 6 7; do
	grep -q "KETTE: STUFE $n ZU ENDE" "$WORK/run.log" || {
		echo "  Stufe $n nicht durchgelaufen:"
		grep "KETTE:" "$WORK/run.log" | sed 's/^/    /'
		tr -d '\r' < "$WORK/run.log" | tail -14 | sed 's/^/    /'
		exit 1; }
done
echo "  alle sieben Stufen durchgelaufen"

echo "== 5/6 das auf dem Ziel gebaute Modul zurueckholen =="
pkill -f "q9.exe.*$WORK/img.hda" 2>/dev/null || true
sleep 2
"$OS9" copy -r "$WORK/img.hda,/HOME/ROOT/q9_hk" "$WORK/ziel/q9_hk" >/dev/null 2>&1 ||
	die "Modul nicht aus dem Abbild lesbar"
echo "  $(wc -c < "$WORK/ziel/q9_hk" | tr -d ' ') Byte"

echo "== 6/6 pruefen =="
fail=0
if cmp -s "$WORK/host/q9_hk" "$WORK/ziel/q9_hk"; then
	echo "  Modul BYTEIDENTISCH zum Hostlauf"
else
	echo "  Modul WEICHT AB (Host $(wc -c < "$WORK/host/q9_hk" | tr -d ' '), Ziel $(wc -c < "$WORK/ziel/q9_hk" | tr -d ' ') Byte)"
	cmp "$WORK/host/q9_hk" "$WORK/ziel/q9_hk" | head -2 | sed 's/^/    /'
	fail=1
fi
# Und die Ausgabe des auf dem Ziel gebauten Programms -- die Erwartungswerte
# sind dieselben wie in test/hello68k.sh, also aus dem clib-Gegenlauf.
n=0
for zeile in "Hallo Welt" "42 -7 0" "puts geht" "gelesen 11: Hallo Datei" \
             "str 11 Datei 0 -1 1" "sprintf 42|xy|A|abc" "realloc 1 7" \
             "strcat abcde" "tok 2 drei" "strtol 42 -7 255" "memcpy 65 66 46" \
             "fputs ohne Umbruch!"; do
	if tr -d '\r' < "$WORK/run.log" | grep -qxF "$zeile"; then
		n=$((n + 1))
	else
		echo "  FEHLT in der Ausgabe: $zeile"; fail=1
	fi
done
echo "  $n von 12 Stichproben in der Ausgabe"

echo
if [ $fail -eq 0 ]; then
	echo "DIE KETTE LAEUFT AUF DEM ZIEL: qcpp, qcc, qcc_backend, qr68 und ql68"
	echo "haben auf dem 68030 aus hello.c ein Modul gebaut, das dort laeuft --"
	echo "byteidentisch zu dem, was der Host aus derselben Quelle baut."
else
	echo "ROT (Log: $WORK/run.log)"
fi
exit $fail
