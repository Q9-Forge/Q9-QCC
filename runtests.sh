#!/bin/sh
#================================================================================
# runtests.sh -- komplette Regressionssuite fuer das ebnf-Projekt
# Aufruf: ./runtests.sh   (baut zuerst, dann alle Grammatiken + TESTS-Bloecke)
#================================================================================
cd "$(dirname "$0")" || exit 1
mkdir -p build
clang++ -std=c++17 -Wall -Wno-format-security -o build/ebnf Source/ebnf.cpp Source/codegen.cpp || exit 1

fail=0

# 1) Test-Grammatiken mit TESTS-Bloecken (Stack-Maschine gegen Erwartung)
for g in seqtest alttest blocktest opttest reptest numtest rangetest optalt multalt actiontest calcexpr actionrollback; do
	out=$(build/ebnf "Test/$g" 2>&1)
	mm=$(echo "$out" | grep -c MISMATCH)
	pass=$(echo "$out" | grep "PASS ===")
	if [ "$mm" -ne 0 ]; then echo "FAIL  $g:"; echo "$out" | grep MISMATCH; fail=1
	else echo "ok    $g: $pass"; fi
done

# 2) Linksrekursions-Erkennung (muss anschlagen)
for g in leftrec leftrec2 opt_leftrec; do
	if build/ebnf "Test/$g" 2>&1 | grep -q LINKSREKURSION; then echo "ok    $g: Linksrekursion erkannt"
	else echo "FAIL  $g: Linksrekursion NICHT erkannt"; fail=1; fi
done

# 3) Grosse Beispiel-Grammatiken (duerfen keine Linksrekursion melden; bekannte
#    "undefinierte Regel"-Zahlen aus auskommentierten Low-Level-Regeln als Referenz;
#    oberon0 ist seit 2026-07-19 vollstaendig definiert -> 0.
#    modula2 36 seit dem Kommentarfilter-Fix (Zeilen mit gequotetem "#" wurden vorher
#    KOMPLETT verschluckt): modula2s Relation-Regel parst jetzt (-1).
#    oberon07 ist seit 2026-07-20 ebenfalls vollstaendig definiert -> 0 (relation-Regel
#    referenziert IN/IS inzwischen gequotet als TS-Literale statt als undefinierte Regeln).
for gc in ebnf:28 java:33 modula2:36 oberon0:0 oberon07:0; do
	g=${gc%:*}; expect=${gc#*:}
	out=$(build/ebnf "Data/$g" 2>&1)
	errs=$(echo "$out" | grep -c FEHLER)
	lr=$(echo "$out" | grep -c LINKSREKURSION)
	if [ "$lr" -ne 0 ]; then echo "FAIL  $g: falsche Linksrekursions-Meldung"; fail=1
	elif [ "$errs" -ne "$expect" ]; then echo "FAIL  $g: $errs FEHLER (erwartet $expect)"; fail=1
	else echo "ok    $g: $errs bekannte Meldungen"; fi
done

# 4) Codegen-Validierung: erzeugten C-Parser kompilieren und die TESTS-Bloecke der
#    Arbeitsdatei GEGEN DEN ERZEUGTEN PARSER laufen lassen (semantischer Zwilling
#    des 68k-Codes -- gleicher AST-Walker, gleiche Struktur). actiontest bewusst
#    NICHT hier drin: seine ROUTINE C schreibt absichtlich zusaetzliche Ausgabe vor
#    OK/FAIL (siehe 7b) -- der generische Vergleich hier erwartet reines OK/FAIL.
for g in seqtest alttest blocktest opttest reptest numtest rangetest optalt multalt; do
	if [ ! -f "Test/${g}_p.c" ]; then echo "FAIL  codegen $g: Test/${g}_p.c fehlt"; fail=1; continue; fi
	if ! cc -w -o "build/${g}_p" "Test/${g}_p.c"; then echo "FAIL  codegen $g: C-Parser kompiliert nicht"; fail=1; continue; fi
	mm=0; n=0
	while IFS= read -r line; do
		t=${line#TEST \"}
		exp=${line##* }
		inp=${t%\" *}
		got=$("build/${g}_p" "$inp")
		n=$((n+1))
		if [ "$got" != "$exp" ]; then
			echo "FAIL  codegen $g: Eingabe \"$inp\" erwartet $exp, erzeugter Parser sagt $got"
			mm=1; fail=1
		fi
	done <<EOT
$(grep '^TEST ' "Test/$g.lextab")
EOT
	[ $mm -eq 0 ] && echo "ok    codegen $g: $n Tests gegen erzeugten C-Parser"
done

# 5) Semantik-Vorteil des Codegen-Pfads: die Tabelle akzeptiert bei
#    s = [ "-" ] "a" | "b" .  die Eingabe "-b" faelschlich (gewarnte Grenze) --
#    der ERZEUGTE Parser muss sie ablehnen (echtes Backtracking pro Alternative).
if cc -w -o build/ambig_p Test/ambig_p.c 2>/dev/null; then
	if [ "$(build/ambig_p -b)" = "FAIL" ] && [ "$(build/ambig_p -a)" = "OK" ]; then
		echo "ok    codegen ambig: '-b' korrekt abgelehnt, '-a' erkannt"
	else
		echo "FAIL  codegen ambig: Backtracking-Semantik verletzt"; fail=1
	fi
else
	echo "FAIL  codegen ambig: C-Parser fehlt/kompiliert nicht"; fail=1
fi

# 5b) LEXER-Modus: WHITESPACE + COMMENT LINE muessen im erzeugten C-Parser UND im
#     simulierten 68k-Code gleich funktionieren (Test/lexcomment: num = digit {digit}
#     mit TOKEN digit, Kommentar "//").
if cc -w -o build/lexcomment_p Test/lexcomment_p.c 2>/dev/null; then
	lcfail=0
	for r in "build/lexcomment_p" "python3 tools/s68sim.py Test/lexcomment.s68"; do
		[ "$($r '1 2 3' 2>&1)" = "OK" ] || { echo "FAIL  lexer ($r): '1 2 3'"; lcfail=1; }
		[ "$($r "$(printf '1 // foo\n2 3')" 2>&1)" = "OK" ] || { echo "FAIL  lexer ($r): Kommentarzeile"; lcfail=1; }
		[ "$($r '1 x 3' 2>&1)" = "FAIL" ] || { echo "FAIL  lexer ($r): '1 x 3' faelschlich OK"; lcfail=1; }
	done
	if [ $lcfail -eq 0 ]; then echo "ok    lexer lexcomment: WHITESPACE+COMMENT in C und 68k identisch"
	else fail=1; fi
else
	echo "FAIL  lexer lexcomment: C-Parser fehlt/kompiliert nicht"; fail=1
fi

# 5c) LEXER-Modus: MEHRERE gleichzeitige Kommentar-Marker (docs/ARCHITEKTUR.md §7.1,
#     Nutzerwunsch): Test/multicomment konfiguriert "#" UND "//" als Zeilenkommentar
#     sowie "/* */" UND "(* *)" als Blockkommentar gleichzeitig -- alle vier muessen
#     in C UND im simulierten 68k-Code funktionieren, unquotiertes "x" bleibt ein Fehler.
if cc -w -o build/multicomment_p Test/multicomment_p.c 2>/dev/null; then
	mcfail=0
	for r in "build/multicomment_p" "python3 tools/s68sim.py Test/multicomment.s68"; do
		[ "$($r '1 2 3' 2>&1)" = "OK" ] || { echo "FAIL  multicomment ($r): '1 2 3'"; mcfail=1; }
		[ "$($r "$(printf '1 # Raute\n2 3')" 2>&1)" = "OK" ] || { echo "FAIL  multicomment ($r): '#'-Kommentar"; mcfail=1; }
		[ "$($r "$(printf '1 // Slash\n2 3')" 2>&1)" = "OK" ] || { echo "FAIL  multicomment ($r): '//'-Kommentar"; mcfail=1; }
		[ "$($r '1 /* C-Block */ 2 3' 2>&1)" = "OK" ] || { echo "FAIL  multicomment ($r): '/* */'-Block"; mcfail=1; }
		[ "$($r '1 (* Wirth-Block *) 2 3' 2>&1)" = "OK" ] || { echo "FAIL  multicomment ($r): '(* *)'-Block"; mcfail=1; }
		[ "$($r '1 x 3' 2>&1)" = "FAIL" ] || { echo "FAIL  multicomment ($r): '1 x 3' faelschlich OK"; mcfail=1; }
	done
	if [ $mcfail -eq 0 ]; then echo "ok    lexer multicomment: 4 gleichzeitige Kommentar-Marker in C und 68k identisch"
	else fail=1; fi
else
	echo "FAIL  lexer multicomment: C-Parser fehlt/kompiliert nicht"; fail=1
fi

# 6) Schutz gegen eine Endlosschleife im generierten Parser: Der Rumpf einer
# Wiederholung darf nicht ohne Eingabe erfolgreich sein. Der Generator muss die
# Ausgabe bewusst verweigern statt einen haengenden C-/68k-Parser zu erzeugen.
out=$(build/ebnf "Test/nullable_repeat" 2>&1)
if echo "$out" | grep -q "Wiederholung hat einen leeren Rumpf"; then
	echo "ok    codegen nullable_repeat: leere Wiederholung abgelehnt"
else
	echo "FAIL  codegen nullable_repeat: leere Wiederholung nicht erkannt"; fail=1
fi

# 7) Nutzertext fuer spaetere semantische Aktionen ist Teil der Arbeitsdatei und
# darf beim Neu-Erzeugen nicht verloren gehen oder vom EBNF-Parser interpretiert werden.
build/ebnf "Test/usercode" >/dev/null 2>&1
if grep -Fq 'ACTION C after s { frontend_emit_literal("a"); }' "Test/usercode.lextab" \
	&& grep -Fq 'ACTION M68K after s { bsr frontend_emit_literal_a }' "Test/usercode.lextab"; then
	echo "ok    arbeitsdatei usercode: NUTZER-CODE unveraendert erhalten"
else
	echo "FAIL  arbeitsdatei usercode: NUTZER-CODE verloren/verfaelscht"; fail=1
fi

# 7b) ACTION/ROUTINE-Mechanismus (docs/ARCHITEKTUR.md §9): Test/actiontest definiert
#     "ACTION AFTER number CALL got_number" + ROUTINE C/M68K got_number im
#     [NUTZER-CODE]-Block. Der generierte C-Zwilling muss die Routine beim
#     Regelerfolg WIRKLICH aufrufen (start/end = erkannter Text); der 68k-Code
#     muss denselben bsr fehlerfrei assemblieren (vasm-Check unten prueft das separat).
if cc -w -o build/actiontest_p Test/actiontest_p.c 2>/dev/null; then
	got=$(build/actiontest_p 123)
	if [ "$got" = "$(printf 'ACTION got_number: 123\nOK')" ]; then
		echo "ok    action actiontest: ROUTINE C wird mit korrektem Text aufgerufen"
	else
		echo "FAIL  action actiontest: unerwartete Ausgabe: $got"; fail=1
	fi
else
	echo "FAIL  action actiontest: Test/actiontest_p.c fehlt/kompiliert nicht"; fail=1
fi

# 7c) ACTION/ROUTINE mit ECHTER Wertberechnung (nicht nur Seiteneffekt-Text): Test/calcexpr
#     ist ein Operator-Praezedenz-Ausdruck (expr = term {addop term}. term = factor {mulop
#     factor}.), die Aktionen fuehren einen globalen Werte-Stack in den ROUTINE-C-Koerpern
#     (kein Wertrueckgabekanal im Mechanismus selbst, siehe ARCHITEKTUR.md §9.5). Bestaetigt
#     Praezedenz (2+3*4=14) UND Linksassoziativitaet (10-2-3=5, nicht 11). Deckte dabei einen
#     eigenstaendigen, vorbestehenden Bug im TABELLEN-Generator auf (rule()-Regelabschluss
#     prüfte nur die letzte Tabellenzeile auf offene Vorwaertsreferenzen statt die ganze
#     Regel -- siehe ebnf.cpp rule(), gefixt).
if cc -w -o build/calcexpr_p Test/calcexpr_p.c 2>/dev/null; then
	cefail=0
	[ "$(build/calcexpr_p '2+3*4')" = "$(printf 'RESULT: 14\nOK')" ] || { echo "FAIL  action calcexpr: 2+3*4 sollte 14 ergeben"; cefail=1; }
	[ "$(build/calcexpr_p '10-2-3')" = "$(printf 'RESULT: 5\nOK')" ] || { echo "FAIL  action calcexpr: 10-2-3 sollte 5 ergeben (linksassoziativ)"; cefail=1; }
	[ "$(build/calcexpr_p '6/3+1')" = "$(printf 'RESULT: 3\nOK')" ] || { echo "FAIL  action calcexpr: 6/3+1 sollte 3 ergeben"; cefail=1; }
	if [ $cefail -eq 0 ]; then echo "ok    action calcexpr: Praezedenz+Linksassoziativitaet ueber ACTION-Werte-Stack korrekt"
	else fail=1; fi
else
	echo "FAIL  action calcexpr: Test/calcexpr_p.c fehlt/kompiliert nicht"; fail=1
fi

# 7d) Aktions-Rollback bei Backtracking (docs/ARCHITEKTUR.md §9.4): Test/actionrollback
#     (stmt = tag "1" | tag "2". tag = "T".) matcht "tag" bei "T2" zuerst innerhalb der
#     SPAETER verworfenen ersten Alternative, dann nochmal in der gewinnenden zweiten --
#     OHNE Rollback des Aktions-Logs würde note_tag faelschlich 2x statt 1x feuern.
if cc -w -o build/actionrollback_p Test/actionrollback_p.c 2>/dev/null; then
	got=$(build/actionrollback_p T2)
	if [ "$got" = "$(printf 'TAG#1\nOK')" ]; then
		echo "ok    action actionrollback: verworfene Alternative feuert Aktion NICHT dauerhaft"
	else
		echo "FAIL  action actionrollback: TAG-Zaehler falsch (Rollback fehlt): $got"; fail=1
	fi
else
	echo "FAIL  action actionrollback: Test/actionrollback_p.c fehlt/kompiliert nicht"; fail=1
fi

# 7e) Aktions-Rollback DURCH REKURSION hindurch (docs/ARCHITEKTUR.md §9.4, wichtig fuer
#     einen spaeteren oberon0-Durchlauf mit echten Klammerausdruecken): Test/actionrollback2
#     (expr2 = "(" expr2 ")" "A" | "(" expr2 ")" "B" | leaf.) matcht "leaf" innerhalb eines
#     VERSCHACHTELTEN expr2-Aufrufs zuerst in der verworfenen ersten Alternative, dann
#     nochmal in der gewinnenden zweiten. NUR der erzeugte Parser wird hier geprueft (die
#     TABELLE ist fuer dieses gemeinsame Praefix "PEG-committed" und lehnt "(x)B" bereits
#     aus einem bekannten, unabhaengigen Grund ab, siehe Test/actionrollback2.lextab).
if cc -w -o build/actionrollback2_p Test/actionrollback2_p.c 2>/dev/null; then
	got=$(build/actionrollback2_p "(x)B")
	if [ "$got" = "$(printf 'LEAF#1\nOK')" ]; then
		echo "ok    action actionrollback2: Rollback funktioniert auch durch Rekursion hindurch"
	else
		echo "FAIL  action actionrollback2: LEAF-Zaehler falsch: $got"; fail=1
	fi
else
	echo "FAIL  action actionrollback2: Test/actionrollback2_p.c fehlt/kompiliert nicht"; fail=1
fi
if command -v python3 >/dev/null; then
	got=$(python3 tools/s68sim.py Test/actionrollback2.s68 "(x)B" 2>&1)
	if [ "$got" = "OK" ]; then
		echo "ok    s68sim actionrollback2: Backtracking mit gemeinsamem Praefix im 68k-Code korrekt"
	else
		echo "FAIL  s68sim actionrollback2: '(x)B' erwartet OK, erhalten $got"; fail=1
	fi
fi

# 7f) Erster Interpreter-Test Richtung oberon0 (docs/ARCHITEKTUR.md §9.5): Test/miniOberon
#     (VAR-Deklarationen, Zuweisung, Ausdruecke MIT Variablenreferenzen, echte Symboltabelle
#     in den ROUTINE-C-Koerpern). Deckte den WICHTIGEN, unabhaengigen "entry vor fuehrendem
#     ws()"-Bug auf (siehe ARCHITEKTUR.md §9.4c) -- ohne dessen Fix waeren start/end einer
#     ACTION um das fuehrende Leerzeichen verschoben gewesen (nur bei aktivem [LEXER]-Block
#     sichtbar, calcexpr/actiontest hatten keinen).
if cc -w -o build/miniOberon_p Test/miniOberon_p.c 2>/dev/null; then
	got=$(build/miniOberon_p 'VAR x; y; z; BEGIN x := 2 + 3 * 4; y := x - 1; z := x * y END')
	exp=$(printf 'x = 14\ny = 13\nz = 182\n--- final state ---\nx = 14\ny = 13\nz = 182\nOK')
	if [ "$got" = "$exp" ]; then
		echo "ok    action miniOberon: Deklaration+Zuweisung+Variablenreferenzen korrekt berechnet"
	else
		echo "FAIL  action miniOberon: unerwartete Ausgabe:"; echo "$got"; fail=1
	fi
else
	echo "FAIL  action miniOberon: Test/miniOberon_p.c fehlt/kompiliert nicht"; fail=1
fi

# 8) Echte Sprachgrammatik: erzeugter Oberon-0-Parser (Data/oberon0_p.c) MIT
#    [LEXER]-Block (WHITESPACE + TOKEN ident/integer): richtige Programme mit
#    Leerzeichen, Wortgrenzen-Check (MODULEX ist nicht MODULE + X). Die Faelle
#    mit IF/WHILE/VAR kann ausserdem NUR der Codegen-Pfad (Backtracking pro
#    Alternative) -- die flache Tabelle bleibt committed haengen.
oberon0_cases() {
	runner=$1; name=$2; o0fail=0
	while IFS='|' read -r inp exp; do
		[ -z "$inp" ] && continue
		got=$($runner "$inp" 2>&1)
		if [ "$got" != "$exp" ]; then
			echo "FAIL  $name oberon0: \"$inp\" erwartet $exp, erhalten $got"; o0fail=1; fail=1
		fi
	done <<'EOT'
MODULE m; END m|OK
MODULE t; VAR x : INTEGER; BEGIN x := 1 END t|OK
MODULE t; BEGIN x := y + 2 * z END t|OK
MODULE t; BEGIN IF x <= 1 THEN x := 2 END END t|OK
MODULE t; BEGIN WHILE x # 0 DO x := x - 1 END END t|OK
MODULE m; (* kommentar *) END m|OK
MODULE t; BEGIN (* setze x *) x := 1 END t|OK
MODULE m; (* unterminiert END m|FAIL
MODULE m; (* a (* geschachtelt *) c *) END m|OK
MODULE m; (* a (* b *) END m|FAIL
MODULEX m; END m|FAIL
MODULE m END m|FAIL
MODULE t; BEGIN x := END t|FAIL
EOT
	[ $o0fail -eq 0 ] && echo "ok    $name oberon0: 13 Programm-Tests (LEXER-Block inkl. geschachtelter Blockkommentare)"
}
if cc -w -o build/oberon0_p Data/oberon0_p.c 2>/dev/null; then
	oberon0_cases build/oberon0_p codegen
else
	echo "FAIL  codegen oberon0: C-Parser fehlt/kompiliert nicht"; fail=1
fi

# 9) 68k-Backend end-to-end: der erzeugte Assembler-TEXT (.s68) wird von
#    tools/s68sim.py wirklich ausgefuehrt (typisierter a7-Stack: Ruecksprungadresse
#    vs. gesicherte Position -- Stack-Lecks im generierten Code = harter Sim-Fehler).
#    Gleiche TESTS-Bloecke wie beim C-Zwilling, dazu die Oberon-0-Programme.
if command -v python3 >/dev/null; then
	# actionrollback/actionrollback2 bewusst NICHT hier: leere TESTS-Bloecke (die Tabelle
	# ist fuer ihr gemeinsames Alternativen-Praefix "PEG-committed", siehe deren .lextab) --
	# werden stattdessen mit eigenen, gezielten Checks weiter unten geprueft.
	for g in seqtest alttest blocktest opttest reptest numtest rangetest optalt multalt actiontest calcexpr; do
		mm=0; n=0
		while IFS= read -r line; do
			t=${line#TEST \"}
			exp=${line##* }
			inp=${t%\" *}
			got=$(python3 tools/s68sim.py "Test/$g.s68" "$inp" 2>&1)
			n=$((n+1))
			if [ "$got" != "$exp" ]; then
				echo "FAIL  s68sim $g: Eingabe \"$inp\" erwartet $exp, 68k-Code sagt: $got"
				mm=1; fail=1
			fi
		done <<EOT
$(grep '^TEST ' "Test/$g.lextab")
EOT
		[ $mm -eq 0 ] && echo "ok    s68sim $g: $n Tests gegen simulierten 68k-Code"
	done
	oberon0_cases "python3 tools/s68sim.py Data/oberon0.s68" s68sim
else
	echo "warn  s68sim: python3 nicht gefunden -- 68k-Simulation uebersprungen"
fi

# 10) Echter Assembler: alle erzeugten .s68 muessen mit vasm (Motorola-Syntax,
#     68000) fehlerfrei assemblieren. tools/vasmm68k_mot wurde aus den Original-
#     Quellen (sun.hasenbraten.de/vasm) gebaut; fehlt das Binary (z.B. andere
#     Plattform), wird der Check uebersprungen.
if [ -x tools/vasmm68k_mot ]; then
	vfail=0; vcnt=0
	for f in Data/oberon0.s68 Test/*.s68; do
		[ -f "$f" ] || continue
		out=$(tools/vasmm68k_mot -Fbin -quiet -o /dev/null -m68000 "$f" 2>&1)
		vcnt=$((vcnt+1))
		if [ -n "$out" ]; then
			echo "FAIL  vasm $f:"; echo "$out" | head -5; vfail=1; fail=1
		fi
	done
	[ $vfail -eq 0 ] && echo "ok    vasm: $vcnt .s68-Dateien assemblieren fehlerfrei (68000)"
else
	echo "warn  vasm: tools/vasmm68k_mot fehlt -- Assembler-Check uebersprungen"
fi

# 11) OS-9/r68-Format: Data/oberon0.lextab hat "M68K OS9" im [CODEGEN]-Block ->
#     oberon0_os9.a (nam/psect/ends) muss mit der ECHTEN Microware-Toolchain
#     (r68 via Wine/MWOS) assemblieren. Fehlt Wine oder MWOS, wird uebersprungen.
WINE="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
MWOS_TMP="/Volumes/SSD1TB/projects/MWOS/TMP"
if [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && [ -f Data/oberon0_os9.a ]; then
	mkdir -p "$MWOS_TMP"
	cp Data/oberon0_os9.a "$MWOS_TMP/rtest.a"
	rm -f "$MWOS_TMP/rtest.r"
	WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\rtest.a -o=M:\\TMP\\rtest.r -q" >/dev/null 2>&1
	if [ -s "$MWOS_TMP/rtest.r" ]; then
		echo "ok    r68: oberon0_os9.a assembliert fehlerfrei (Microware r68 via Wine)"
	else
		echo "FAIL  r68: oberon0_os9.a assembliert NICHT (r68 via Wine)"; fail=1
	fi
	rm -f "$MWOS_TMP/rtest.a" "$MWOS_TMP/rtest.r"
else
	echo "warn  r68: Wine/MWOS nicht verfuegbar -- OS-9-Assembler-Check uebersprungen"
fi

# 12) Tiny-C: eigenstaendige Sprache -> Stack-IR -> tinyvm (docs/ARCHITEKTUR.md Kap.10).
#     Grammatik Data/tinyc.ebnf + [NUTZER-CODE] emittieren Stack-IR; tools/tinyvm
#     fuehrt die IR aus (Interpreter + Referenz-Orakel). Meilenstein 1: Ausdruecke,
#     lokale Variablen, putint. (Kein 68k-Backend hier -- kommt in M4.)
if command -v python3 >/dev/null 2>&1; then
	build/ebnf Data/tinyc >/dev/null 2>&1
	if cc -w -o build/tinyc_p Data/tinyc_p.c 2>/dev/null; then
		tcfail=0
		tc_check() {
			got=$(build/tinyc_p "$1" 2>/dev/null | python3 tools/tinyvm.py 2>/dev/null)
			exp=$(printf '%b' "$2")
			if [ "$got" != "$exp" ]; then
				echo "FAIL  tinyc: [$1]"
				echo "        erhalten: [$got]  erwartet: [$exp]"
				tcfail=1; fail=1
			fi
		}
		tc_check 'int main(){ putint(2 + 3 * 4); }'                        '14'
		tc_check 'int main(){ putint(10 - 2 - 3); }'                       '5'
		tc_check 'int main(){ putint((2 + 3) * 4); }'                      '20'
		tc_check 'int main(){ putint(-5 + 8); }'                           '3'
		tc_check 'int main(){ putint(20 / 3); }'                           '6'
		tc_check 'int main(){ int x; x = 2 + 3 * 4; putint(x); int y = x - 1; putint(y); }' '14\n13'
		tc_check 'int main(){ int a; int b; a = 7; b = a * a; putint(b); }' '49'
		tc_check 'int main(){ int x = 40; /* Kommentar */ putint(x + 2); }' '42'
		tc_check 'int main(){ putint(2 <= 3); putint(3 <= 3); putint(4 <= 3); }' '1\n1\n0'
		tc_check 'int main(){ int x = 3; if(x > 2) putint(11); else putint(22); }' '11'
		tc_check 'int main(){ int x = 1; if(x > 2) putint(11); else putint(22); }' '22'
		tc_check 'int main(){ int n = 5; int sum = 0; while(n > 0) { sum = sum + n; n = n - 1; } putint(sum); }' '15'
		tc_check 'int main(){ int n = 4; int sum = 0; while(n > 0) { if(n > 2) sum = sum + n; else sum = sum + 1; n = n - 1; } putint(sum); }' '9'
		tc_check 'int main(){ int sum=0; int i; for(i=0; i<5; i+=1) { sum += i; } putint(sum); }' '10'
		tc_check 'int main(){ int i=0; for(;;) { if(i>=3) break; putint(i); i+=1; } }' '0\n1\n2'
		tc_check 'int main(){ int i; int sum=0; for(i=0; i<10; i+=1) { if(i==5) break; if(i==2) continue; sum += i; } putint(sum); }' '8'
		tc_check 'int main(){ int n=0; int sum=0; do { sum += n; n += 1; } while(n<5); putint(sum); }' '10'
		tc_check 'int main(){ int i; int j; int count=0; for(i=0;i<3;i+=1){ j=0; while(j<10){ if(j==2) break; count += 1; j+=1; } } putint(count); }' '6'
		tc_check 'struct Point { int x; int y; }; int main(){ struct Point p; p.x = 3; p.y = 4; putint(p.x + p.y); }' '7'
		tc_check 'struct Point { int x; int y; }; int main(){ struct Point p; p.x = 10; p.y = p.x * 2; putint(p.y); }' '20'
		tc_check 'struct Pair { char a; char b; }; int main(){ struct Pair pr; pr.a = 65; pr.b = 66; putchar(pr.a); putchar(pr.b); }' 'AB'
		tc_check 'struct Mixed { char a; int b; char c; }; int main(){ struct Mixed m; m.a = 1; m.b = 1000; m.c = 2; putint(m.b); putchar(m.a + 64); putchar(m.c + 64); }' '1000\nAB'
		tc_check 'struct Mixed { char a; int b; }; int main(){ struct Mixed m; m.a = 1; m.b = 100; m.b += 5; m.a += 1; putint(m.b); putchar(m.a + 64); }' '105\nB'
		tc_check 'typedef int MyInt; int main(){ MyInt a = 5; MyInt b = 7; putint(a + b); }' '12'
		tc_check 'typedef int* IntPtr; int main(){ int x = 42; IntPtr p = &x; putint(*p); }' '42'
		tc_check 'typedef struct { char a; int b; } Mixed; int main(){ Mixed m; m.a = 1; m.b = 1000; putint(m.b + m.a); }' '1001'
		tc_check 'typedef struct { char a; int b; } Mixed; int main(){ putint(sizeof(struct Mixed)); }' '8'
		tc_check 'enum Color { RED, GREEN, BLUE }; int main(){ putint(RED); putint(GREEN); putint(BLUE); }' '0\n1\n2'
		tc_check 'enum Color { RED, GREEN, BLUE }; int main(){ int c = GREEN; if (c == GREEN) putint(1); else putint(0); putint(BLUE - RED); }' '1\n2'
		if build/tinyc_p 'enum A { X, Y }; enum B { X, Z }; int main(){ putint(X); }' 2>&1 | grep -q 'duplicate enum constant'; then
			echo "ok    tinyc: doppelte enum-Konstante wird diagnostiziert"
		else
			echo "FAIL  tinyc: enum-Diagnose fehlt"; tcfail=1; fail=1
		fi
		tc_check 'int main(){ putint(sizeof(int)); putint(sizeof(char)); putint(sizeof(bool)); putint(sizeof(unsigned int)); }' '4\n1\n1\n4'
		tc_check 'struct Point { int x; int y; }; int main(){ putint(sizeof(struct Point)); }' '8'
		tc_check 'int main(){ int i=5; putint(i++); putint(i); }' '5\n6'
		tc_check 'int main(){ int i=5; putint(++i); putint(i); }' '6\n6'
		tc_check 'int main(){ int i=5; putint(i--); putint(i); putint(--i); putint(i); }' '5\n4\n3\n3'
		tc_check 'int main(){ int sum=0; int i; for(i=0;i<5;i++){ sum += i; } putint(sum); }' '10'
		tc_check 'int main(){ int x=5; putint(- -x); putint(-(-x)); putint(x - -1); }' '5\n5\n6'
		tc_check 'int counter=0; int main(){ counter++; counter++; putint(counter); }' '2'
		tc_check 'int main(){ char c=65; c++; putchar(c); }' 'B'
		tc_check 'int main(){ int i=0; i++; i++; i++; putint(i); }' '3'
		tc_check 'int main(){ int x=2; switch(x){ case 1: putint(11); break; case 2: putint(22); break; default: putint(99); } }' '22'
		tc_check 'int main(){ int x=5; switch(x){ case 1: putint(11); break; case 2: putint(22); break; default: putint(99); } }' '99'
		tc_check 'int main(){ int x=5; switch(x){ case 1: putint(11); break; case 2: putint(22); } putint(1); }' '1'
		tc_check 'int main(){ int x=2; switch(x){ case 1: case 2: putint(12); break; case 3: putint(3); } }' '12'
		tc_check 'enum Color { RED, GREEN, BLUE }; int main(){ int c=GREEN; switch(c){ case RED: putint(0); break; case GREEN: putint(1); break; case BLUE: putint(2); } }' '1'
		tc_check 'int main(){ int x=-1; switch(x){ case -1: putint(99); break; case 0: putint(0); } }' '99'
		tc_check 'int main(){ int i=0; int n=0; while(i<3){ switch(i){ case 1: break; default: n+=1; } i+=1; } putint(n); }' '2'
		tc_check 'int main(){ int i=0; int sum=0; while(i<5){ i+=1; switch(i){ case 3: continue; } sum+=i; } putint(sum); }' '12'
		tc_check 'int main(){ int x=321; putint((char)x); }' '65'
		tc_check 'int main(){ char c=65; putint((int)c); }' '65'
		tc_check 'int main(){ int x=5; int y=0; putint((bool)x); putint((bool)y); }' '1\n0'
		tc_check 'int main(){ int x=5; putint((x)); putint((x)+1); }' '5\n6'
		tc_check 'int main(){ int a=2; int b=3; putint((a+b)*2); }' '10'
		tc_check 'int main(){ putint((int)sizeof(char)); }' '1'
		if build/tinyc_p 'int main(){ int x=5; int *p=&x; putint((int)p); }' 2>&1 | grep -q 'cast expects int'; then
			echo "ok    tinyc: Cast auf Pointer wird diagnostiziert"
		else
			echo "FAIL  tinyc: Cast-Diagnose fehlt"; tcfail=1; fail=1
		fi
		tc_check 'int main(){ char line[80]; putint(sizeof(line)); }' '80'
		tc_check 'int main(){ int x; putint(sizeof(x)); }' '4'
		tc_check 'int g[10]; int main(){ putint(sizeof(g)); }' '40'
		tc_check 'enum Color { RED, GREEN, BLUE }; int main(){ enum Color c; c = GREEN; putint(c); }' '1'
		tc_check 'enum Color { RED, GREEN, BLUE }; enum Color pick(int i){ if(i==0) return RED; else return GREEN; } int main(){ enum Color c = pick(1); putint(c); }' '1'
		if build/tinyc_p 'int main(){ int x; int *p=&x; putint(sizeof(p)); }' 2>&1 | grep -q 'sizeof of pointer types'; then
			echo "ok    tinyc: sizeof auf Pointer wird diagnostiziert"
		else
			echo "FAIL  tinyc: sizeof-Pointer-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ enum Nope x; }' 2>&1 | grep -q 'unknown enum'; then
			echo "ok    tinyc: unbekannter enum-Typ wird diagnostiziert"
		else
			echo "FAIL  tinyc: enum-Typ-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'struct Point { int x; int y; }; int main(){ struct Point p; p.z = 1; }' 2>&1 | grep -q 'unknown struct field'; then
			echo "ok    tinyc: unbekanntes struct-Feld wird diagnostiziert"
		else
			echo "FAIL  tinyc: struct-Feld-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'struct Mixed { int a; char b; }; int main(){ struct Mixed m; m.a = 1; putint(m.a); }' 2>&1 | grep -q 'FEHLER\|tinyc: unknown\|tinyc: struct'; then
			echo "FAIL  tinyc: gemischte Feldtypen (int+char) werden faelschlich abgelehnt"; tcfail=1; fail=1
		else
			echo "ok    tinyc: gemischte Feldtypen (int+char) werden akzeptiert"
		fi
		# Pointer-Felder sind in structField (Grammatik ohne pointerDecl) schon strukturell
		# unmoeglich; verschachtelte structs als Feld sind es aber und werden bewusst
		# abgelehnt (siehe SELFHOSTING_LUECKENLISTE.md: eigener Folgeschritt).
		if build/tinyc_p 'struct Inner { int x; }; struct Outer { struct Inner i; }; int main(){ struct Outer o; }' 2>&1 | grep -q 'struct field type not supported'; then
			echo "ok    tinyc: verschachteltes struct als Feld wird bewusst abgelehnt (eigener Folgeschritt)"
		else
			echo "FAIL  tinyc: Diagnose fuer verschachtelte struct-Felder fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: Array-Felder in struct (z.B. char name[8]) -- Byte-Layout inkl.
		# Array-Feld-Groesse (Elementgroesse * Elementzahl), Zugriff nur ueber eine
		# Pointer-Zwischenvariable (direkte "p.field[i]"-Syntax noch nicht moeglich,
		# eigener Folgeschritt, siehe docs/FORTSCHRITT.md). Zuweisung an das GANZE
		# Array-Feld wird diagnostiziert (wie in echtem C nicht erlaubt).
		tc_check 'struct Rec { char name[8]; int id; }; int main(){ putint(sizeof(struct Rec)); }' '12'
		tc_check 'struct Rec { char name[8]; int id; }; int main(){ struct Rec r; r.id = 42; char *p = r.name; p[0] = 65; p[1] = 66; putint(r.id); putchar(p[0]); putchar(p[1]); }' '42\nAB'
		tc_check 'struct Rec { char tag; int value; char buf[4]; }; int main(){ struct Rec r; r.tag = 1; r.value = 1000; char *p = r.buf; p[0]=9; putint(r.tag); putint(r.value); putint(p[0]); putint(sizeof(struct Rec)); }' '1\n1000\n9\n12'
		if build/tinyc_p 'struct Rec { char name[8]; }; int main(){ struct Rec r; struct Rec r2; r.name = r2.name; }' 2>&1 | grep -q 'cannot assign to array field'; then
			echo "ok    tinyc: Zuweisung an ganzes Array-Feld wird diagnostiziert"
		else
			echo "FAIL  tinyc: Array-Feld-Zuweisungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: void als Rueckgabetyp (echte Semantik, return; war bereits vorher
		# moeglich) und void* als generischer Pointer (bidirektional kompatibel zu jedem
		# ANDEREN Pointer derselben Tiefe, siehe tcCompatible). void* selbst darf nicht
		# dereferenziert/indiziert/arithmetisch veraendert werden (kein LOADIND mit
		# geratener Groesse); bare "void" bleibt ausserhalb des Rueckgabetyps verboten.
		tc_check 'void greet(){ putint(1); } int main(){ greet(); putint(2); }' '1\n2'
		tc_check 'int main(){ int x = 42; void *p = &x; int *q = p; putint(*q); }' '42'
		tc_check 'int deref(void *p){ int *q = p; return *q; } int main(){ int x=7; putint(deref(&x)); }' '7'
		if build/tinyc_p 'int main(){ void x; putint(1); }' 2>&1 | grep -q 'void is not a valid variable type'; then
			echo "ok    tinyc: bare void als lokale Variable wird diagnostiziert"
		else
			echo "FAIL  tinyc: bare-void-Lokale-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int f(void x){ return 0; } int main(){ putint(f(1)); }' 2>&1 | grep -q 'void is not a valid parameter type'; then
			echo "ok    tinyc: bare void als Parameter wird diagnostiziert"
		else
			echo "FAIL  tinyc: bare-void-Parameter-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'void g; int main(){ putint(1); }' 2>&1 | grep -q 'void is not a valid variable type'; then
			echo "ok    tinyc: bare void als globale Variable wird diagnostiziert"
		else
			echo "FAIL  tinyc: bare-void-Globale-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ int x=1; void *p=&x; putint(*p); }' 2>&1 | grep -q 'cannot dereference void\*'; then
			echo "ok    tinyc: Dereferenzierung von void* wird diagnostiziert"
		else
			echo "FAIL  tinyc: void-Dereferenzierungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ int x=1; void *p=&x; putint(p[0]); }' 2>&1 | grep -q 'cannot dereference void\*'; then
			echo "ok    tinyc: Indizierung von void* wird diagnostiziert"
		else
			echo "FAIL  tinyc: void-Indizierungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ int x=1; void *p=&x; p = p + 1; putint(1); }' 2>&1 | grep -q 'arithmetic on void\* is not supported'; then
			echo "ok    tinyc: Arithmetik auf void* wird diagnostiziert"
		else
			echo "FAIL  tinyc: void-Arithmetik-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'void f(){ return 5; } int main(){ f(); putint(1); }' 2>&1 | grep -q 'return expects void, got int'; then
			echo "ok    tinyc: return mit Wert aus void-Funktion wird diagnostiziert"
		else
			echo "FAIL  tinyc: void-Return-Diagnose fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: typedef struct { ... } Name; -- anonymes struct inline im typedef.
		# Der typedef-Zielname wird bewusst als interner struct-Tag wiederverwendet (harmlose
		# Vereinfachung); Namenskollision mit einem bereits existierenden struct wird wie eine
		# normale doppelte struct-Deklaration abgelehnt.
		if build/tinyc_p 'struct Dup { int x; }; typedef struct { int y; } Dup; int main(){ putint(1); }' 2>&1 | grep -q 'duplicate struct'; then
			echo "ok    tinyc: Namenskollision bei anonymem struct-typedef wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer anonymes-struct-typedef-Namenskollision fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ break; }' 2>&1 | grep -q 'break outside loop' && \
		   build/tinyc_p 'int main(){ continue; }' 2>&1 | grep -q 'continue outside loop'; then
			echo "ok    tinyc: break/continue ausserhalb Schleife werden diagnostiziert"
		else
			echo "FAIL  tinyc: break/continue-Diagnose fehlt"; tcfail=1; fail=1
		fi
		tc_check 'int add(int a, int b){ return a + b; } int twice(int x){ return add(x, x); } int main(){ putint(twice(21)); }' '42'
		tc_check 'int fact(int n){ if(n <= 1) return 1; else return n * fact(n - 1); } int main(){ putint(fact(5)); }' '120'
		tc_check 'int counter; int bump(){ counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); }' '1\n2'
		tc_check 'int limit = 10; int debt = -7; int counter; int main(){ counter = limit + debt; putint(counter); }' '3'
		tc_check 'int main(){ putchar(72); putchar(105); putchar(10); putint(7); }' 'Hi\n7'
		tc_check 'char mark = 346; int main(){ char copy; copy = mark; putchar(copy); putchar(10); }' 'Z'
		tc_check 'char next(char c){ return c + 1; } int main(){ putchar(next(345)); putchar(10); }' 'Z'
		tc_check 'int g[3]; char bytes[4]; int main(){ int a[3]; char c[2]; a[1] = 40; a[2] = 2; c[0] = 300; g[0] = a[1] + a[2]; bytes[3] = c[0] + 1; putint(g[0]); putint(bytes[3]); }' '42\n45'
		tc_check 'int g[4] = {7, -2, 9}; char h[3] = {65, 322}; int main(){ int a[3] = {10, 20, 30}; char b[2] = {90, 256}; putint(g[1]); putint(a[2]); putchar(h[1]); putchar(b[0]); }' '-2\n30\nBZ'
		tc_check 'int sum(int a[], int n){ int s=0; while(n>0){ n=n-1; s=s+a[n]; } return s; } int main(){ int v[3]={10,20,30}; putint(sum(v,3)); }' '60'
		tc_check 'bool less(int a, int b){ return a < b; } int main(){ bool ok = less(2,3); if(ok) putint(1); else putint(0); }' '1'
		tc_check 'unsigned int x=-1; int main(){ putint(x > 1); putint(x / 2); }' '1\n2147483647'
		tc_check 'unsigned int x=-1; int main(){ putint(x); putuint(x); }' '-1\n4294967295'
		tc_check 'int main(){ bool a=true; bool b=false; putint(!a); putint(!b); putuint(~0); }' '0\n1\n4294967295'
		tc_check 'int counter; bool bump(){ counter = counter + 1; return true; } int main(){ putint(false && bump()); putint(counter); putint(true || bump()); putint(counter); putint(true && bump()); putint(counter); putint(false || bump()); putint(counter); }' '0\n0\n1\n0\n1\n1\n1\n2'
		tc_check 'int main(){ putint(true || false && false); putint((true || false) && false); }' '1\n0'
		tc_check 'unsigned int high = -1; int main(){ putuint(high & 255); putint(6 ^ 3); putint(6 | 3); putint(8 | 3 ^ 1 & 6); }' '255\n5\n7\n11'
		tc_check 'unsigned int high = -1; int main(){ putint(1 << 3); putint(16 >> 2); putint(32 >> 1 >> 2); putuint(high >> 30); putint(1 + 2 << 2); putint(1 << 2 + 1); }' '8\n4\n4\n3\n12\n8'
		tc_check 'unsigned int high = -1; int main(){ putint(20 % 6); putint(-20 % 6); putuint(high % 10); putint(20 / 6 % 4); }' '2\n-2\n5\n3'
		tc_check 'int counter; int bump(){ counter = counter + 1; return 77; } int main(){ bool yes=true; bool no=false; putint(yes ? 11 : bump()); putint(counter); putint(no ? bump() : 22); putint(counter); putint(false ? 1 : true ? 2 : 3); }' '11\n0\n22\n0\n2'
		tc_check 'int g[2]={7,0}; int main(){ int x=20; unsigned int high=-1; int a[2]={10,20}; x += 3; x -= 3; x *= 2; x /= 6; x %= 4; x <<= 3; x >>= 2; x |= 8; x &= 10; x ^= 3; high >>= 30; a[1] += 2; a[0] |= 4; g[0] ^= 3; putint(x); putuint(high); putint(a[0]); putint(a[1]); putint(g[0]); }' '11\n3\n14\n22\n4'
		tc_check 'int *identity(int *p){ return p; } int main(){ int x=40; int *p=&x; *p += 2; putint(*identity(p)); }' '42'
		tc_check 'int values[4]={10,20,30,40}; char bytes[4]={5,6,7,8}; int main(){ int *p=values; char *c=bytes; int **pp=&p; int *pa[2]; int **r=pa; pa[0]=&values[0]; pa[1]=&values[3]; putint(p[2]); *(p+1)=25; putint(*(1+p)); p+=3; putint(*p); putint(p-values); c+=2; putint(*c); putint(c-bytes); putint(p!=0); putint(p>values); putint(**pp); putint(*r[1]); }' '30\n25\n40\n3\n7\n2\n1\n1\n40\n40'
		if build/tinyc_p 'bool id(bool b){ return b; } int main(){ int n=1; id(n); }' 2>&1 | grep -q 'argument expects bool, got int' && \
		   build/tinyc_p 'bool bad(){ return 1; } int main(){ return 0; }' 2>&1 | grep -q 'return expects bool, got int'; then
			echo "ok    tinyc: bool-Argumente und -Rueckgaben werden typgeprueft"
		else
			echo "FAIL  tinyc: bool-Typpruefung fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ char a[2]; a[2] = 9; }' 2>&1 | grep -q 'constant array index 2 out of range (length 2)'; then
			echo "ok    tinyc: konstante Arraygrenze wird diagnostiziert"
		else
			echo "FAIL  tinyc: konstante Arraygrenze nicht diagnostiziert"; tcfail=1; fail=1
		fi
		# 2026-07-24: const-Qualifizierer fuer globale/lokale Variablen und Parameter --
		# rein frontend-seitig (kein Backend-/IR-Unterschied), verbietet Zuweisung/++/--
		# auf die qualifizierte Variable selbst (kein Pointee-const wie in echtem C).
		tc_check 'const int g = 7; int main(){ putint(g); }'                  '7'
		tc_check 'int main(){ const int x = 5; putint(x + 1); }'             '6'
		if build/tinyc_p 'const int g = 7; int main(){ g = 8; putint(g); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    tinyc: Zuweisung an const-Globale wird diagnostiziert"
		else
			echo "FAIL  tinyc: const-Globale-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ const int x = 5; x = 6; putint(x); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    tinyc: Zuweisung an const-Lokale wird diagnostiziert"
		else
			echo "FAIL  tinyc: const-Lokale-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int bump(const int x){ x = x + 1; return x; } int main(){ putint(bump(1)); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    tinyc: Zuweisung an const-Parameter wird diagnostiziert"
		else
			echo "FAIL  tinyc: const-Parameter-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ const int x = 5; x++; putint(x); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    tinyc: ++/-- auf const-Variable wird diagnostiziert"
		else
			echo "FAIL  tinyc: const-++/---Diagnose fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: "static" lokale Variablen -- persistieren ueber Aufrufe hinweg (als
		# ganz normaler GLOBAL registriert, siehe tc_staticlocal in Data/tinyc.lextab).
		# "static" bei Funktionen/globalen Variablen ist ein reines No-op (interne
		# Verlinkung ist bei einer einzigen Uebersetzungseinheit ohne Mehrdatei-Linkage
		# bedeutungslos). Bewusst OHNE Initialisierer (siehe staticVarDecl-Kommentar in
		# Data/tinyc.ebnf) und OHNE struct/Array in dieser Version.
		tc_check 'int bump(){ static int counter; counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); putint(bump()); }' '1\n2\n3'
		tc_check 'static int add(int a, int b){ return a + b; } int main(){ putint(add(2, 3)); }' '5'
		tc_check 'static int g = 7; int main(){ putint(g); }' '7'
		tc_check 'int f(){ static const int limit; return limit; } int main(){ putint(f()); }' '0'
		tc_check 'int x = 42; int f(){ static int *p; p = &x; return *p; } int main(){ putint(f()); }' '42'
		if build/tinyc_p 'int f(){ static const int limit; limit = 5; return limit; } int main(){ putint(f()); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    tinyc: Zuweisung an static-const-Lokale wird diagnostiziert"
		else
			echo "FAIL  tinyc: static-const-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'struct P{ int x; }; int f(){ static struct P p; return p.x; } int main(){ putint(f()); }' 2>&1 | grep -q 'static struct locals not yet supported'; then
			echo "ok    tinyc: static struct-Lokale wird bewusst abgelehnt (eigener Folgeschritt)"
		else
			echo "FAIL  tinyc: static-struct-Diagnose fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: static-Initialisierer -- NUR globalValue (Zahl/Negativ/bool-Literal,
		# dieselbe seiteneffektfreie Regel wie bei globalen Variablen) erlaubt, damit KEIN
		# Laufzeit-Code fuer den Initialiserer emittiert wird (tc_staticlocal extrahiert den
		# Wert per Rohtext-Scan, kein Durchlauf durch die normale expr-Grammatik). Ein NICHT-
		# konstanter Ausdruck bleibt ein sauberer Parse-Fehler (kein Absturz).
		tc_check 'int bump(){ static int counter = 10; counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); }' '11\n12'
		tc_check 'int f(){ static int x = -5; return x; } int main(){ putint(f()); }' '-5'
		tc_check 'int f(){ static bool b = true; return b; } int main(){ putint(f()); }' '1'
		tc_check 'int f(){ static char c = 65; return c; } int main(){ putint(f()); }' '65'
		tc_check 'int f(){ static int *p = 0; return p == 0; } int main(){ putint(f()); }' '1'
		if build/tinyc_p 'int f(){ static int *p = 5; return 0; } int main(){ putint(f()); }' 2>&1 | grep -q 'static local pointer initializer must be 0'; then
			echo "ok    tinyc: static-Pointer-Initialisierer != 0 wird diagnostiziert"
		else
			echo "FAIL  tinyc: static-Pointer-Initialisierer-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int y=1; int bump(){ static int counter = y; counter = counter + 1; return counter; } int main(){ putint(bump()); }' >/dev/null 2>&1; then
			echo "FAIL  tinyc: nicht-konstanter static-Initialisierer wird faelschlich akzeptiert"; tcfail=1; fail=1
		else
			echo "ok    tinyc: nicht-konstanter static-Initialisierer bleibt Parse-Fehler (eigener Folgeschritt)"
		fi
		# 2026-07-24: Pointee-Constness fuer "const T*" -- TCType.pointeeConst (ein Bit,
		# reist durch Zeigerarithmetik/Parameteruebergabe mit). Schreiben DURCH den Pointer
		# (*p = .., p[i] = ..) wird verboten, der Pointer selbst bleibt frei zuweisbar
		# (deckt das uebliche "p++"-Idiom UND den Hauptnutzungsfall im Generator-Vorbild,
		# const char* line-Parameter, jetzt auch semantisch ab -- nicht nur syntaktisch).
		tc_check 'int values[3] = {1,2,3}; int main(){ const int *p = values; p += 1; putint(*p); }' '2'
		tc_check 'int main(){ int x=42; const int *p = &x; putint(*p); putint(p[0]); }' '42\n42'
		tc_check 'int f(const int *p){ return *p; } int main(){ int x=42; putint(f(&x)); }' '42'
		if build/tinyc_p 'int main(){ int x=1; const int *p = &x; *p = 5; putint(x); }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    tinyc: Schreiben durch const-Pointer (*p = ..) wird diagnostiziert"
		else
			echo "FAIL  tinyc: const-Pointer-Schreibschutz (*p) fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ int a[3]; const int *p = a; p[0] = 5; putint(a[0]); }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    tinyc: Schreiben durch const-Pointer (p[i] = ..) wird diagnostiziert"
		else
			echo "FAIL  tinyc: const-Pointer-Schreibschutz (p[i]) fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int g[3]; const int *p; int main(){ p = g; p[0] = 5; putint(g[0]); }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    tinyc: Schreiben durch globalen const-Pointer wird diagnostiziert"
		else
			echo "FAIL  tinyc: const-Pointer-Schreibschutz (global) fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int f(const int *p){ *p = 5; return *p; } int main(){ int x=1; putint(f(&x)); }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    tinyc: Schreiben durch const-Pointer-Parameter wird diagnostiziert"
		else
			echo "FAIL  tinyc: const-Pointer-Schreibschutz (Parameter) fehlt"; tcfail=1; fail=1
		fi
		[ $tcfail -eq 0 ] && echo "ok    tinyc: 102 Programme inkl. Pointer, for/do-while/break/continue, struct (gemischte Feldtypen, anonym im typedef, Array-Felder)/typedef/enum, sizeof/++/--/switch/Casts/const/static/Pointee-Constness/void/void* -> tinyvm korrekt"
	else
		echo "FAIL  tinyc: Data/tinyc_p.c kompiliert nicht"; fail=1
	fi
else
	echo "warn  tinyc: python3 fehlt -- Tiny-C/tinyvm-Check uebersprungen"
fi

# 13) Tiny-C M4a: eigenstaendiges Backend liest Stack-IR und erzeugt
#     PIC-faehigen 68000-Assembler. Noch keine Ziel-Runtime/Ausfuehrung; vasm
#     prueft aber Funktionsframes, Parameter, CALL/RET und alle Syntaxdetails.
#     Gebaut wird die reine-C-Fassung (tinyc_backend_c.cpp, siehe
#     docs/SELFHOSTING_LUECKENLISTE.md); das C++-Original (tinyc_backend.cpp)
#     bleibt als Referenz liegen -- Ruecksetzen = hier wieder die .cpp bauen.
if [ -x tools/vasmm68k_mot ]; then
	if cc -std=c11 -Wall -Wextra -x c -o build/tinyc_backend Source/tinyc_backend_c.cpp 2>/dev/null && \
		build/tinyc_p 'int add(int a, int b){ return a + b; } int main(){ putint(add(19, 23)); }' > build/tinyc_m4.ir && \
		build/tinyc_backend build/tinyc_m4.ir build/tinyc_m4.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_m4.bin build/tinyc_m4.s68 2>/dev/null && \
		grep -q '^tc_add:' build/tinyc_m4.s68 && grep -q $'bsr\ttc_add' build/tinyc_m4.s68; then
		echo "ok    tinyc M4a: reines C, IR->PIC-68000-Assembler assembliert mit vasm"
	else
		echo "FAIL  tinyc M4a: IR->68k-Backend oder vasm fehlgeschlagen"; fail=1
	fi
else
	echo "warn  tinyc M4a: vasm fehlt -- Backend-Assembler-Check uebersprungen"
fi

# 13a) "static" lokale Variablen (2026-07-24): GLOBAL/GARRAY/GINIT duerfen jetzt auch
#      INNERHALB einer Funktion im IR-Strom stehen (frueher nur davor erlaubt) -- eine
#      static-Lokale wird an genau der Textstelle ihrer Deklaration als GLOBAL emittiert,
#      siehe collectGlobals()/collectFunctions() in tinyc_backend_c.cpp. Test prueft
#      echte Persistenz ueber mehrere Aufrufe hinweg im ECHTEN 68000-Code (nicht nur TinyVM).
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'int bump(){ static int counter; counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); putint(bump()); }' > build/tinyc_static.ir && \
		build/tinyc_backend build/tinyc_static.ir build/tinyc_static.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_static.bin build/tinyc_static.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_static.s68 2>/dev/null)" = "$(printf '1\n2\n3')" ]; then
		echo "ok    tinyc static 68000: lokale static-Variable persistiert ueber Aufrufe hinweg"
	else
		echo "FAIL  tinyc static 68000: static-Persistenz fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc static 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi

# 13b) struct-Felder (2026-07-24: gemischte skalare Feldtypen, echtes Byte-Layout,
#      siehe SELFHOSTING_LUECKENLISTE.md): Feldzugriff nutzt PUSHADDR/IPADD/LOADIND/
#      STOREIND -- bereits vorhandene, architekturneutrale Opcodes, kein neuer Opcode
#      und keine Backend-Aenderung noetig, trotzdem hier explizit durch 68000 und ARM64
#      gegengeprueft (einheitlicher Feldtyp als Regressionsschutz plus gemischter Fall).
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'struct Point { int x; int y; }; int main(){ struct Point p; p.x=3; p.y=4; putint(p.x+p.y); }' > build/tinyc_struct.ir && \
		build/tinyc_backend build/tinyc_struct.ir build/tinyc_struct.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_struct.bin build/tinyc_struct.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_struct.s68 2>/dev/null)" = "7" ]; then
		echo "ok    tinyc struct 68000: Feldzugriff (LOADIDX/STOREIDX) korrekt"
	else
		echo "FAIL  tinyc struct 68000: Feldzugriff fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'struct Mixed { char a; int b; char c; }; int main(){ struct Mixed m; m.a=1; m.b=1000; m.c=2; putint(m.b+m.a+m.c); }' > build/tinyc_struct_mixed.ir && \
		build/tinyc_backend build/tinyc_struct_mixed.ir build/tinyc_struct_mixed.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_struct_mixed.bin build/tinyc_struct_mixed.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_struct_mixed.s68 2>/dev/null)" = "1003" ]; then
		echo "ok    tinyc struct 68000: gemischte Feldtypen (char/int/char, Byte-Offset+Padding) korrekt"
	else
		echo "FAIL  tinyc struct 68000: gemischte Feldtypen fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000 (gemischt): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'typedef struct { char a; int b; } Mixed; int main(){ Mixed m; m.a=1; m.b=1000; putint(m.b+m.a); }' > build/tinyc_struct_anon.ir && \
		build/tinyc_backend build/tinyc_struct_anon.ir build/tinyc_struct_anon.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_struct_anon.bin build/tinyc_struct_anon.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_struct_anon.s68 2>/dev/null)" = "1001" ]; then
		echo "ok    tinyc struct 68000: anonymes struct inline im typedef korrekt"
	else
		echo "FAIL  tinyc struct 68000: anonymes struct inline im typedef fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000 (anonym im typedef): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'struct Rec { char tag; int value; char buf[4]; }; int main(){ struct Rec r; r.tag = 1; r.value = 1000; char *p = r.buf; p[0]=9; putint(r.tag); putint(r.value); putint(p[0]); putint(sizeof(struct Rec)); }' > build/tinyc_struct_arrfield.ir && \
		build/tinyc_backend build/tinyc_struct_arrfield.ir build/tinyc_struct_arrfield.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_struct_arrfield.bin build/tinyc_struct_arrfield.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_struct_arrfield.s68 2>/dev/null)" = "$(printf '1\n1000\n9\n12')" ]; then
		echo "ok    tinyc struct 68000: Array-Feld (char buf[4]) korrekt"
	else
		echo "FAIL  tinyc struct 68000: Array-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000 (Array-Feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'void greet(int x){ putint(x); } int deref(void *p){ int *q = p; return *q; } int main(){ greet(9); int x=7; putint(deref(&x)); }' > build/tinyc_void.ir && \
		build/tinyc_backend build/tinyc_void.ir build/tinyc_void.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_void.bin build/tinyc_void.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_void.s68 2>/dev/null)" = "$(printf '9\n7')" ]; then
		echo "ok    tinyc void 68000: void-Rueckgabe + void*-Parameter korrekt"
	else
		echo "FAIL  tinyc void 68000: void/void* fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc void 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'int main(){ int x=2; int r=0; switch(x){ case 1: r=11; break; case 2: case 3: r=23; break; default: r=99; } putint(r); }' > build/tinyc_switch.ir && \
		build/tinyc_backend build/tinyc_switch.ir build/tinyc_switch.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_switch.bin build/tinyc_switch.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_switch.s68 2>/dev/null)" = "23" ]; then
		echo "ok    tinyc switch 68000: gestapelte case-Label + default korrekt"
	else
		echo "FAIL  tinyc switch 68000: switch/case fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc switch 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# 14) Tiny-C M4b/M4c-1: Der Python-Simulator ist ausschliesslich ein Test-Orakel,
#     nicht Teil der auszuliefernden Toolchain. Er fuehrt die von M4a erzeugte 68k-
#     Schablonen-Ausgabe inklusive Frame/Call/RET und der ECHTEN 68k-Core-
#     Schablonen fuer signed int32 MUL/DIV aus. Nur PRINT bleibt ein Plattform-Hook.
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ]; then
	if build/tinyc_p 'unsigned int high=-1; int fact(int n){ if(n <= 1) return 1; else return n * fact(n - 1); } int main(){ putint(fact(5)); putint(-7 * 6); putint(20 / 3); putint(-20 / 3); putint(high > 1); putint(high / 2); putuint(high); }' > build/tinyc_m4b.ir && \
		build/tinyc_backend build/tinyc_m4b.ir build/tinyc_m4b.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_m4b.bin build/tinyc_m4b.s68 2>/dev/null && \
		grep -q '^tc_mul_loop:' build/tinyc_m4b.s68 && grep -q '^tc_div_loop:' build/tinyc_m4b.s68 && grep -q '^tc_udiv_loop:' build/tinyc_m4b.s68 && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_m4b.s68 2>/dev/null)" = "$(printf '120\n-42\n6\n-6\n1\n2147483647\n4294967295')" ]; then
		echo "ok    tinyc M4c-1: echte 68k int32 signed/unsigned MUL/DIV + Fakultaet = tinyvm"
	else
		echo "FAIL  tinyc M4c-1: 68k-Core-Lauf stimmt nicht mit tinyvm ueberein"; fail=1
	fi
else
	echo "warn  tinyc M4c-1: python3 oder Backend fehlt -- Ausfuehrungstest uebersprungen"
fi

# 15) Tiny-C M4c-2: Globale int32-Variablen besitzen eine eigene, dauerhafte
#     Namens-Tabelle im Frontend und werden im 68k-Code PC-relativ als LOADG/
#     STOREG auf das nullinitialisierte Daten-/BSS-Aequivalent angesprochen.
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ]; then
	if build/tinyc_p 'int limit = 10; char mark = 346; int counter; char next(char c){ return c + 1; } int bump(){ counter = counter + limit; return counter; } int main(){ char copy; copy = mark; putchar(copy); putchar(next(334)); putchar(10); putint(bump()); putint(bump()); }' > build/tinyc_globals.ir && \
		build/tinyc_backend build/tinyc_globals.ir build/tinyc_globals.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_globals.bin build/tinyc_globals.s68 2>/dev/null && \
		grep -q $'tc_g_limit:\tdc.l\t10' build/tinyc_globals.s68 && grep -q $'tc_g_mark:\tdc.b\t90' build/tinyc_globals.s68 && grep -q 'andi.l' build/tinyc_globals.s68 && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_globals.s68 2>/dev/null)" = "$(printf 'ZO\n10\n20')" ]; then
		echo "ok    tinyc M4c-4: DATA/BSS + putchar im 68k-Runtime-Vertrag = tinyvm"
	else
		echo "FAIL  tinyc M4c-4: globale Variablen/putchar im 68k-Pfad fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc M4c-4: python3 oder Backend fehlt -- Globals-Test uebersprungen"
fi

# 15b) Pointer-End-to-End auf dem 68000-Pfad: echte Adressen, char/int-Skalierung,
#      Pointerdifferenz, Pointer auf Pointer und Pointerarrays.
pointer_program='int values[4]={10,20,30,40}; char bytes[4]={5,6,7,8}; int main(){ int *p=values; char *c=bytes; int **pp=&p; int *pa[2]; int **r=pa; pa[0]=&values[0]; pa[1]=&values[3]; putint(p[2]); *(p+1)=25; putint(*(1+p)); p+=3; putint(*p); putint(p-values); c+=2; putint(*c); putint(c-bytes); putint(p!=0); putint(p>values); putint(**pp); putint(*r[1]); }'
pointer_expected=$(printf '30\n25\n40\n3\n7\n2\n1\n1\n40\n40')
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p "$pointer_program" > build/tinyc_pointer_reg.ir && \
		build/tinyc_backend build/tinyc_pointer_reg.ir build/tinyc_pointer_reg.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_pointer_reg.bin build/tinyc_pointer_reg.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_pointer_reg.s68 2>/dev/null)" = "$pointer_expected" ]; then
		echo "ok    tinyc 68000 Pointer: Adressen, Skalierung, Differenz und T** korrekt"
	else
		echo "FAIL  tinyc 68000 Pointer: Backend- oder Ausfuehrungsfehler"; fail=1
	fi
else
	echo "warn  tinyc 68000 Pointer: Backend, vasm oder python3 fehlt -- uebersprungen"
fi

# 16) ARM64/Darwin: erster echter Hosted-Zielweg. Der Backend-Treiber erzeugt
#     Programmassembler; runtime/arm64_darwin/start.s liefert eigenen Einstieg,
#     putint und exit. Der Linker bindet nur libSystem zum Laden des Mach-O ein,
#     keine C-Startdateien (-nostartfiles).
#     Gebaut wird die reine-C-Fassung (tinyc_arm64_backend_c.cpp, siehe
#     docs/SELFHOSTING_LUECKENLISTE.md); das C++-Original bleibt als Referenz
#     liegen -- Ruecksetzen = hier wieder die .cpp bauen.
if [ "$(uname -m)" = "arm64" ] && command -v clang >/dev/null 2>&1; then
	if cc -std=c11 -Wall -Wextra -x c -o build/tinyc_arm64_backend Source/tinyc_arm64_backend_c.cpp 2>/dev/null && \
		build/tinyc_p 'int limit = 5; int debt = -20; unsigned int high = -1; char mark = 335; int counter; char next(char c){ return c + 1; } int fact(int n){ if(n <= 1) return 1; else return n * fact(n - 1); } int main(){ char copy; copy = mark; putchar(copy); putchar(next(334)); putchar(10); counter = fact(limit); putint(counter); putint(debt / 3); putint(high > 1); putint(high / 2); putuint(high); }' > build/tinyc_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_arm64.ir build/tinyc_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_arm64 build/tinyc_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		grep -q 'ldrb' build/tinyc_arm64.s && grep -q 'strb' build/tinyc_arm64.s && grep -q 'and.*#255' build/tinyc_arm64.s && grep -q 'udiv' build/tinyc_arm64.s && \
		[ "$(build/tinyc_arm64)" = "$(printf 'OO\n120\n-6\n1\n2147483647\n4294967295')" ] && \
		build/tinyc_p "$pointer_program" > build/tinyc_pointer_arm64_reg.ir && \
		build/tinyc_arm64_backend build/tinyc_pointer_arm64_reg.ir build/tinyc_pointer_arm64_reg.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_pointer_arm64_reg build/tinyc_pointer_arm64_reg.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_pointer_arm64_reg)" = "$pointer_expected" ]; then
		echo "ok    tinyc ARM64/Darwin: native char/unsigned und 64-Bit-Pointer korrekt"
	else
		echo "FAIL  tinyc ARM64/Darwin: nativer Backend-/Runtime-Pfad fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc ARM64/Darwin: nur auf arm64-macOS getestet -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'struct Point { int x; int y; }; int main(){ struct Point p; p.x=3; p.y=4; putint(p.x+p.y); }' > build/tinyc_struct_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_struct_arm64.ir build/tinyc_struct_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_struct_arm64 build/tinyc_struct_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_struct_arm64)" = "7" ]; then
		echo "ok    tinyc struct ARM64: Feldzugriff korrekt"
	else
		echo "FAIL  tinyc struct ARM64: Feldzugriff fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'struct Mixed { char a; int b; char c; }; int main(){ struct Mixed m; m.a=1; m.b=1000; m.c=2; putint(m.b+m.a+m.c); }' > build/tinyc_struct_mixed_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_struct_mixed_arm64.ir build/tinyc_struct_mixed_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_struct_mixed_arm64 build/tinyc_struct_mixed_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_struct_mixed_arm64)" = "1003" ]; then
		echo "ok    tinyc struct ARM64: gemischte Feldtypen (char/int/char, Byte-Offset+Padding) korrekt"
	else
		echo "FAIL  tinyc struct ARM64: gemischte Feldtypen fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64 (gemischt): Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'typedef struct { char a; int b; } Mixed; int main(){ Mixed m; m.a=1; m.b=1000; putint(m.b+m.a); }' > build/tinyc_struct_anon_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_struct_anon_arm64.ir build/tinyc_struct_anon_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_struct_anon_arm64 build/tinyc_struct_anon_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_struct_anon_arm64)" = "1001" ]; then
		echo "ok    tinyc struct ARM64: anonymes struct inline im typedef korrekt"
	else
		echo "FAIL  tinyc struct ARM64: anonymes struct inline im typedef fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64 (anonym im typedef): Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'struct Rec { char tag; int value; char buf[4]; }; int main(){ struct Rec r; r.tag = 1; r.value = 1000; char *p = r.buf; p[0]=9; putint(r.tag); putint(r.value); putint(p[0]); putint(sizeof(struct Rec)); }' > build/tinyc_struct_arrfield_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_struct_arrfield_arm64.ir build/tinyc_struct_arrfield_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_struct_arrfield_arm64 build/tinyc_struct_arrfield_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_struct_arrfield_arm64)" = "$(printf '1\n1000\n9\n12')" ]; then
		echo "ok    tinyc struct ARM64: Array-Feld (char buf[4]) korrekt"
	else
		echo "FAIL  tinyc struct ARM64: Array-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64 (Array-Feld): Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'void greet(int x){ putint(x); } int deref(void *p){ int *q = p; return *q; } int main(){ greet(9); int x=7; putint(deref(&x)); }' > build/tinyc_void_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_void_arm64.ir build/tinyc_void_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_void_arm64 build/tinyc_void_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_void_arm64)" = "$(printf '9\n7')" ]; then
		echo "ok    tinyc void ARM64: void-Rueckgabe + void*-Parameter korrekt"
	else
		echo "FAIL  tinyc void ARM64: void/void* fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc void ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'int main(){ int x=2; int r=0; switch(x){ case 1: r=11; break; case 2: case 3: r=23; break; default: r=99; } putint(r); }' > build/tinyc_switch_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_switch_arm64.ir build/tinyc_switch_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_switch_arm64 build/tinyc_switch_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_switch_arm64)" = "23" ]; then
		echo "ok    tinyc switch ARM64: gestapelte case-Label + default korrekt"
	else
		echo "FAIL  tinyc switch ARM64: switch/case fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc switch ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'int bump(){ static int counter; counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); putint(bump()); }' > build/tinyc_static_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_static_arm64.ir build/tinyc_static_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_static_arm64 build/tinyc_static_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_static_arm64)" = "$(printf '1\n2\n3')" ]; then
		echo "ok    tinyc static ARM64: lokale static-Variable persistiert ueber Aufrufe hinweg"
	else
		echo "FAIL  tinyc static ARM64: static-Persistenz fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc static ARM64: Backend fehlt -- uebersprungen"
fi

[ $fail -eq 0 ] && echo "=== ALLE TESTS OK ===" || echo "=== FEHLER IN DER SUITE ==="
exit $fail
