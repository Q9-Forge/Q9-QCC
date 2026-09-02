#!/usr/bin/env bash
# Regressionssuite fuer qcpp.
#
# Die Faelle stehen absichtlich IN diesem Skript und nicht als Dateipaare
# daneben: Eingabe und Sollwert direkt untereinander zu sehen ist bei einem
# Praeprozessor die halbe Diagnose.
#
# Verglichen wird normalisiert (Zwischenraum und Umbrueche zusammengefasst) --
# ausser bei den Faellen mit t_exact, die genau die Zeilentreue pruefen.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
QCPP=build/qcpp
[ -x "$QCPP" ] || { echo "FEHLER: $QCPP fehlt -- vorher 'make'"; exit 2; }

TMP="$(mktemp -d /tmp/qcpp-test.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

ok=0
fail=0
failed_names=""

t_src() { cat > "$TMP/in.c"; }
t_exp() { cat > "$TMP/exp.txt"; }
t_file() { cat > "$TMP/$1"; }

norm() { tr '\t\n' '  ' < "$1" | tr -s ' ' | sed 's/^ //; s/ $//'; }

t_report() {
	local name="$1" verdict="$2"
	if [ "$verdict" = ok ]; then
		ok=$((ok + 1))
		printf '  ok    %s\n' "$name"
	else
		fail=$((fail + 1))
		failed_names="$failed_names
    $name"
		printf 'FAIL    %s\n' "$name"
	fi
}

# t_run <name> [optionen...]   -- normalisierter Vergleich
t_run() {
	local name="$1"; shift
	if ! "$QCPP" "$@" "$TMP/in.c" "$TMP/out.i" > "$TMP/msg.txt" 2>&1; then
		t_report "$name" fail
		printf '        qcpp brach ab: %s\n' "$(cat "$TMP/msg.txt")"
		return
	fi
	if [ "$(norm "$TMP/out.i")" = "$(norm "$TMP/exp.txt")" ]; then
		t_report "$name" ok
	else
		t_report "$name" fail
		printf '        soll: %s\n' "$(norm "$TMP/exp.txt")"
		printf '        ist:  %s\n' "$(norm "$TMP/out.i")"
	fi
}

# t_exact <name> [optionen...]  -- byteweiser Vergleich
t_exact() {
	local name="$1"; shift
	if ! "$QCPP" "$@" "$TMP/in.c" "$TMP/out.i" > "$TMP/msg.txt" 2>&1; then
		t_report "$name" fail
		printf '        qcpp brach ab: %s\n' "$(cat "$TMP/msg.txt")"
		return
	fi
	if cmp -s "$TMP/out.i" "$TMP/exp.txt"; then
		t_report "$name" ok
	else
		t_report "$name" fail
		diff -u "$TMP/exp.txt" "$TMP/out.i" | sed 's/^/        /' | head -20
	fi
}

# t_fail <name> <erwarteter-textausschnitt> [optionen...]
t_fail() {
	local name="$1" want="$2"; shift 2
	if "$QCPP" "$@" "$TMP/in.c" "$TMP/out.i" > "$TMP/msg.txt" 2>&1; then
		t_report "$name" fail
		printf '        qcpp lief durch, erwartet war ein Abbruch\n'
		return
	fi
	if grep -qF "$want" "$TMP/msg.txt"; then
		t_report "$name" ok
	else
		t_report "$name" fail
		printf '        erwartet: %s\n' "$want"
		printf '        gemeldet: %s\n' "$(cat "$TMP/msg.txt")"
	fi
}

echo "=== qcpp-Regressionssuite ==="

# --------------------------------------------------------------- Makros ----
t_src <<'EOF'
#define N 42
int x = N;
EOF
t_exp <<'EOF'
int x = 42;
EOF
t_run "01 Objektmakro"

t_src <<'EOF'
#define ADD(a,b) ((a)+(b))
int x = ADD(1,2);
EOF
t_exp <<'EOF'
int x = ((1)+(2));
EOF
t_run "02 Funktionsmakro"

t_src <<'EOF'
#define A B
#define B C
#define C 7
int x = A;
EOF
t_exp <<'EOF'
int x = 7;
EOF
t_run "03 Kette von Makros"

t_src <<'EOF'
#define f(x) x + f(x)
int y = f(1);
EOF
t_exp <<'EOF'
int y = 1 + f(1);
EOF
t_run "04 Selbstbezug bleibt stehen (blaue Farbe)"

t_src <<'EOF'
#define STR(x) #x
char *a = STR(hallo welt);
char *b = STR("mit \" drin");
EOF
t_exp <<'EOF'
char *a = "hallo welt";
char *b = "\"mit \\\" drin\"";
EOF
t_run "05 # macht eine Zeichenkette"

t_src <<'EOF'
#define CAT(a,b) a##b
#define N 4
int CAT(foo,bar) = CAT(N,2);
EOF
t_exp <<'EOF'
int foobar = N2;
EOF
t_run "06 ## verkettet roh: CAT(N,2) ist N2, nicht 42"

t_src <<'EOF'
#define STR(x) #x
#define XSTR(x) STR(x)
#define VAL 42
char *a = STR(VAL);
char *b = XSTR(VAL);
EOF
t_exp <<'EOF'
char *a = "VAL";
char *b = "42";
EOF
t_run "07 Doppelexpansion vor #"

t_src <<'EOF'
#define EMPTY
#define P(x) [x]
int a = P();
int b = P(EMPTY);
EOF
t_exp <<'EOF'
int a = [];
int b = [];
EOF
t_run "08 leeres Argument"

# Die Ersetzung traegt die Zeile des AUFRUFS (Zeile 2), das ";" steht in der
# Quelle aber auf Zeile 3 -- und dort bleibt es auch, damit die Zeilentreue
# erhalten bleibt. Der Umbruch dazwischen ist also gewollt.
t_src <<'EOF'
#define ADD(a,b) a+b
int x = ADD(1,
            2);
EOF
printf '\nint x = 1+2\n;\n' > "$TMP/exp.txt"
t_exact "09 Aufruf ueber Zeilengrenze"

t_src <<'EOF'
#define LONG 1 + \
             2
int x = LONG;
EOF
t_exp <<'EOF'
int x = 1 + 2;
EOF
t_run "10 Zeilenfortsetzung im Rumpf"

t_src <<'EOF'
#define X 1
#undef X
#ifdef X
falsch
#endif
int x = 2;
EOF
t_exp <<'EOF'
int x = 2;
EOF
t_run "11 #undef"

t_src <<'EOF'
#define M(x) x
int a = M
;
EOF
t_exp <<'EOF'
int a = M ;
EOF
t_run "12 Funktionsmakro ohne ( bleibt Name"

# ---------------------------------------------------------------- #if -----
t_src <<'EOF'
#if 1 + 2 * 3 == 7
ja
#endif
#if (1 + 2) * 3 == 7
nein
#endif
#if 8 >> 2 == 2 && !0
auch_ja
#endif
EOF
t_exp <<'EOF'
ja
auch_ja
EOF
t_run "13 #if rechnet mit Vorrang"

t_src <<'EOF'
#define A 1
#if defined A && defined(A) && !defined B
ja
#endif
#if defined(B)
nein
#endif
EOF
t_exp <<'EOF'
ja
EOF
t_run "14 defined in beiden Schreibweisen"

t_src <<'EOF'
#if 0
#if 1
tief_nein
#endif
nein
#elif 1
ja
#else
auch_nein
#endif
EOF
t_exp <<'EOF'
ja
EOF
t_run "15 #elif nach geschachteltem #if"

t_src <<'EOF'
#if 0
das ist "kein gueltiges C und auch keine geschlossene Zeichenkette
#nonsense
#endif
int x = 1;
EOF
t_exp <<'EOF'
int x = 1;
EOF
t_run "16 uebersprungener Zweig wird nicht ausgewertet"

t_src <<'EOF'
#if UNBEKANNT
nein
#endif
#if UNBEKANNT == 0
ja
#endif
EOF
t_exp <<'EOF'
ja
EOF
t_run "17 unbekannter Name ist 0 (C89 3.8.1)"

t_src <<'EOF'
#if 'A' == 65 && '\n' == 10
ja
#endif
EOF
t_exp <<'EOF'
ja
EOF
t_run "18 Zeichenkonstanten im #if"

t_src <<'EOF'
#if 0x10 == 16 && 010 == 8 && 1u == 1 && 2L == 2
ja
#endif
EOF
t_exp <<'EOF'
ja
EOF
t_run "19 Zahlenformate und Suffixe im #if"

t_src <<'EOF'
#if 1 ? 2 : (1/0)
ja
#endif
EOF
t_exp <<'EOF'
ja
EOF
t_run "20 Bedingungsoperator"

# ------------------------------------------------------- Vorbelegungen ----
t_src <<'EOF'
#ifdef _OSK
osk
#endif
#ifdef _UCC
ucc
#endif
#ifdef _OS9000
os9000
#endif
#ifdef __STDC__
stdc
#endif
EOF
t_exp <<'EOF'
osk
ucc
EOF
t_run "21 Vorbelegung wie xcc (gemessen)"

t_src <<'EOF'
#ifdef __STDC__
stdc
#endif
#ifdef _OSK
osk
#endif
EOF
t_exp <<'EOF'
stdc
osk
EOF
t_run "22 -ansi setzt __STDC__" -ansi

t_src <<'EOF'
#ifdef _OSK
osk
#endif
sonst
EOF
t_exp <<'EOF'
sonst
EOF
t_run "23 -nopredef nimmt _OSK weg" -nopredef

t_src <<'EOF'
#ifdef EIGEN
int a = EIGEN;
#endif
#ifdef WEG
falsch
#endif
EOF
t_exp <<'EOF'
int a = 5;
EOF
t_run "24 -D und -U" -DEIGEN=5 -DWEG -UWEG

# ----------------------------------------------------------- #include -----
t_file "kopf.h" <<'EOF'
#ifndef KOPF_H
#define KOPF_H
int aus_dem_kopf(void);
#endif
EOF
t_src <<'EOF'
#include "kopf.h"
#include "kopf.h"
int main(void) { return aus_dem_kopf(); }
EOF
t_exp <<'EOF'
int aus_dem_kopf(void);
int main(void) { return aus_dem_kopf(); }
EOF
t_run "25 #include mit Wiederholungsschutz" "-I$TMP"

t_file "spitz.h" <<'EOF'
int spitz;
EOF
t_src <<'EOF'
#include <spitz.h>
int x;
EOF
t_exp <<'EOF'
int spitz;
int x;
EOF
t_run "26 #include <...> ueber -I" "-I$TMP"

t_src <<'EOF'
#ifdef NIE
#include "gibt-es-nicht.h"
#endif
int x;
EOF
t_exp <<'EOF'
int x;
EOF
t_run "27 #include im ausgeschalteten Zweig wird nicht geoeffnet"

t_file "mit_if.h" <<'EOF'
#ifdef DA
int im_kopf;
#endif
EOF
t_src <<'EOF'
#ifdef DA
#include "mit_if.h"
#endif
int x;
EOF
t_exp <<'EOF'
int im_kopf;
int x;
EOF
t_run "28 #include innerhalb eines #if-Zweiges" -DDA "-I$TMP"

# ---------------------------------------------------------------- #asm ----
t_src <<'EOF'
#define ASMVAL 42
#asm
 move.l  #ASMVAL,d0     /* C-Kommentar faellt weg */
 nop                    * asm-Kommentar bleibt
#endasm
int c = 1;
EOF
t_exp <<'EOF'
#asm
move.l #42,d0
nop * asm-Kommentar bleibt
#endasm
int c = 1;
EOF
t_run "29 #asm: Makro expandiert, move.l bleibt zusammen"

t_src <<'EOF'
#asm
 nop
#endasm
int c = 1;
EOF
t_exp <<'EOF'
nop
int c = 1;
EOF
t_run "30 -asm-strip wie xcc -pp" -asm-strip

t_src <<'EOF'
#asm
 dc.l $0515B007
#endasm
EOF
t_exp <<'EOF'
#asm
dc.l $0515B007
#endasm
EOF
t_run "31 #asm: Assemblerzahl bleibt ein Stueck"

# ------------------------------------------------------------ Sonstiges ---
t_src <<'EOF'
int a/**/b;
int c/* mehrzeilig
   weiter */d;
EOF
t_exp <<'EOF'
int a b; int c d;
EOF
t_run "32 Kommentar trennt Tokens"

t_src <<'EOF'
#pragma once
#pragma warning ( disable : 4114)
int x;
EOF
t_exp <<'EOF'
#pragma once
#pragma warning ( disable : 4114)
int x;
EOF
t_run "33 #pragma geht unveraendert durch"

t_src <<'EOF'
int a = __LINE__;
#define L __LINE__
int b = L;
EOF
t_exp <<'EOF'
int a = 1;
int b = 3;
EOF
t_run "34 __LINE__ zeigt auf die Verwendung"

t_src <<'EOF'
char *d = __DATE__;
char *t = __TIME__;
EOF
t_exp <<'EOF'
char *d = "Jan  1 1970";
char *t = "00:00:00";
EOF
t_run "35 __DATE__/__TIME__ sind fest (Reproduzierbarkeit)"

t_src <<'EOF'
char *d = __DATE__;
EOF
t_exp <<'EOF'
char *d = "Sep  2 2026";
EOF
t_run "36 -fdate setzt __DATE__" "-fdate=Sep  2 2026"

# Zeilentreue: die Ausgabe muss dieselbe Zeilennummer tragen wie die Quelle,
# damit die Meldungen der naechsten Stufe auf die richtige Zeile zeigen.
t_src <<'EOF'
#define N 1
#if 0
weg
weg
#endif
int x = N;
EOF
printf '\n\n\n\n\nint x = 1;\n' > "$TMP/exp.txt"
t_exact "37 Zeilentreue: Token steht auf Zeile 6"

t_src <<'EOF'
#define N 1
#if 0
weg
#endif
int x = N;
EOF
printf '\nint x = 1;\n' > "$TMP/exp.txt"
t_exact "38 -min faltet die Luecke zusammen" -min

t_src <<'EOF'
int x = 1;
EOF
t_exp <<'EOF'
#line 1 "IN"
int x = 1;
EOF
sed -i.bak "s|IN|$TMP/in.c|" "$TMP/exp.txt"
t_run "39 -lines erzeugt eine Zeilenmarke" -lines

# ------------------------------------------------------------- Abbrueche ---
t_src <<'EOF'
#error So nicht
EOF
t_fail "40 #error bricht ab" "qcpp:"

t_src <<'EOF'
#if 1
int x;
EOF
t_fail "41 fehlendes #endif" "nicht geschlossen"

t_src <<'EOF'
#endif
EOF
t_fail "42 #endif ohne #if" "#endif ohne #if"

t_src <<'EOF'
#nonsense
EOF
t_fail "43 unbekannte Direktive" "unbekannte Direktive"

t_src <<'EOF'
#define P(a,b) a+b
int x = P(1);
EOF
t_fail "44 zu wenige Argumente" "zu wenige Argumente"

t_src <<'EOF'
#define P(a) a
int x = P(1,2);
EOF
t_fail "45 zu viele Argumente" "zu viele Argumente"

t_src <<'EOF'
#define CAT(a,b) a##b
int x = CAT(+,-);
EOF
t_fail "46 ## ergibt kein Token" "kein einzelnes Token"

t_src <<'EOF'
#if 1 +
x
#endif
EOF
t_fail "47 abgebrochener #if-Ausdruck" "#if"

t_src <<'EOF'
#if 1/0
x
#endif
EOF
t_fail "48 Division durch Null im #if" "Division durch Null"

t_src <<'EOF'
#asm
 nop
EOF
t_fail "49 #asm ohne #endasm" "#asm ohne #endasm"

t_src <<'EOF'
#include "gibt-es-wirklich-nicht.h"
EOF
t_fail "50 fehlender Header" "nicht gefunden"

echo
echo "=== Zusammenfassung ==="
echo "  $ok ok, $fail FAIL"
if [ "$fail" -gt 0 ]; then
	echo "  fehlgeschlagen:$failed_names"
	exit 1
fi
exit 0
