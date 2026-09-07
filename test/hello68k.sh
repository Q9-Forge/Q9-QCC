#!/usr/bin/env bash
# Der Pruefstein von qclib: ein Programm, das NUR gegen qclib gebunden
# ist, muss auf echtem 68030 dasselbe ausgeben wie gegen Microwares clib.
#
# Byteidentitaet taugt hier nicht als Pruefstein -- fuer clib gibt es
# keine Quellen, also auch keine gemeinsame Eingabe (s. README). Es zaehlt
# das VERHALTEN, und das laesst sich nur im Emulator messen.
#
# Fallen, die hier Zeit gekostet haben (aus der QCC-Arbeit uebernommen):
#   - auf einer Image-KOPIE arbeiten; ToolShed schreibt an einem laufenden
#     Emulator vorbei direkt in die Imagedatei, und auf dem Mac laufen oft
#     mehrere Emulatoren.
#   - "copy -r" setzt KEIN e-Attribut -> OS-9 startet das Modul nicht
#     (Fehler 214). Danach os9 attr -e noetig.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
: "${FORGE:=$(cd .. && pwd)}"
: "${QCC:=$FORGE/Q9-QCC}"
: "${FLUX:=$FORGE/Q9-Flux}"
: "${MWOS:=/Volumes/SSD1TB/projects/MWOS}"
: "${IMG_SRC:=$FLUX/local_images/OS9SYS.qcc-xcc-test.hda}"

die() { echo "FEHLER: $*" >&2; exit 2; }
[ -f "$REPO/build/qclib.l" ] || die "build/qclib.l fehlt -- vorher 'make'"
[ -f "$IMG_SRC" ]            || die "Image fehlt: $IMG_SRC"
[ -x "$FLUX/build/macos/q9.exe" ] || die "Emulator fehlt (in Q9-Flux 'make host')"

WORK=/tmp/qclib-68k
rm -rf "$WORK"; mkdir -p "$WORK"
# CoW-Klon: kostenlos, und der Lauf fasst das Originalimage nicht an.
cp -c "$IMG_SRC" "$WORK/img.hda" 2>/dev/null || cp "$IMG_SRC" "$WORK/img.hda"

MWOS_UNIX="$MWOS"
# shellcheck disable=SC1091
source "$MWOS/tools/macos/env/os9-toolchain.sh" >/dev/null 2>&1 ||
	die "OS-9-Toolchain nicht ladbar"
MWOS="$MWOS_UNIX"

echo "== 1/4 Modul binden (eigener Binder, nur gegen qclib) =="
cp "$REPO/build/hello.r" "$REPO/build/q9_cstart.r" "$REPO/build/qclib.l" "$WORK/" ||
	die "Eingaben fehlen -- vorher 'make'"
# Gebunden wird mit dem EIGENEN Binder. Seit ql68 die Bibliothekssuche
# beherrscht, braucht es dafuer kein l68 mehr -- und damit steckt in der
# Kette vom Praeprozessor bis zum Modul kein fremdes Werkzeug mehr.
QL68="${QL68:-$FORGE/Q9-ql68/build/ql68}"
[ -x "$QL68" ] || die "ql68 fehlt: $QL68"
"$QL68" -a "$WORK/q9_cstart.r" "$WORK/hello.r" -l="$WORK/qclib.l" \
	-M=8K "-O=$WORK/q9_hello" >"$WORK/link.log" 2>&1
[ -f "$WORK/q9_hello" ] || { sed 's/^/    /' "$WORK/link.log" | head -10; die "ql68"; }
echo "  ok ($(wc -c < "$WORK/q9_hello" | tr -d ' ') Byte, gebunden mit ql68)"

echo "== 2/4 ins Image =="
"$MWOS_TOOLSHED_OS9" copy -r "$WORK/q9_hello" "$WORK/img.hda,/CMDS/q9_hello" >/dev/null 2>&1 ||
	die "ToolShed-copy"
# Ohne e-Attribut startet OS-9 das Modul nicht (Fehler 214).
"$MWOS_TOOLSHED_OS9" attr "$WORK/img.hda,/CMDS/q9_hello" -e -w -r -pe -pr >/dev/null 2>&1
echo "  ok"

echo "== 3/4 im Emulator ausfuehren =="
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
send -s "/dd/CMDS/q9_hello\r"
# AUF DIE EIGENE AUSGABE WARTEN, NICHT AUF DEN PROMPT: der steht nach dem
# Login noch im Puffer und trifft sofort -- dann killt der Escape das Modul,
# bevor es etwas ausgibt. Genau das ist am 2026-09-07 einmal passiert (0 von
# 14 Zeilen, beim Wiederholen wieder gruen). Die Schlussmarke kommt aus
# hello.c.
expect {
    -re {hello fertig}   { }
    -re {Stack Overflow} { send_log "\nTEST: STACK OVERFLOW\n" }
    -re {PMMU}           { send_log "\nTEST: PMMU\n" }
    eof                  { send_log "\nTEST: EMULATOR WEG\n"; exit 1 }
    timeout              { send_log "\nTEST: TIMEOUT\n"; exit 1 }
}
send "\x1d"
expect eof
EOF
sed -i.bak -e "s|LOGFILE|$WORK/run.log|" -e "s|IMAGE|$WORK/img.hda|" \
	-e "s|MWOSDIR|$MWOS|" "$WORK/run.exp" && rm -f "$WORK/run.exp.bak"
( cd "$FLUX" && expect -f "$WORK/run.exp" >/dev/null 2>&1 )
[ -f "$WORK/run.log" ] || die "kein Emulator-Log"
echo "  ok"

echo "== 4/4 Ausgabe pruefen =="
ok=0
bad=0
# WICHTIG: auf die GANZE Zeile pruefen, nicht auf ein Vorkommen. Ein
# blosses "grep -F" findet den Text auch mitten in einer Zeile -- daran
# ist der fehlende Zeilenumbruch von puts durchgerutscht, denn
# "puts gehtgeschrieben 11" enthaelt beide gesuchten Texte. Der Gegenlauf
# gegen clib hat es gefunden, dieser Test nicht.
pruefe() {
	if tr -d "\r" < "$WORK/run.log" | grep -qxF "$1"; then
		echo "  ok      $1"
		ok=$((ok + 1))
	else
		echo "  FEHLT   $1"
		bad=$((bad + 1))
	fi
}
# DIESE ERWARTUNGSWERTE SIND NICHT AUSGEDACHT, sondern aus dem Gegenlauf
# gegen Microwares clib uebernommen (test/vsclib.sh, alle Zeilen
# zeichengleich). Von Hand hingeschriebene Sollwerte koennen denselben
# Denkfehler enthalten wie der Code -- so ist der fehlende Zeilenumbruch
# von puts durchgerutscht. Neue Faelle also erst dort pruefen.
pruefe "Hallo Welt"
pruefe "42 -7 0"
pruefe "hex ff, Zeichen A, Prozent %"
pruefe "puts geht"
pruefe "geschrieben 11"
pruefe "gelesen 11: Hallo Datei"
pruefe "str 11 Datei 0 -1 1"
pruefe "strchr0 1"
pruefe "sprintf 42|xy|A|abc"
pruefe "langzahl -123456"
pruefe "realloc 1 7"
pruefe "rueckgaben 1 33"
pruefe "zurueck 33: fp 7 sieben 7"
pruefe "fputs ohne Umbruch!"
pruefe "hello fertig"
if grep -q "TEST: " "$WORK/run.log"; then
	echo "  Abbruch im Emulator:"
	grep "TEST: " "$WORK/run.log" | sed 's/^/    /'
	bad=$((bad + 1))
fi
echo
echo "  $ok von 15 Zeilen richtig"
[ "$bad" -eq 0 ] || echo "  (Log: $WORK/run.log)"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
