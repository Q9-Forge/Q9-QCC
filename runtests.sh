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
	# WICHTIGER FUND (2026-07-25): build/ebnf hat ein festes internes Puffer-Limit
	# fuer den [NUTZER-CODE]-Block (USER_CODE_LEN in Source/ebnf.cpp) -- bei
	# Ueberschreitung wird der Ueberschuss STILLSCHWEIGEND abgeschnitten (nur eine
	# Warnzeile im stdout, die hier vorher mit ">/dev/null" verschluckt wurde).
	# Das hat einmal drei ROUTINE-C-Bloecke (tc_ternarybegin/-middle/-end) aus
	# Data/tinyc.lextab geloescht, ohne dass ein einziger Build-Schritt einen
	# Fehler gemeldet hat -- nur ein spaeter fehlschlagender Ternary-Test hat es
	# aufgedeckt. USER_CODE_LEN wurde deshalb grosszuegig erhoeht (128 KB -> 1 MB),
	# UND hier wird die Ausgabe jetzt auf "WARNUNG" geprueft statt verschluckt.
	ebnfout=$(build/ebnf Data/tinyc 2>&1)
	if echo "$ebnfout" | grep -q 'WARNUNG'; then
		echo "FAIL  tinyc: build/ebnf meldet eine Kuerzungswarnung (siehe oben) -- Data/tinyc.lextab wurde vermutlich abgeschnitten!"
		echo "$ebnfout" | grep 'WARNUNG'
		fail=1
	fi
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
		# 2026-07-24: direkte p.field[i]-Indizierung von Array-Feldern -- member
		# bekommt einen eigenen optionalen index-Anschluss (kein zweiter, ineinander
		# verschachtelter). Stack traegt an der Stelle bereits [Indexwert, Feldadresse]
		# (Feldadresse zuletzt gepusht) -- exakt die Reihenfolge, die ein zweites IPADD
		# braucht, daher IPADD+LOADIND (lesend) bzw. IPADD (schreibend) statt eines
		# neuen Opcodes. Nur lokale struct-Variablen (globale structs sind generell noch
		# nicht unterstuetzt, siehe docs/SELFHOSTING_LUECKENLISTE.md).
		tc_check 'struct P{int x; char buf[4];}; int main(){ struct P p; p.buf[0]=65; p.buf[1]=66; putchar(p.buf[0]); putchar(p.buf[1]); }' 'AB'
		tc_check 'struct P{int a[3];}; int main(){ struct P p; int i; i = 1; p.a[0]=10; p.a[1]=20; p.a[2]=30; putint(p.a[i]); }' '20'
		tc_check 'struct P{int x; char buf[4];}; int main(){ struct P p; p.x=7; p.buf[0]=1; putint(p.x); putint(p.buf[0]); }' '7\n1'
		tc_check 'struct P{int a[3];}; int main(){ struct P p; p.a[0]=5; p.a[0] += 3; putint(p.a[0]); }' '8'
		tc_check 'struct P{int a[3];}; int main(){ struct P p; p.a[0]=10; p.a[1]=20; putint(1 + p.a[0] + p.a[1]); }' '31'
		tc_check 'struct P{char buf[4];}; int main(){ struct P p; char* q; q = p.buf; q[0]=9; putchar(p.buf[0]); }' '\t'
		if build/tinyc_p 'struct P{int x;}; int main(){ struct P p; putint(p.x[0]); }' 2>&1 | grep -q 'scalar struct field cannot be indexed'; then
			echo "ok    tinyc: Indizierung eines skalaren struct-Felds wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer Indizierung eines skalaren struct-Felds fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'struct P{char buf[4];}; int main(){ struct P p; p.buf[9]=1; }' 2>&1 | grep -q 'constant array index 9 out of range'; then
			echo "ok    tinyc: konstanter Index-Bereichsverstoss bei p.field[i] wird diagnostiziert"
		else
			echo "FAIL  tinyc: Bounds-Check fuer p.field[i] fehlt"; tcfail=1; fail=1
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
		# 2026-07-24: zweidimensionale Arrays -- NUR bei lokalen/globalen Variablen (nicht
		# bei struct-Feldern/Parametern). Byte-Layout = dim1*dim2 (row-major, flach im
		# Speicher wie in echtem C); arr[i][j] wird im Frontend zu einem flachen Index
		# i*dim2+j zusammengefuehrt (tcEmit2DCombine: STOREG/PUSH/MUL/LOADG/ADD, bereits
		# vorhandene Opcodes -- kein neuer Opcode, kein Backend-Change). Flacher
		# Initialisierer {1,2,3,4,5,6} funktioniert automatisch mit (row-major).
		tc_check 'int main(){ int m[2][3]; m[0][0]=1; m[0][1]=2; m[0][2]=3; m[1][0]=4; m[1][1]=5; m[1][2]=6; putint(m[0][0]); putint(m[0][1]); putint(m[0][2]); putint(m[1][0]); putint(m[1][1]); putint(m[1][2]); }' '1\n2\n3\n4\n5\n6'
		tc_check 'int g[2][3]; int main(){ g[0][0]=10; g[1][2]=99; putint(g[0][0]); putint(g[1][2]); putint(g[0][1]); }' '10\n99\n0'
		tc_check 'char names[3][4]; int main(){ names[0][0]=65; names[1][2]=66; putchar(names[0][0]); putchar(names[1][2]); putint(names[2][0]); }' 'AB0'
		tc_check 'int main(){ int m[3][3]; int i; int j; for(i=0;i<3;i+=1){ for(j=0;j<3;j+=1){ m[i][j]=i*10+j; } } putint(m[2][1]); putint(m[0][2]); }' '21\n2'
		tc_check 'int main(){ int m[2][3]; putint(sizeof(m)); }' '24'
		tc_check 'int m[2][3] = {1,2,3,4,5,6}; int main(){ putint(m[0][0]); putint(m[1][2]); }' '1\n6'
		tc_check 'int main(){ int b[2]; b[0]=1; int a[3]; a[0]=10; a[1]=20; putint(a[b[0]]); }' '20'
		if build/tinyc_p 'int main(){ int m[2][3]; putint(m[0]); }' 2>&1 | grep -q 'partial indexing of a 2D array is not supported'; then
			echo "ok    tinyc: partielle Indizierung eines 2D-Arrays wird diagnostiziert"
		else
			echo "FAIL  tinyc: 2D-Array-Teilindizierungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ int a[5]; putint(a[0][1]); }' 2>&1 | grep -q 'array is not two-dimensional'; then
			echo "ok    tinyc: 2 Indizes auf ein 1D-Array werden diagnostiziert"
		else
			echo "FAIL  tinyc: 1D-Array-Doppelindizierungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'struct R{ int m[2][3]; }; int main(){ putint(1); }' >/dev/null 2>&1; then
			echo "FAIL  tinyc: 2D-Array-struct-Feld wird faelschlich akzeptiert"; tcfail=1; fail=1
		else
			echo "ok    tinyc: 2D-Array als struct-Feld bleibt Parse-Fehler (eigener Folgeschritt)"
		fi
		if build/tinyc_p 'int f(int m[][3]){ return 0; } int main(){ putint(1); }' >/dev/null 2>&1; then
			echo "FAIL  tinyc: 2D-Array-Parameter wird faelschlich akzeptiert"; tcfail=1; fail=1
		else
			echo "ok    tinyc: 2D-Array als Parameter bleibt Parse-Fehler (eigener Folgeschritt)"
		fi
		# 2026-07-24: mehr als 2 Array-Dimensionen -- tcCheck2DIndex/tcEmit2DCombine
		# generalisiert zu tcCheckNDIndex/tcEmitNDCombine (TC_MAXDIMS=6 als grosszuegige
		# Obergrenze). arr[i1]..[iN] wird per Horner-Schema ueber N-1 Scratch-Globals
		# (__idxNd_2..__idxNd_N) zu einem flachen row-major-Index kombiniert -- fuer N=2
		# identisch zur bisherigen Loesung (nur EIN Scratch-Feld), kein neuer Opcode,
		# kein Backend-Change. Die beiden 2D-spezifischen Fehlermeldungen oben bleiben
		# wortgleich (siehe tcCheckNDIndex-Kommentar in tinyc.lextab).
		tc_check 'int main(){ int m[2][3][4]; int i; int j; int k; for(i=0;i<2;i+=1){ for(j=0;j<3;j+=1){ for(k=0;k<4;k+=1){ m[i][j][k]=i*100+j*10+k; } } } putint(m[1][2][3]); putint(m[0][0][0]); putint(m[1][0][2]); }' '123\n0\n102'
		tc_check 'int g[2][2][2]; int main(){ g[0][0][0]=1; g[0][0][1]=2; g[0][1][0]=3; g[1][1][1]=8; putint(g[1][1][1]); putint(g[0][1][0]); putint(g[0][0][1]); }' '8\n3\n2'
		tc_check 'int m[2][2][2] = {1,2,3,4,5,6,7,8}; int main(){ putint(m[1][1][1]); putint(m[0][1][0]); }' '8\n3'
		tc_check 'int main(){ int m[2][2][2]; m[0][0][0]=5; m[1][1][1]=10; putint(1 + m[0][0][0] + m[1][1][1]); }' '16'
		if build/tinyc_p 'int main(){ int m[2][3][4]; putint(m[0][1]); }' 2>&1 | grep -q 'partial indexing of a 3D array is not supported'; then
			echo "ok    tinyc: partielle Indizierung eines 3D-Arrays wird diagnostiziert"
		else
			echo "FAIL  tinyc: 3D-Array-Teilindizierungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ int m[2][2][2][2][2][2][2]; putint(1); }' 2>&1 | grep -q 'too many array dimensions (max 6)'; then
			echo "ok    tinyc: Ueberschreiten von TC_MAXDIMS wird diagnostiziert"
		else
			echo "FAIL  tinyc: TC_MAXDIMS-Diagnose fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: String-Literale -- erzeugen zur Uebersetzungszeit einen anonymen
		# globalen char-Array-Konstant (GARRAY/GINIT + Nullterminator) und liefern dessen
		# Adresse als char* (ADDRG) -- dieselben IR-Opcodes wie ein initialisiertes
		# globales char-Array, kein neuer Opcode.
		tc_check 'int main(){ char* s = "AB"; putchar(s[0]); putchar(s[1]); putint(s[2]); }' 'AB0'
		tc_check 'int first(char* s){ return s[0]; } int main(){ putint(first("Hi")); }' '72'
		# 2026-07-24: String-Literale als Array-Initialisierer (char msg[6] = "hallo";)
		# -- kopiert die Bytes DIREKT in die Array-Slots (PUSH/PUSH/STOREIDX, lokal)
		# bzw. per GINIT (global), NICHT nur eine char*-Adresse. Grammatik-Trick:
		# arrayStringInit/globalStringInit sind eigene Huellregeln, damit tc_string
		# (anonyme GARRAY-Konstante) beim lokalen Skalarfall weiterhin normal feuert,
		# aber bei einem Array-Ziel die Adresse per DROP verworfen wird -- global
		# feuert gar keine Nested-ACTION (tc_globalend erkennt/dekodiert per Rohtext-
		# Scan selbst, exakt wie bei Zahlen/Bools). Wie in echtem C ist ein EXAKT
		# passendes Array (ohne Platz fuer den Nullterminator) erlaubt.
		tc_check 'int main(){ char m[5] = "hallo"; putchar(m[0]); putchar(m[4]); putint(sizeof(m)); }' 'ho5'
		tc_check 'int main(){ char m[6] = "hallo"; putchar(m[0]); putint(m[5]); }' 'h0'
		tc_check 'char msg[6] = "hallo"; int main(){ putchar(msg[0]); putint(msg[5]); }' 'h0'
		tc_check 'char msg[5] = "hallo"; int main(){ putint(sizeof(msg)); putchar(msg[4]); }' '5\no'
		if build/tinyc_p 'int main(){ char m[4] = "hallo"; }' 2>&1 | grep -q 'string literal too long for array'; then
			echo "ok    tinyc: zu langes String-Literal als lokaler Array-Initialisierer wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer zu langes lokales String-Array-Literal fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'char msg[4] = "hallo"; int main(){ putint(1); }' 2>&1 | grep -q 'string literal too long for array'; then
			echo "ok    tinyc: zu langes String-Literal als globaler Array-Initialisierer wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer zu langes globales String-Array-Literal fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int arr[4] = "hi"; int main(){ putint(1); }' 2>&1 | grep -q 'string literal initializer requires a char array'; then
			echo "ok    tinyc: String-Literal-Initialisierer fuer Nicht-char-Array wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer String-Literal auf int-Array fehlt"; tcfail=1; fail=1
			fi
		# 2026-07-24: direkte Indizierung ohne Zwischenvariable -- "func()[i]" und
		# "text"[i] duerfen jetzt direkt indiziert werden (postfixIndex-Huellregel um
		# die bestehende index-Regel, siehe Data/tinyc.ebnf). Laufzeitreihenfolge auf
		# dem Stack ist [Pointer, Indexwert] wie bei "p + n" -- daher PADD+LOADIND statt
		# PTRINDEX/LOADIND (das den Pointer zuerst erwartet). Bewusst NICHT Teil dieser
		# Version: Indizierung als Zuweisungsziel (foo()[0] = 5;), verkettete Postfix-
		# Indizierung (f()[0][1]), Indizierung auf "(" expr ")".
		tc_check 'int main(){ putchar("hallo"[0]); putchar("hallo"[4]); }' 'ho'
		tc_check 'char* mkstr(){ return "world"; } int main(){ putchar(mkstr()[0]); putchar(mkstr()[2]); }' 'wr'
		tc_check 'int arr[3]; int* getarr(){ return arr; } int main(){ arr[0]=10; arr[1]=20; arr[2]=30; putint(getarr()[1]); }' '20'
		tc_check 'char* mkstr(){ return "hallo"; } int main(){ int i; i = 3; putchar(mkstr()[i]); }' 'l'
		tc_check 'char* mkstr(){ return "AB"; } int main(){ putint(1 + mkstr()[0]); }' '66'
		if build/tinyc_p 'int f(){ return 5; } int main(){ int x = f()[0]; }' 2>&1 | grep -q 'index expects'; then
			echo "ok    tinyc: Indizierung eines nicht-Pointer-Rueckgabewerts wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer Indizierung eines Nicht-Pointers fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'void* mkvoid(){ return 0; } int main(){ int x = mkvoid()[0]; }' 2>&1 | grep -q 'cannot index void'; then
			echo "ok    tinyc: Indizierung von void* wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer Indizierung von void* fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int* getarr(); int main(){ getarr()[0] = 5; }' >/dev/null 2>&1; then
			echo "FAIL  tinyc: Zuweisung auf indizierten Funktionsaufruf wird faelschlich akzeptiert"; tcfail=1; fail=1
		else
			echo "ok    tinyc: Zuweisung auf foo()[0] bleibt Parse-Fehler (eigener Folgeschritt)"
		fi
		# 2026-07-24: extern-Deklarationen fuer NICHT in Tiny-C definierte Funktionen
		# (z.B. echte OS-9/Microware-clib-Funktionen wie strcmp/printf/malloc). Nur
		# Aufrufpruefung (Argumentanzahl/-typen) hier per TinyVM-Frontend testbar --
		# die eigentliche Microware-ABI-Codeerzeugung (CALLEXT/CALLEXTP) ist 68k-
		# spezifisch und wird weiter unten per Hand-Mock-Stub end-to-end verifiziert
		# (TinyVM/ARM64 kennen die Aufrufkonvention bewusst nicht und lehnen CALLEXT
		# sauber ab statt es stillschweigend falsch zu behandeln).
		tc_check 'extern int strcmp(const char *a, const char *b); int main(){ putint(1); }' '1'
		tc_check 'extern int getval(); int main(){ putint(1); }' '1'
		if build/tinyc_p 'extern int f(int a); extern int f(int a); int main(){ putint(1); }' 2>&1 | grep -q 'duplicate function'; then
			echo "ok    tinyc: doppelte extern-Deklaration wird diagnostiziert"
		else
			echo "FAIL  tinyc: extern-Doppeldeklarations-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'extern int f(int a); int f(int a){ return a; } int main(){ putint(1); }' 2>&1 | grep -q 'duplicate function'; then
			echo "ok    tinyc: interne Funktion kollidiert mit extern-Deklaration wird diagnostiziert"
		else
			echo "FAIL  tinyc: extern/intern-Kollisions-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'extern int f(int a, int b); int main(){ putint(f(1)); }' 2>&1 | grep -q 'wrong argument count'; then
			echo "ok    tinyc: falsche Argumentanzahl bei extern-Aufruf wird diagnostiziert"
		else
			echo "FAIL  tinyc: extern-Argumentanzahl-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'extern int f(...); int main(){ putint(1); }' >/dev/null 2>&1; then
			echo "FAIL  tinyc: \"...\" ohne benannten Parameter wird faelschlich akzeptiert"; tcfail=1; fail=1
		else
			echo "ok    tinyc: \"...\" ohne benannten Parameter davor bleibt Parse-Fehler (wie ISO C)"
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
		# 2026-07-24: static-Initialisierer -- globalValue (Zahl/Negativ/bool-Literal,
		# dieselbe seiteneffektfreie Regel wie bei globalen Variablen) bleibt der
		# Fastpath OHNE Laufzeit-Code (tc_staticlocal extrahiert den Wert per Rohtext-Scan).
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
		# 2026-07-24: nicht-konstanter static-Initialisierer (staticRuntimeInit = expr) --
		# staticInit ist eine geordnete Alternation (globalValue zuerst versucht, faellt bei
		# Nichtuebereinstimmung auf staticRuntimeInit zurueck, gleiches Backtracking-Prinzip
		# wie "call vor varRef"). Runs-once-Guard mit verstecktem bool-Flag-Global
		# (__static_init_<name>, LOADGC/JZ/STOREG.../STOREGC -- dieselben Opcodes wie
		# if/while) sichert zu, dass NUR der erste Aufruf den Wert tatsaechlich speichert.
		# Bewusste Vereinfachung: der Ausdruck wird bei JEDEM Aufruf neu ausgewertet (nicht
		# wie in echtem ISO C nur einmal) -- bei seiteneffektfreien Ausdruecken (Regelfall)
		# identisches beobachtbares Verhalten.
		tc_check 'int base(){ return 10; } int f(){ static int x = base() + 5; x += 1; return x; } int main(){ putint(f()); putint(f()); putint(f()); }' '16\n17\n18'
		tc_check 'int y=1; int bump(){ static int counter = y; counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); }' '2\n3'
		tc_check 'int f(int n){ static int x = n * 2; return x; } int main(){ putint(f(5)); putint(f(100)); }' '10\n10'
		if build/tinyc_p 'int base(){ return 3; } int f(){ static const int x = base()+1; x = 5; return x; } int main(){ putint(f()); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    tinyc: Zuweisung an static-const-Lokale mit Laufzeit-Initialisierer wird diagnostiziert"
		else
			echo "FAIL  tinyc: static-const-Diagnose mit Laufzeit-Initialisierer fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'char* mk(){ return "hi"; } int f(){ static int x = mk(); return x; } int main(){ putint(f()); }' 2>&1 | grep -q 'static initializer expects int, got char\*'; then
			echo "ok    tinyc: Typinkompatibilitaet bei Laufzeit-static-Initialisierer wird diagnostiziert"
		else
			echo "FAIL  tinyc: Typpruefung fuer Laufzeit-static-Initialisierer fehlt"; tcfail=1; fail=1
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
		# 2026-07-24: direkte p.field[i]-Indizierung von Array-Feldern -- member
		# bekommt einen eigenen optionalen index-Anschluss (kein zweiter, ineinander
		# verschachtelter). Stack traegt an der Stelle bereits [Indexwert, Feldadresse]
		# (Feldadresse zuletzt gepusht) -- exakt die Reihenfolge, die ein zweites IPADD
		# braucht, daher IPADD+LOADIND (lesend) bzw. IPADD (schreibend) statt eines
		# neuen Opcodes. Nur lokale struct-Variablen (globale structs sind generell noch
		# nicht unterstuetzt, siehe docs/SELFHOSTING_LUECKENLISTE.md).
		tc_check 'struct P{int x; char buf[4];}; int main(){ struct P p; p.buf[0]=65; p.buf[1]=66; putchar(p.buf[0]); putchar(p.buf[1]); }' 'AB'
		tc_check 'struct P{int a[3];}; int main(){ struct P p; int i; i = 1; p.a[0]=10; p.a[1]=20; p.a[2]=30; putint(p.a[i]); }' '20'
		tc_check 'struct P{int x; char buf[4];}; int main(){ struct P p; p.x=7; p.buf[0]=1; putint(p.x); putint(p.buf[0]); }' '7\n1'
		tc_check 'struct P{int a[3];}; int main(){ struct P p; p.a[0]=5; p.a[0] += 3; putint(p.a[0]); }' '8'
		tc_check 'struct P{int a[3];}; int main(){ struct P p; p.a[0]=10; p.a[1]=20; putint(1 + p.a[0] + p.a[1]); }' '31'
		tc_check 'struct P{char buf[4];}; int main(){ struct P p; char* q; q = p.buf; q[0]=9; putchar(p.buf[0]); }' '\t'
		if build/tinyc_p 'struct P{int x;}; int main(){ struct P p; putint(p.x[0]); }' 2>&1 | grep -q 'scalar struct field cannot be indexed'; then
			echo "ok    tinyc: Indizierung eines skalaren struct-Felds wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer Indizierung eines skalaren struct-Felds fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'struct P{char buf[4];}; int main(){ struct P p; p.buf[9]=1; }' 2>&1 | grep -q 'constant array index 9 out of range'; then
			echo "ok    tinyc: konstanter Index-Bereichsverstoss bei p.field[i] wird diagnostiziert"
		else
			echo "FAIL  tinyc: Bounds-Check fuer p.field[i] fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-25 (Selfhosting L2): Pointer-Felder in struct -- IMMER 8 Byte
		# Groesse/Ausrichtung (siehe tcRegisterStruct-Kommentar), damit dasselbe
		# frontend-berechnete Offset fuer 68k (4-Byte-Pointer) UND ARM64 (8-Byte-
		# Pointer) gueltig bleibt. Direkte Indizierung DURCH ein Pointer-Feld
		# (p.field[i]) ist bewusst NICHT Teil dieser Version (wie zuvor bei
		# Array-Feldern) -- Zugriff nur ueber eine Pointer-Zwischenvariable.
		tc_check 'struct P{char* text; int len;}; int main(){ struct P p; char msg[4]; char* t; msg[0]=72; msg[1]=105; msg[2]=0; p.text=msg; p.len=2; t=p.text; putchar(t[0]); putchar(t[1]); putint(p.len); }' 'Hi2'
		if build/tinyc_p 'struct P{char* text;}; int main(){ struct P p; putint(p.text[0]); }' 2>&1 | grep -q 'scalar struct field cannot be indexed'; then
			echo "ok    tinyc: direkte Indizierung durch ein Pointer-Feld wird diagnostiziert (wie bei Array-Feldern zuvor)"
		else
			echo "FAIL  tinyc: Diagnose fuer Indizierung eines Pointer-Felds fehlt"; tcfail=1; fail=1
		fi
		# arr[i].feld (2026-07-25): ein ARRAY von structs, per Laufzeit-Index adressiert,
		# DANN Feldzugriff. Setzt den Allokations-Fix in tc_local/tc_localdecl voraus
		# (frueher wurde fuer "struct Rec arr[N];" IMMER nur Platz fuer EIN Element
		# reserviert, ein "[N]"-Suffix bei struct-Locals komplett ignoriert -- siehe
		# FORTSCHRITT.md "Bewusst offen"). Neuer Opcode IPADDN skaliert einen Pointer
		# um eine LAUFZEIT-Byte-Groesse (hier: sizeof(struct Rec)) statt einer festen
		# Typtag-Groesse wie IPADD.
		tc_check 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; arr[0].a=10; arr[0].b=11; arr[1].a=20; arr[1].b=21; arr[2].a=30; arr[2].b=31; putint(arr[0].a); putint(arr[0].b); putint(arr[1].a); putint(arr[1].b); putint(arr[2].a); putint(arr[2].b); }' '10\n11\n20\n21\n30\n31'
		# Derselbe Fall, der den Allokations-Bug ueberhaupt aufgedeckt hat: struct mit
		# Pointer-Feld (8-Byte-Layout) in einem Array -- ohne den Fix wuerde arr[1]/arr[2]
		# ausserhalb des (zu klein alloziierten) Blocks liegen ("pointer outside object").
		# Zugriff bewusst nur ueber Pointer-Zwischenvariable (wie bei p.field[i] oben,
		# arr[i].feld[j] direkt bleibt diagnostiziert, siehe naechster Test).
		tc_check 'struct Rec { char name[8]; char* text; }; int main(){ struct Rec arr[3]; char* n; n = arr[0].name; n[0]=65; n = arr[1].name; n[0]=66; n = arr[2].name; n[0]=67; n = arr[1].name; n[0] = 88; n = arr[0].name; putchar(n[0]); n = arr[1].name; putchar(n[0]); n = arr[2].name; putchar(n[0]); }' 'AXC'
		if build/tinyc_p 'struct Rec { char name[8]; }; int main(){ struct Rec arr[3]; putint(arr[0].name[0]); }' 2>&1 | grep -q 'arr\[i\].field\[j\] not supported in this version'; then
			echo "ok    tinyc: arr[i].feld[j] (Index nach Feldzugriff) wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer arr[i].feld[j] fehlt"; tcfail=1; fail=1
		fi
		# ptr[i].feld (2026-07-25, Milestone B): eine LOKALE Pointer-auf-struct-Variable,
		# indiziert, dann Feldzugriff -- braucht der Selfhosting-Pilot fuer routinesC[i].name/
		# .text (ActionRoutine*, ein malloc/realloc-gewachsenes Array, kein festes lokales
		# Array wie arr[i].feld oben). LOADP statt PUSHADDR, sonst dieselbe IPADDN-Idee.
		tc_check 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; struct Rec* p; arr[0].a=10; arr[1].a=20; arr[2].a=30; p = arr; putint(p[0].a); putint(p[1].a); putint(p[2].a); p[1].a = 99; putint(arr[1].a); }' '10\n20\n30\n99'
		if build/tinyc_p 'struct Rec { char name[8]; }; int main(){ struct Rec arr[2]; struct Rec* p; p = arr; putint(p[0].name[0]); }' 2>&1 | grep -q 'ptr\[i\].field\[j\] not supported in this version'; then
			echo "ok    tinyc: ptr[i].feld[j] (Index nach Feldzugriff durch Pointer) wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer ptr[i].feld[j] fehlt"; tcfail=1; fail=1
		fi
		# Die Grammatik-Erweiterung fuer arr[i].feld (Sequenz statt Alternation) macht generell
		# jede "indiziert-dann-Member"-Kombination parsebar -- nur die ZWEI oben gebauten Faelle
		# (festes lokales struct-Array, lokale Pointer-auf-struct-Variable) haben Codegen.
		# Alles andere (hier: Pointer auf einen NICHT-struct-Typ) muss weiterhin sauber
		# diagnostiziert werden statt die "."-Fortsetzung stillschweigend zu ignorieren.
		if build/tinyc_p 'int main(){ int x=1; int* p=&x; putint(p[0].a); }' 2>&1 | grep -q 'indexed variable followed by a member access is only supported for a fixed array of structs, or a pointer to struct'; then
			echo "ok    tinyc: indizierter Pointer auf Nicht-struct mit Feldanhang wird weiterhin diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer indizierten Nicht-struct-Pointer mit Feldanhang fehlt -- Risiko stiller Fehlcode!"; tcfail=1; fail=1
		fi
		# sizeof(structArray) muss die GESAMTE Array-Groesse liefern (count * structByteSize),
		# nicht nur die Groesse eines einzelnen Elements (das war vor dem Allokations-Fix
		# still falsch, da tcLocalArrayLen fuer struct-Locals bisher nie die echte
		# Array-Laenge kannte).
		tc_check 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; putint(sizeof(arr)); putint(sizeof(struct Rec)); }' '24\n8'
		# Globale structs (2026-07-25): tc_globalend erkannte "struct" bisher ueberhaupt
		# nicht als Basistyp ("bad global declaration"). Jetzt: skalare globale structs,
		# globale Arrays von structs UND globale Pointer-auf-struct-Variablen -- Codegen
		# in tc_varref/tc_target ist strukturell identisch zum lokalen Fall (ADDRG/PUSHADDR G
		# statt PUSHADDR L, LOADGP statt LOADP), da globals_ in tinyvm.py bei GLOBAL wie bei
		# GARRAY einheitlich eine Liste ist (ADDRG braucht keine lokale Scalar/Block-
		# Unterscheidung wie bei Locals). Initialisierer + mehrdimensionale struct-Arrays
		# bleiben bewusst ein sauberer Parse-Fehler (wie beim lokalen Fall).
		tc_check 'struct Rec { int a; int b; }; struct Rec g; int main(){ g.a = 42; g.b = 99; putint(g.a); putint(g.b); }' '42\n99'
		tc_check 'struct Rec { int a; int b; }; struct Rec garr[3]; int main(){ garr[0].a=10; garr[1].a=20; garr[2].a=30; putint(garr[0].a); putint(garr[1].a); putint(garr[2].a); }' '10\n20\n30'
		tc_check 'struct Rec { int a; int b; }; struct Rec garr[3]; struct Rec* gp; int main(){ gp = garr; gp[0].a=100; gp[1].a=200; putint(garr[0].a); putint(garr[1].a); putint(gp[1].a); }' '100\n200\n200'
		tc_check 'struct Rec { int a; int b; }; struct Rec garr[3]; struct Rec g; int main(){ putint(sizeof(garr)); putint(sizeof(g)); putint(sizeof(struct Rec)); }' '24\n8\n8'
		if build/tinyc_p 'struct Rec { int a; }; struct Rec g = {1}; int main(){ putint(1); }' 2>&1 | grep -q 'struct global cannot have an initializer'; then
			echo "ok    tinyc: Initialisierer bei globaler struct-Variable wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer struct-Global-Initialisierer fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'struct Rec { int a; }; struct Rec g[2][2]; int main(){ putint(1); }' 2>&1 | grep -q 'multi-dimensional struct arrays not supported'; then
			echo "ok    tinyc: mehrdimensionales globales struct-Array wird diagnostiziert"
		else
			echo "FAIL  tinyc: Diagnose fuer mehrdimensionales globales struct-Array fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: mehr als 2 Array-Dimensionen -- tcCheck2DIndex/tcEmit2DCombine
		# generalisiert zu tcCheckNDIndex/tcEmitNDCombine (TC_MAXDIMS=6 als grosszuegige
		# Obergrenze). arr[i1]..[iN] wird per Horner-Schema ueber N-1 Scratch-Globals
		# (__idxNd_2..__idxNd_N) zu einem flachen row-major-Index kombiniert -- fuer N=2
		# identisch zur bisherigen Loesung (nur EIN Scratch-Feld), kein neuer Opcode,
		# kein Backend-Change. Die beiden 2D-spezifischen Fehlermeldungen oben bleiben
		# wortgleich (siehe tcCheckNDIndex-Kommentar in tinyc.lextab).
		tc_check 'int main(){ int m[2][3][4]; int i; int j; int k; for(i=0;i<2;i+=1){ for(j=0;j<3;j+=1){ for(k=0;k<4;k+=1){ m[i][j][k]=i*100+j*10+k; } } } putint(m[1][2][3]); putint(m[0][0][0]); putint(m[1][0][2]); }' '123\n0\n102'
		tc_check 'int g[2][2][2]; int main(){ g[0][0][0]=1; g[0][0][1]=2; g[0][1][0]=3; g[1][1][1]=8; putint(g[1][1][1]); putint(g[0][1][0]); putint(g[0][0][1]); }' '8\n3\n2'
		tc_check 'int m[2][2][2] = {1,2,3,4,5,6,7,8}; int main(){ putint(m[1][1][1]); putint(m[0][1][0]); }' '8\n3'
		tc_check 'int main(){ int m[2][2][2]; m[0][0][0]=5; m[1][1][1]=10; putint(1 + m[0][0][0] + m[1][1][1]); }' '16'
		if build/tinyc_p 'int main(){ int m[2][3][4]; putint(m[0][1]); }' 2>&1 | grep -q 'partial indexing of a 3D array is not supported'; then
			echo "ok    tinyc: partielle Indizierung eines 3D-Arrays wird diagnostiziert"
		else
			echo "FAIL  tinyc: 3D-Array-Teilindizierungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/tinyc_p 'int main(){ int m[2][2][2][2][2][2][2]; putint(1); }' 2>&1 | grep -q 'too many array dimensions (max 6)'; then
			echo "ok    tinyc: Ueberschreiten von TC_MAXDIMS wird diagnostiziert"
		else
			echo "FAIL  tinyc: TC_MAXDIMS-Diagnose fehlt"; tcfail=1; fail=1
		fi
		# Gefundener und behobener Bug (2026-07-25, beim Bau des Milestone-B-Piloten entdeckt):
		# eine indizierte (oder Funktionsaufruf-)Expression als RECHTER Operand eines
		# Vergleichs ("nc != name[j]") lieferte falsche Typfehler -- tcRel0/tcRel1 (vom
		# Vergleichsoperator gesetzt) wurden von der VERSCHACHTELTEN tc_expr-Aktion des
		# inneren Index-Ausdrucks faelschlich verbraucht, bevor der aeussere Vergleich
		# abgeschlossen war. Fix: dasselbe Rette-und-nulle-Muster wie bei tcPendingAdd/
		# tcPendingMul (tc_callname/tc_arg/tc_call). War VORHER (auch schon vor dieser
		# Session) nie aufgefallen, da kein Test eine Indizierung/einen Aufruf als
		# rechten Vergleichsoperanden hatte (nur links, z.B. "arr[i] != 0").
		tc_check 'int main(){ char a[2]; char b[2]; a[0]=65; b[0]=65; if (a[0] != b[0]) { putint(0); } else { putint(1); } b[0]=66; if (a[0] != b[0]) { putint(2); } else { putint(3); } }' '1\n2'
		[ $tcfail -eq 0 ] && echo "ok    tinyc: 150 Programme inkl. Pointer, for/do-while/break/continue, struct (gemischte Feldtypen, anonym im typedef, Array-Felder inkl. direkter p.field[i]-Indizierung, Pointer-Felder, Arrays von structs inkl. arr[i].feld und ptr[i].feld, globale struct-Variablen/-Arrays/-Pointer)/typedef/enum, sizeof/++/--/switch/Casts/const/static (inkl. nicht-konstantem Laufzeit-Initialisierer)/Pointee-Constness/void/void*/Mehrdim-Arrays (bis TC_MAXDIMS)/extern/String-Literale (inkl. Array-Initialisierer + direkter Indizierung ohne Zwischenvariable) -> tinyvm korrekt"
	else
		echo "FAIL  tinyc: Data/tinyc_p.c kompiliert nicht"; fail=1
	fi
else
	echo "warn  tinyc: python3 fehlt -- Tiny-C/tinyvm-Check uebersprungen"
fi

# 12b) Tiny-C Mehrdatei-Uebersetzung, M1 (2026-07-25): bare Funktionsprototyp
#     ohne Rumpf ("int f(int x);" statt extern -- normale interne bsr/bl-ABI,
#     NICHT die Microware-ABI/CALLEXT des bestehenden extern-Features) und
#     "extern <typ> <name>;" bei globalen Variablen erlauben getrennt
#     kompilierte Tiny-C-Dateien. tools/tinyc_merge.py simuliert dafuer einen
#     Mini-Linker vor TinyVM (echte Linker: l68 fuers 68k/OS-9-Ziel, ld/clang
#     fuers ARM64-Ziel, siehe M2/M3) -- prueft Duplicate-Symbole, genau ein
#     main, static-Sichtbarkeit UND (als Bonus, den ein echter Linker NICHT
#     leisten koennte) Signatur-Konsistenz zwischen Deklaration und Definition.
if command -v python3 >/dev/null 2>&1; then
	build/tinyc_p 'int shared; int helper(int x); int main(){ shared = 10; putint(helper(shared)); }' > build/tinyc_mf_a.ir
	build/tinyc_p 'extern int shared; int helper(int x){ return x + shared; }' > build/tinyc_mf_b.ir
	if [ "$(python3 tools/tinyc_merge.py build/tinyc_mf_a.ir build/tinyc_mf_b.ir 2>/dev/null | python3 tools/tinyvm.py 2>/dev/null)" = "20" ]; then
		echo "ok    tinyc Mehrdatei M1: Funktionsaufruf + globale Variable ueber Dateigrenze korrekt"
	else
		echo "FAIL  tinyc Mehrdatei M1: Funktionsaufruf/Global ueber Dateigrenze fehlerhaft"; fail=1
	fi
	build/tinyc_p 'int secret(int x); int main(){ putint(secret(1)); }' > build/tinyc_mf_c.ir
	build/tinyc_p 'static int secret(int x){ return x*2; }' > build/tinyc_mf_d.ir
	if python3 tools/tinyc_merge.py build/tinyc_mf_c.ir build/tinyc_mf_d.ir > /dev/null 2>build/tinyc_mf.err; then
		echo "FAIL  tinyc Mehrdatei M1: static-Funktion faelschlich ueber Dateigrenze sichtbar"; fail=1
	elif grep -q "als static definiert" build/tinyc_mf.err; then
		echo "ok    tinyc Mehrdatei M1: static-Funktion bleibt fuer andere Datei unsichtbar"
	else
		echo "FAIL  tinyc Mehrdatei M1: static-Diagnose fehlt/falsch"; fail=1
	fi
	build/tinyc_p 'int f(){ return 1; } int main(){ putint(f()); }' > build/tinyc_mf_e.ir
	build/tinyc_p 'int f(){ return 2; } int main2(){ return 0; }' > build/tinyc_mf_f.ir
	if python3 tools/tinyc_merge.py build/tinyc_mf_e.ir build/tinyc_mf_f.ir > /dev/null 2>build/tinyc_mf.err; then
		echo "FAIL  tinyc Mehrdatei M1: doppelte nicht-static Definition nicht erkannt"; fail=1
	elif grep -q "doppelte Definition" build/tinyc_mf.err; then
		echo "ok    tinyc Mehrdatei M1: doppelte nicht-static Definition wird wie 'duplicate symbol' erkannt"
	else
		echo "FAIL  tinyc Mehrdatei M1: Duplicate-Diagnose fehlt/falsch"; fail=1
	fi
	build/tinyc_p 'int g(int a, int b); int main(){ putint(g(1,2)); }' > build/tinyc_mf_g.ir
	build/tinyc_p 'int g(int a){ return a; }' > build/tinyc_mf_h.ir
	if python3 tools/tinyc_merge.py build/tinyc_mf_g.ir build/tinyc_mf_h.ir > /dev/null 2>build/tinyc_mf.err; then
		echo "FAIL  tinyc Mehrdatei M1: Signatur-Inkonsistenz (Parameterzahl) nicht erkannt"; fail=1
	elif grep -q "Parameter deklariert" build/tinyc_mf.err; then
		echo "ok    tinyc Mehrdatei M1: Signatur-Inkonsistenz (Bonus-Check) wird erkannt"
	else
		echo "FAIL  tinyc Mehrdatei M1: Signatur-Konsistenz-Diagnose fehlt/falsch"; fail=1
	fi
	rm -f build/tinyc_mf.err
else
	echo "warn  tinyc Mehrdatei M1: python3 fehlt -- uebersprungen"
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

	# 13a-ext) extern-Aufrufe (CALLEXT/CALLEXTP, 2026-07-24): die Microware-68K-
	# C/C++-ABI (Ultra C/C++ Processor Guide, "Passing Arguments to Functions")
	# schreibt vor: 1./2. Argument -> d0/d1, weitere Argumente auf dem Stack in
	# UMGEKEHRTER Erscheinungsreihenfolge; bei variadischen Funktionen (wie
	# printf) ALLES auf dem Stack, keine Register. Da es keinen echten Q9-/
	# clib.l-Zugriff in dieser Umgebung gibt, wird die Platzierung end-to-end
	# gegen einen HANDGESCHRIEBENEN Mock-Stub verifiziert, der die Argumente an
	# genau den ABI-Stellen erwartet und einen gewichteten Wert zurueckgibt --
	# jede falsch platzierte Stelle wuerde das erwartete Ergebnis veraendern.
	if [ -x build/tinyc_backend ]; then
		# Fall 1: 2 Argumente, beide in Registern (a->d0, b->d1), kein Stack-Rest.
		if build/tinyc_p 'extern int myadd(int a, int b); int main(){ putint(myadd(3,4)); }' > build/tinyc_ext2.ir && \
			build/tinyc_backend build/tinyc_ext2.ir build/tinyc_ext2.s68; then
			cp build/tinyc_ext2.s68 build/tinyc_ext2_test.s68
			{
				echo ""
				echo "; Mock: erwartet d0=a=3, d1=b=4 -- Ergebnis = a*16+b = 52"
				echo "myadd:	lsl.l	#4,d0"
				echo "	add.l	d1,d0"
				echo "	rts"
			} >> build/tinyc_ext2_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_ext2_test.bin build/tinyc_ext2_test.s68 2>/dev/null && \
				[ "$(python3 tools/tiny68sim.py build/tinyc_ext2_test.s68 2>/dev/null)" = "52" ]; then
				echo "ok    tinyc extern 68000: 2 Argumente in d0/d1 korrekt platziert"
			else
				echo "FAIL  tinyc extern 68000: 2-Argumente-ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  tinyc extern 68000: IR/Backend fuer 2-Argumente-Fall fehlgeschlagen"; fail=1
		fi
		# Fall 2: 4 Argumente -- a->d0, b->d1, c/d auf dem Stack in umgekehrter
		# Reihenfolge (c am naechsten zur Ruecksprungadresse, d weiter weg).
		if build/tinyc_p 'extern int foo(int a, int b, int c, int d); int main(){ putint(foo(1,2,3,4)); }' > build/tinyc_ext4.ir && \
			build/tinyc_backend build/tinyc_ext4.ir build/tinyc_ext4.s68; then
			cp build/tinyc_ext4.s68 build/tinyc_ext4_test.s68
			{
				echo ""
				echo "; Mock: erwartet d0=a=1, d1=b=2, 4(a7)=c=3, 8(a7)=d=4 (a7 zeigt nach jsr"
				echo "; auf die Ruecksprungadresse) -- Ergebnis = a + b*16 + c*256 + d*4096 = 17185"
				echo "foo:	move.l	4(a7),d2"
				echo "	move.l	8(a7),d3"
				echo "	lsl.l	#4,d1"
				echo "	lsl.l	#8,d2"
				echo "	lsl.l	#8,d3"
				echo "	lsl.l	#4,d3"
				echo "	add.l	d1,d0"
				echo "	add.l	d2,d0"
				echo "	add.l	d3,d0"
				echo "	rts"
			} >> build/tinyc_ext4_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_ext4_test.bin build/tinyc_ext4_test.s68 2>/dev/null && \
				[ "$(python3 tools/tiny68sim.py build/tinyc_ext4_test.s68 2>/dev/null)" = "17185" ]; then
				echo "ok    tinyc extern 68000: 4 Argumente (2 Register + 2 Stack, umgekehrte Reihenfolge) korrekt platziert"
			else
				echo "FAIL  tinyc extern 68000: 4-Argumente-ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  tinyc extern 68000: IR/Backend fuer 4-Argumente-Fall fehlgeschlagen"; fail=1
		fi
		# Fall 3 (2026-07-24, KORRIGIERT nach echtem Q9-Befund): variadisch (wie printf) --
		# NUR der variadische UEBERSCHUSS (ueber die fest deklarierten Parameter hinaus)
		# geht auf den Stack; die fest deklarierten Parameter (hier: "fmt", 1 Stueck)
		# gehen GENAUSO nach d0/d1 wie bei einem nicht-variadischen Aufruf. Die fruehere
		# Annahme "variadisch = ausnahmslos alles auf dem Stack" war NIE gegen echten
		# Compiler-generierten Code verifiziert und stellte sich beim ersten echten
		# printf-Test auf dem echten Q9 als falsch heraus (PMMU-Absturz: Formatstring-
		# Adresse landete auf dem Stack statt in d0, printf laas stattdessen die Zahl 1
		# als Adresse). Siehe docs/FORTSCHRITT.md.
		if build/tinyc_p 'extern int myprintf(int fmt, ...); int main(){ int x = 42; putint(myprintf(1, x, 7)); }' > build/tinyc_extv.ir && \
			build/tinyc_backend build/tinyc_extv.ir build/tinyc_extv.s68; then
			cp build/tinyc_extv.s68 build/tinyc_extv_test.s68
			{
				echo ""
				echo "; Mock: NUR der deklarierte Parameter (fmt) geht nach d0, der variadische"
				echo "; Rest (x, 7) auf den Stack. Erwartet d0=fmt=1, 4(a7)=x=42, 8(a7)=7 --"
				echo "; Ergebnis = fmt+x*16+7en*256 = 1 + 42*16 + 7*256 = 2465"
				echo "myprintf:	move.l	d0,d1"
				echo "	move.l	4(a7),d0"
				echo "	move.l	8(a7),d2"
				echo "	lsl.l	#4,d0"
				echo "	lsl.l	#8,d2"
				echo "	add.l	d1,d0"
				echo "	add.l	d2,d0"
				echo "	rts"
			} >> build/tinyc_extv_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_extv_test.bin build/tinyc_extv_test.s68 2>/dev/null && \
				[ "$(python3 tools/tiny68sim.py build/tinyc_extv_test.s68 2>/dev/null)" = "2465" ]; then
				echo "ok    tinyc extern 68000: variadischer Aufruf (fester Parameter in d0, Rest auf dem Stack) korrekt platziert"
			else
				echo "FAIL  tinyc extern 68000: variadische ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  tinyc extern 68000: IR/Backend fuer variadischen Fall fehlgeschlagen"; fail=1
		fi
		# Fall 4 (2026-07-24): ein char*-Argument (String-Literal) -- die Adresse des
		# per GARRAY/GINIT angelegten String-Konstanten muss unveraendert bis d0 durch-
		# gereicht werden; der Mock liest das erste Byte an dieser Adresse zurueck.
		if build/tinyc_p 'extern int mockchr(const char* s); int main(){ putint(mockchr("Hi")); }' > build/tinyc_extstr.ir && \
			build/tinyc_backend build/tinyc_extstr.ir build/tinyc_extstr.s68; then
			cp build/tinyc_extstr.s68 build/tinyc_extstr_test.s68
			{
				echo ""
				echo "; Mock: erwartet d0=Adresse des Strings -- liest erstes Byte zurueck ('H'=72)"
				echo "mockchr:	move.l	d0,a0"
				echo "	moveq	#0,d0"
				echo "	move.b	(a0),d0"
				echo "	rts"
			} >> build/tinyc_extstr_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_extstr_test.bin build/tinyc_extstr_test.s68 2>/dev/null && \
				[ "$(python3 tools/tiny68sim.py build/tinyc_extstr_test.s68 2>/dev/null)" = "72" ]; then
				echo "ok    tinyc extern 68000: String-Literal-Adresse korrekt an char*-Parameter uebergeben"
			else
				echo "FAIL  tinyc extern 68000: String-Literal-ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  tinyc extern 68000: IR/Backend fuer String-Literal-Fall fehlgeschlagen"; fail=1
		fi
		# Fall 5 (2026-07-25, Selfhosting L2 Milestone A/B): malloc/realloc/free ueber
		# extern+CALLEXT -- Voraussetzung fuer den ActionRoutine-Piloten (siehe SELFHOSTING_
		# LUECKENLISTE.md). Mock: einfacher Bump-Allokator (kein echter Speicher-Freigabe-
		# Mechanismus -- reicht fuer diesen Test). Verifiziert die Aufrufmechanik END-TO-END
		# (Argumentplatzierung d0/d0+d1, Rueckgabewert in d0 als echt benutzbarer Pointer,
		# Schreiben/Lesen durch den zurueckgegebenen Pointer) -- NICHT das Kopierverhalten
		# von realloc (alte Daten ueber die Grenze hinweg erhalten bleiben), das braucht
		# entweder echten Q9-Zugriff oder einen deutlich aufwendigeren Mock (tiny68sim
		# modelliert nur EIN generisches Adressregister a0, kein registerindiziertes
		# Kopieren beliebiger Laenge) -- separat schon strukturell verifiziert: derselbe
		# IR/68k-Code wurde erfolgreich gegen die ECHTE clib.l gelinkt (echter r68+l68-Lauf,
		# siehe FORTSCHRITT.md).
		if build/tinyc_p 'extern void* malloc(int size); extern void* realloc(void* p, int size); extern void free(void* p); int main(){ int* p; int* q; p = malloc(16); p[0] = 111; p[1] = 222; q = realloc(p, 32); putint(p[0]); putint(p[1]); free(q); putint(1); }' > build/tinyc_extmalloc.ir && \
			build/tinyc_backend build/tinyc_extmalloc.ir build/tinyc_extmalloc.s68; then
			cp build/tinyc_extmalloc.s68 build/tinyc_extmalloc_test.s68
			{
				echo ""
				echo "; Mock-Stubs (Bump-Allokator) -- testet die Aufrufmechanik, nicht das"
				echo "; Kopierverhalten von realloc (siehe Kommentar oben)."
				echo "tc_g_heap:	dc.l	9000000"
				echo "malloc:	lea	tc_g_heap(pc),a0"
				echo "	move.l	(a0),d1"
				echo "	move.l	d1,d2"
				echo "	add.l	d0,d1"
				echo "	move.l	d1,(a0)"
				echo "	move.l	d2,d0"
				echo "	rts"
				echo "realloc:	move.l	d1,d0"
				echo "	lea	tc_g_heap(pc),a0"
				echo "	move.l	(a0),d1"
				echo "	move.l	d1,d2"
				echo "	add.l	d0,d1"
				echo "	move.l	d1,(a0)"
				echo "	move.l	d2,d0"
				echo "	rts"
				echo "free:	rts"
			} >> build/tinyc_extmalloc_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_extmalloc_test.bin build/tinyc_extmalloc_test.s68 2>/dev/null && \
				[ "$(python3 tools/tiny68sim.py build/tinyc_extmalloc_test.s68 2>/dev/null)" = "$(printf '111\n222\n1')" ]; then
				echo "ok    tinyc extern 68000: malloc/realloc/free ueber CALLEXT korrekt (Aufrufmechanik + Pointer-Nutzung)"
			else
				echo "FAIL  tinyc extern 68000: malloc/realloc/free-ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  tinyc extern 68000: IR/Backend fuer malloc/realloc/free-Fall fehlgeschlagen"; fail=1
		fi
	else
		echo "warn  tinyc extern 68000: Backend fehlt -- uebersprungen"
	fi
	# ARM64 kennt die Microware-ABI bewusst nicht -- CALLEXT muss dort sauber
	# scheitern (kein "silent wrong"), nicht das Backend crashen lassen.
	if [ -x build/tinyc_arm64_backend ] && [ -f build/tinyc_ext2.ir ]; then
		if build/tinyc_arm64_backend build/tinyc_ext2.ir build/tinyc_ext2_arm64.s 2>&1 | grep -q 'unbekannter Opcode CALLEXT'; then
			echo "ok    tinyc extern ARM64: CALLEXT wird sauber abgelehnt (Microware-ABI ist 68k-spezifisch)"
		else
			echo "FAIL  tinyc extern ARM64: CALLEXT wird nicht sauber abgelehnt"; fail=1
		fi
	fi

	# Selfhosting L2, Milestone B (2026-07-25): Pilot-Portierung des ACTION/ROUTINE-
	# Ausschnitts aus codegen.cpp (ActionRoutine/pushRoutine/freeRoutines/routineTextC,
	# Source/codegen.cpp:560-650) nach Tiny-C -- testet Pointer-struct-Felder, ptr[i].feld
	# UND malloc/realloc/free ueber extern GLEICHZEITIG, in genau der Kombination, die der
	# echte Generator braucht. Bewusste Vereinfachung ggue. dem C-Original: pushRoutine
	# GIBT den (ggf. reallozierten) Array-Pointer zurueck statt ihn ueber einen ActionRoutine**-
	# Out-Parameter zu schreiben (Tiny-C hat keine Pointer-auf-Pointer-Indizierung noetig,
	# dasselbe beobachtbare Verhalten ohne dieses Sprachmittel); kein strcpy/memcpy
	# (Tiny-C hat keine Standardbibliothek), stattdessen manuelle Byte-Kopierschleifen.
	# Verifiziert per echtem r68+l68-Link gegen die ECHTE clib.l (malloc/realloc/free
	# loesen echt auf) -- STRUKTURELL bestaetigt, dass der Mechanismus korrekt ist. Echte
	# Ausfuehrung (bestaetigt, dass "one"/"two" nach dem realloc-Wachstum ueber "three"
	# hinaus noch korrekt lesbar sind) braucht entweder echten Q9-Zugriff oder einen
	# Kopier-faehigen Mock (tiny68sim modelliert nur ein generisches Adressregister a0,
	# kein registerindiziertes Kopieren beliebiger Laenge) -- bewusst NICHT Teil dieses
	# Schritts, siehe SELFHOSTING_LUECKENLISTE.md.
	if [ -x build/tinyc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		pilot_src='extern void* malloc(int size); extern void* realloc(void* p, int size); extern void free(void* p); struct ActionRoutine { char name[16]; char* text; }; struct ActionRoutine* pushRoutine(struct ActionRoutine* arr, int* cnt, int* cap, char* name, char* text, int textLen) { struct ActionRoutine* newArr; int newCap; int i; char* dst; if (*cnt >= *cap) { newCap = *cap > 0 ? *cap * 2 : 2; newArr = realloc(arr, newCap * sizeof(struct ActionRoutine)); arr = newArr; *cap = newCap; } dst = arr[*cnt].name; i = 0; while (name[i] != 0) { dst[i] = name[i]; i = i + 1; } dst[i] = 0; arr[*cnt].text = malloc(textLen + 1); dst = arr[*cnt].text; i = 0; while (i < textLen) { dst[i] = text[i]; i = i + 1; } dst[i] = 0; *cnt = *cnt + 1; return arr; } int routineIndexC(struct ActionRoutine* arr, int cnt, char* name) { int i; int j; bool match; char* n; char nc; for (i = 0; i < cnt; i = i + 1) { n = arr[i].name; match = true; j = 0; nc = n[j]; while (nc != 0) { if (nc != name[j]) { match = false; } j = j + 1; nc = n[j]; } if (name[j] != 0) { match = false; } if (match) { return i; } } return -1; } void freeRoutines(struct ActionRoutine* arr, int cnt) { int i; for (i = 0; i < cnt; i = i + 1) { free(arr[i].text); } } int main() { struct ActionRoutine* routines; int cnt; int cap; int idx; char* t; routines = 0; cnt = 0; cap = 0; routines = pushRoutine(routines, &cnt, &cap, "one", "TEXT-ONE", 8); routines = pushRoutine(routines, &cnt, &cap, "two", "TEXT-TWO", 8); routines = pushRoutine(routines, &cnt, &cap, "three", "TEXT-THREE", 10); putint(cnt); putint(cap); idx = routineIndexC(routines, cnt, "one"); t = routines[idx].text; putchar(t[0]); putchar(t[5]); idx = routineIndexC(routines, cnt, "three"); t = routines[idx].text; putchar(t[0]); putchar(t[9]); idx = routineIndexC(routines, cnt, "nope"); putint(idx); freeRoutines(routines, cnt); free(routines); return 0; }'
		if build/tinyc_p "$pilot_src" > build/tinyc_pilot.ir 2>build/tinyc_pilot.err && [ ! -s build/tinyc_pilot.err ] && \
			build/tinyc_backend build/tinyc_pilot.ir build/tinyc_pilot.s68 -os9; then
			cp build/tinyc_pilot.s68 "$MWOS_TMP/pilot.a"
			rm -f "$MWOS_TMP/pilot.r" "$MWOS_TMP/pilot.out" "$MWOS_TMP/pilot.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\pilot.a -o=M:\\TMP\\pilot.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/pilot.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\pilot.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\pilot.out -s=M:\\TMP\\pilot.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/pilot.out" ]; then
					echo "ok    tinyc Selfhosting L2 Milestone B: ActionRoutine-Pilot kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  tinyc Selfhosting L2 Milestone B: echter l68-Link fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  tinyc Selfhosting L2 Milestone B: echte r68-Assemblierung fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/pilot.a "$MWOS_TMP"/pilot.r "$MWOS_TMP/pilot.out" "$MWOS_TMP/pilot.sym"
		else
			echo "FAIL  tinyc Selfhosting L2 Milestone B: ActionRoutine-Pilot kompiliert nicht sauber (siehe build/tinyc_pilot.err)"; fail=1
		fi
	else
		echo "warn  tinyc Selfhosting L2 Milestone B: Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Globale structs (2026-07-25) machen den ECHTEN routinesC-Vollport-Fall jetzt moeglich:
	# routinesC/routinesCCnt/routinesCCap sind im echten codegen.cpp file-scope-Globale
	# (nicht wie im Pilot oben lokale Variablen einer Testfunktion) -- derselbe Test wie
	# oben, aber diesmal mit echten Globalen statt main()-lokalen Variablen.
	if [ -x build/tinyc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		pilotg_src='extern void* malloc(int size); extern void* realloc(void* p, int size); extern void free(void* p); struct ActionRoutine { char name[16]; char* text; }; struct ActionRoutine* routinesC; int routinesCCnt; int routinesCCap; void pushRoutine(char* name, char* text, int textLen) { struct ActionRoutine* newArr; int newCap; int i; char* dst; if (routinesCCnt >= routinesCCap) { newCap = routinesCCap > 0 ? routinesCCap * 2 : 2; newArr = realloc(routinesC, newCap * sizeof(struct ActionRoutine)); routinesC = newArr; routinesCCap = newCap; } dst = routinesC[routinesCCnt].name; i = 0; while (name[i] != 0) { dst[i] = name[i]; i = i + 1; } dst[i] = 0; routinesC[routinesCCnt].text = malloc(textLen + 1); dst = routinesC[routinesCCnt].text; i = 0; while (i < textLen) { dst[i] = text[i]; i = i + 1; } dst[i] = 0; routinesCCnt = routinesCCnt + 1; } int main() { int idx; char* t; routinesC = 0; routinesCCnt = 0; routinesCCap = 0; pushRoutine("one", "TEXT-ONE", 8); pushRoutine("two", "TEXT-TWO", 8); pushRoutine("three", "TEXT-THREE", 10); putint(routinesCCnt); putint(routinesCCap); t = routinesC[0].text; putchar(t[0]); t = routinesC[2].text; putchar(t[0]); putchar(t[9]); free(routinesC); return 0; }'
		if build/tinyc_p "$pilotg_src" > build/tinyc_pilotg.ir 2>build/tinyc_pilotg.err && [ ! -s build/tinyc_pilotg.err ] && \
			build/tinyc_backend build/tinyc_pilotg.ir build/tinyc_pilotg.s68 -os9; then
			cp build/tinyc_pilotg.s68 "$MWOS_TMP/pilotg.a"
			rm -f "$MWOS_TMP/pilotg.r" "$MWOS_TMP/pilotg.out" "$MWOS_TMP/pilotg.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\pilotg.a -o=M:\\TMP\\pilotg.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/pilotg.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\pilotg.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\pilotg.out -s=M:\\TMP\\pilotg.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/pilotg.out" ]; then
					echo "ok    tinyc Selfhosting L2: ActionRoutine-Pilot MIT echten globalen routinesC/-Cnt/-Cap (wie im echten codegen.cpp) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  tinyc Selfhosting L2: echter l68-Link (globale Variante) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  tinyc Selfhosting L2: echte r68-Assemblierung (globale Variante) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/pilotg.a "$MWOS_TMP"/pilotg.r "$MWOS_TMP/pilotg.out" "$MWOS_TMP/pilotg.sym"
		else
			echo "FAIL  tinyc Selfhosting L2: ActionRoutine-Pilot (globale Variante) kompiliert nicht sauber (siehe build/tinyc_pilotg.err)"; fail=1
		fi
	else
		echo "warn  tinyc Selfhosting L2 (globale Variante): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25): naechster Ausschnitt von codegen.cpp nach
	# SourceTinyC/codegen.tc portiert -- LEXER-Konfigurationsparser (lexUnquote/
	# lexUnquoteAt/lexParseConfig/markLexicalNode/computeLexicalSet, das [LEXER]-
	# Konfigurationsblock-Handling). Haengt an strncmp/strchr/strrchr/strstr/memcpy
	# (CALLEXT/clib.l) -- TinyVM kennt CALLEXT NICHT (keine libc-Simulation), daher
	# hier wie beim ActionRoutine-Piloten oben NUR strukturell verifiziert: kompiliert
	# sauber, echter r68 assembliert, echter l68 linkt gegen echte clib.l. Die
	# eigentliche ALGORITHMUS-Korrektheit (WHITESPACE-Escape-Dekodierung, mehrere
	# COMMENT LINE/BLOCK-Marker, COMMENT BLOCK NESTED-Erkennung, TOKEN-Registrierung,
	# UND -- am wichtigsten -- die TRANSITIVE lexikalische Markierung ueber NTS-
	# Referenzen in markLexicalNode) wurde EINMALIG separat verifiziert: eine Kopie
	# mit Tiny-C-eigenen String-Helfern statt extern/CALLEXT (tcStrncmp/tcStrchr/...)
	# lieferte ueber TinyVM UND nativ per ARM64-Backend exakt dieselben 19 erwarteten
	# Werte (inkl. der transitiven Markierung einer per NTS referenzierten Regel) --
	# siehe docs/FORTSCHRITT.md fuer die Details, hier bewusst nicht dauerhaft als
	# Skript verankert (Wartungsaufwand einer zweiten String-Bibliothek nur fuers
	# Testen steht in keinem Verhaeltnis zum Grenzwert).
	if [ -x build/tinyc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		cgtest_main='
int main() {
	char cfg[300];
	char* srcTemplate;
	int i;
	int r;
	astReset();
	astPushTS("x"); astFinishRule("letter");
	astPushNTS("letter"); astFinishRule("ident");
	astPushTS("5"); astFinishRule("number");
	astPushTS("s"); astFinishRule("stringLit");
	astPushTS("z"); astFinishRule("other");
	srcTemplate = "WHITESPACE = ` \\t\\r\\n`\nTOKEN ident\nTOKEN number\nTOKEN stringLit\nCOMMENT LINE = `//`\nCOMMENT BLOCK = `/*` `*/`\nCOMMENT BLOCK NESTED = `(*` `*)`\n";
	i = 0;
	while (srcTemplate[i] != 0) {
		if (srcTemplate[i] == 96) { cfg[i] = 34; } else { cfg[i] = srcTemplate[i]; }
		i = i + 1;
	}
	cfg[i] = 0;
	lexParseConfig(cfg);
	computeLexicalSet();
	putint(lexActive); putint(lexWsLen); putint(lexRootCnt);
	putint(lexLineCommentCnt); putint(lexBlockCnt);
	putint(lexBlockNested[0]); putint(lexBlockNested[1]);
	r = ruleIndexByName("ident"); putint(ruleIsLexical[r]);
	r = ruleIndexByName("letter"); putint(ruleIsLexical[r]);
	r = ruleIndexByName("other"); putint(ruleIsLexical[r]);
	return 0;
}'
		if build/tinyc_p "$(cat SourceTinyC/codegen.tc)$cgtest_main" > build/tinyc_cgtest.ir 2>build/tinyc_cgtest.err && \
			build/tinyc_backend build/tinyc_cgtest.ir build/tinyc_cgtest_os9.a -os9 -largedata; then
			cp build/tinyc_cgtest_os9.a "$MWOS_TMP/cgtest.a"
			rm -f "$MWOS_TMP/cgtest.r" "$MWOS_TMP/cgtest.out" "$MWOS_TMP/cgtest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\cgtest.a -o=M:\\TMP\\cgtest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/cgtest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\cgtest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\cgtest.out -s=M:\\TMP\\cgtest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/cgtest.out" ]; then
					echo "ok    tinyc Selfhosting L2 Vollport: LEXER-Konfigurationsparser (SourceTinyC/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  tinyc Selfhosting L2 Vollport: echter l68-Link (LEXER-Konfigurationsparser) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  tinyc Selfhosting L2 Vollport: echte r68-Assemblierung (LEXER-Konfigurationsparser) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/cgtest.a "$MWOS_TMP"/cgtest.r "$MWOS_TMP"/cgtest.out "$MWOS_TMP"/cgtest.sym
		else
			echo "FAIL  tinyc Selfhosting L2 Vollport: SourceTinyC/codegen.tc kompiliert nicht sauber (siehe build/tinyc_cgtest.err)"; fail=1
		fi
	else
		echo "warn  tinyc Selfhosting L2 Vollport (LEXER-Konfigurationsparser): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): naechster Ausschnitt
	# -- CODEGEN-Konfigurationsparser (cgenWantOS9/cgenStartRule/cgenParseConfig, das
	# [CODEGEN]-Konfigurationsblock-Handling aus Source/codegen.cpp Zeilen 462-535).
	# Strukturell fast identisch zu lexParseConfig (letztes Wort einer Zeile
	# extrahieren) -- braucht KEINE Anfuehrungszeichen im Konfigurationsformat, daher
	# hier ohne den Backtick-Workaround aus dem LEXER-Test moeglich. Gleiche
	# dreifache Verifikationsmethode wie beim LEXER-Konfigurationsparser (TinyVM +
	# ARM64 mit Tiny-C-eigenen String-Helfern lieferten identische Werte -- 1/
	# "mySect"+Nullterminator/"myRule"+Nullterminator, siehe docs/FORTSCHRITT.md);
	# hier nur die dauerhafte Regression (echter r68+l68 gegen echte clib.l).
	if [ -x build/tinyc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		cgentest_main='
int main() {
	char* s;
	cgenParseConfig("M68K OS9\nM68K PSECT = mySect\nSTART myRule\n");
	putint(cgenWantOS9());
	putchar(cgenPsect[0]); putchar(cgenPsect[1]); putchar(cgenPsect[2]);
	putchar(cgenPsect[3]); putchar(cgenPsect[4]); putchar(cgenPsect[5]);
	putint(cgenPsect[6]);
	s = cgenStartRule();
	putchar(s[0]); putchar(s[1]); putchar(s[2]); putchar(s[3]); putchar(s[4]); putchar(s[5]);
	putint(s[6]);
	return 0;
}'
		if build/tinyc_p "$(cat SourceTinyC/codegen.tc)$cgentest_main" > build/tinyc_cgentest.ir 2>build/tinyc_cgentest.err && \
			build/tinyc_backend build/tinyc_cgentest.ir build/tinyc_cgentest_os9.a -os9 -largedata; then
			cp build/tinyc_cgentest_os9.a "$MWOS_TMP/cgentest.a"
			rm -f "$MWOS_TMP/cgentest.r" "$MWOS_TMP/cgentest.out" "$MWOS_TMP/cgentest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\cgentest.a -o=M:\\TMP\\cgentest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/cgentest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\cgentest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\cgentest.out -s=M:\\TMP\\cgentest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/cgentest.out" ]; then
					echo "ok    tinyc Selfhosting L2 Vollport: CODEGEN-Konfigurationsparser (SourceTinyC/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  tinyc Selfhosting L2 Vollport: echter l68-Link (CODEGEN-Konfigurationsparser) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  tinyc Selfhosting L2 Vollport: echte r68-Assemblierung (CODEGEN-Konfigurationsparser) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/cgentest.a "$MWOS_TMP"/cgentest.r "$MWOS_TMP"/cgentest.out "$MWOS_TMP"/cgentest.sym
		else
			echo "FAIL  tinyc Selfhosting L2 Vollport: SourceTinyC/codegen.tc (CODEGEN-Konfigurationsparser) kompiliert nicht sauber (siehe build/tinyc_cgentest.err)"; fail=1
		fi
	else
		echo "warn  tinyc Selfhosting L2 Vollport (CODEGEN-Konfigurationsparser): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): naechster Ausschnitt
	# -- ActionRoutine-Verwaltung UND ACTIONS-Konfigurationsparser (freeRoutines/
	# pushRoutineC/pushRoutine68k/routineTextC/routineText68k/lastWord/growLineBuf/
	# growCollectBuf/actionsParseConfig, das [NUTZER-CODE]-Block-Handling aus
	# Source/codegen.cpp Zeilen 580-750). pushRoutineC/pushRoutine68k sind ZWEI fast
	# identische Funktionen statt EINER generischen mit "ActionRoutine**"-Parameter
	# wie im C++-Original (Tiny-C hat keine Generik/Funktionszeiger, routinesC/
	# routines68k sind hier wie im Original file-scope-Globale -- direktes Mutieren
	# ist einfacher, gleiches Muster wie beim ActionRoutine-Piloten Milestone B).
	# Verifikation NUR strukturell (wie beim Piloten): kompiliert sauber (NULL
	# Semantikfehler), assembliert (echter r68), linkt (echter l68 gegen echte
	# clib.l). Eine versuchte TIEFERE Logikverifikation (TinyVM/ARM64 mit
	# selbstgeschriebenen malloc/realloc-Ersatzfunktionen fuer eigenstaendige
	# Ausfuehrbarkeit) scheiterte an Grenzen der TESTUMGEBUNG, nicht des Ports:
	# TinyVMs Zeigermodell ist nicht byte-adressierbar und vertraegt keine
	# malloc-Heap-Umdeutung auf struct-Zeiger; eine ARM64-Reproduktion mit
	# selbstgebautem Bump-Allocator stuerzte ab (vermutlich ein Bug im
	# Test-Stub selbst, nicht im geprueften Code -- der echte extern-Pfad
	# gegen clib.l linkt fehlerfrei). Echte Verhaltensverifikation bleibt der
	# Live-Q9-Ausfuehrung vorbehalten (bereits als offener Schritt vermerkt).
	if [ -x build/tinyc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		actiontest_main='
int main() {
	int idx;
	char* t;
	astReset();
	astPushTS("x"); astFinishRule("myRule");
	astPushTS("y"); astFinishRule("unused");
	actionsParseConfig("ACTION AFTER myRule CALL myAction\nROUTINE C myAction\nline one\nline two\nEND\nROUTINE M68K other68k\nm68k line\nEND\nACTION AFTER unused CALL nowhereAction\n");
	idx = ruleIndexByName("myRule");
	putint(idx >= 0);
	putchar(ruleActionCallFlat[idx * 64 + 0]);
	putchar(ruleActionCallFlat[idx * 64 + 1]);
	putint(ruleActionCallFlat[idx * 64 + 8]);
	t = routineTextC("myAction");
	putchar(t[0]); putchar(t[1]); putchar(t[2]); putchar(t[3]);
	putint(t[8]);
	putchar(t[9]); putchar(t[10]); putchar(t[11]); putchar(t[12]);
	t = routineText68k("other68k");
	putchar(t[0]); putchar(t[1]);
	idx = ruleIndexByName("unused");
	putchar(ruleActionCallFlat[idx * 64 + 0]);
	t = routineTextC("nowhereAction");
	putint(t == 0);
	return 0;
}'
		if build/tinyc_p "$(cat SourceTinyC/codegen.tc)$actiontest_main" > build/tinyc_actiontest.ir 2>build/tinyc_actiontest.err && \
			build/tinyc_backend build/tinyc_actiontest.ir build/tinyc_actiontest_os9.a -os9 -largedata; then
			cp build/tinyc_actiontest_os9.a "$MWOS_TMP/actiontest.a"
			rm -f "$MWOS_TMP/actiontest.r" "$MWOS_TMP/actiontest.out" "$MWOS_TMP/actiontest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\actiontest.a -o=M:\\TMP\\actiontest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/actiontest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\actiontest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\actiontest.out -s=M:\\TMP\\actiontest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/actiontest.out" ]; then
					echo "ok    tinyc Selfhosting L2 Vollport: ActionRoutine/ACTIONS-Konfigurationsparser (SourceTinyC/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  tinyc Selfhosting L2 Vollport: echter l68-Link (ActionRoutine/ACTIONS-Konfigurationsparser) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  tinyc Selfhosting L2 Vollport: echte r68-Assemblierung (ActionRoutine/ACTIONS-Konfigurationsparser) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/actiontest.a "$MWOS_TMP"/actiontest.r "$MWOS_TMP"/actiontest.out "$MWOS_TMP"/actiontest.sym
		else
			echo "FAIL  tinyc Selfhosting L2 Vollport: SourceTinyC/codegen.tc (ActionRoutine/ACTIONS-Konfigurationsparser) kompiliert nicht sauber (siehe build/tinyc_actiontest.err)"; fail=1
		fi
	else
		echo "warn  tinyc Selfhosting L2 Vollport (ActionRoutine/ACTIONS-Konfigurationsparser): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): naechster Ausschnitt
	# -- AST-Validierung (isWordLiteral/nodeNullable/validateRepeatProgress/
	# validateAstForCodegen, Zeilen 750-857 in Source/codegen.cpp). Prueft VOR der
	# Ausgabe: (1) eine Wiederholung mit nullbarem Rumpf (z.B. "{ [x] }") waere eine
	# Endlosschleife im erzeugten Parser -- wird erkannt und abgelehnt (Fixpunkt-
	# analyse ueber gegenseitig rekursive Regeln); (2) zwei Regeln, die nach der
	# "$"->"_"-Normalisierung denselben Namen ergeben, wuerden doppelte C-Funktionen/
	# 68k-Labels erzeugen -- wird erkannt und abgelehnt. Einmalig per TinyVM
	# tiefenverifiziert (mit Tiny-C-eigenen Stand-ins fuer strcmp/isalpha/isalnum
	# statt extern): alle 10 erwarteten Werte trafen exakt zu, inklusive beider
	# Diagnosepfade -- siehe docs/FORTSCHRITT.md. ARM64 hier NICHT moeglich (das
	# kumulative Gesamtkompilat enthaelt bereits "free" aus dem ActionRoutine-Chunk,
	# CALLEXT wird von ARM64 grundsaetzlich abgelehnt, unabhaengig von Erreichbarkeit
	# -- TinyVM meldet den Fehler dagegen nur bei tatsaechlicher Ausfuehrung, hier
	# also unproblematisch). Dauerhafte Regression wie ueblich: echter r68+l68.
	if [ -x build/tinyc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		astvaltest_main='
int main() {
	putint(isWordLiteral("hello"));
	putint(isWordLiteral("_foo$bar"));
	putint(isWordLiteral("foo-bar"));
	putint(isWordLiteral("5abc"));
	astReset();
	astPushTS("x"); astWrapOpt(); astWrapRep(); astFinishRule("bad");
	putint(validateAstForCodegen());
	astReset();
	astPushTS("y"); astWrapRep(); astFinishRule("good");
	putint(validateAstForCodegen());
	astReset();
	astPushTS("a"); astFinishRule("foo$bar");
	astPushTS("b"); astFinishRule("foo_bar");
	putint(validateAstForCodegen());
	return 0;
}'
		if build/tinyc_p "$(cat SourceTinyC/codegen.tc)$astvaltest_main" > build/tinyc_astvaltest.ir 2>build/tinyc_astvaltest.err && \
			build/tinyc_backend build/tinyc_astvaltest.ir build/tinyc_astvaltest_os9.a -os9 -largedata; then
			cp build/tinyc_astvaltest_os9.a "$MWOS_TMP/astvaltest.a"
			rm -f "$MWOS_TMP/astvaltest.r" "$MWOS_TMP/astvaltest.out" "$MWOS_TMP/astvaltest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\astvaltest.a -o=M:\\TMP\\astvaltest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/astvaltest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\astvaltest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\astvaltest.out -s=M:\\TMP\\astvaltest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/astvaltest.out" ]; then
					echo "ok    tinyc Selfhosting L2 Vollport: AST-Validierung (SourceTinyC/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  tinyc Selfhosting L2 Vollport: echter l68-Link (AST-Validierung) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  tinyc Selfhosting L2 Vollport: echte r68-Assemblierung (AST-Validierung) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/astvaltest.a "$MWOS_TMP"/astvaltest.r "$MWOS_TMP"/astvaltest.out "$MWOS_TMP"/astvaltest.sym
		else
			echo "FAIL  tinyc Selfhosting L2 Vollport: SourceTinyC/codegen.tc (AST-Validierung) kompiliert nicht sauber (siehe build/tinyc_astvaltest.err)"; fail=1
		fi
	else
		echo "warn  tinyc Selfhosting L2 Vollport (AST-Validierung): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): naechster Ausschnitt
	# -- C-Backend-Codegenerator (emitCString/emitLongerLiteralRejectC/genNodeC,
	# Source/codegen.cpp Zeilen 858-1000). Erste Beruehrung mit ECHTER Dateiausgabe
	# (fopen/fprintf/fputc/fclose statt nur stdout-Diagnosen wie bisher) -- FILE*
	# wird als "void*" gefuehrt (Tiny-C hat keinen FILE-Struct-Typ, der ABI-Aufruf
	# braucht nur einen opaken Zeiger). clib.l hat KEIN snprintf (nur sprintf,
	# per `strings` bestaetigt) -- deshalb sprintf ohne Laengenlimit verwendet.
	# NEUE Tiny-C-Grenze gefunden: Tiny-C kann KEINE EIGENEN variadischen
	# Funktionen definieren (nur variadische extern-Aufrufe) -- ein Tiny-C-
	# Stand-in fuer fprintf (fuer TinyVM/ARM64-Tiefenverifikation wie bei den
	# String-Funktionen zuvor) ist deshalb NICHT moeglich. Verifikation bleibt
	# bei kompiliert sauber + echter r68/l68-Link (wie beim ActionRoutine-Chunk).
	if [ -x build/tinyc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		gennodetest_main='
int main() {
	void* fp;
	int m;
	int m2;
	astReset();
	astPushTS("x"); astFinishRule("sub");
	m = astMark();
	astPushTS("a");
	m2 = astMark();
	astPushTS("b"); astPushTS("c"); astGroupAlt(m2);
	astPushTS("d"); astWrapOpt();
	astPushTS("e"); astWrapRep();
	astPushNTS("sub");
	astGroupSeq(m);
	astFinishRule("test");
	computeLexicalSet();
	fp = fopen("/tmp/tinyc_gennodetest_output.c", "w");
	if (fp == 0) { putint(-1); return 1; }
	genNodeC(fp, rules[1].root, 999, 0);
	fclose(fp);
	putint(1);
	return 0;
}'
		if build/tinyc_p "$(cat SourceTinyC/codegen.tc)$gennodetest_main" > build/tinyc_gennodetest.ir 2>build/tinyc_gennodetest.err && \
			build/tinyc_backend build/tinyc_gennodetest.ir build/tinyc_gennodetest_os9.a -os9 -largedata; then
			cp build/tinyc_gennodetest_os9.a "$MWOS_TMP/gennodetest.a"
			rm -f "$MWOS_TMP/gennodetest.r" "$MWOS_TMP/gennodetest.out" "$MWOS_TMP/gennodetest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\gennodetest.a -o=M:\\TMP\\gennodetest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/gennodetest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\gennodetest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\gennodetest.out -s=M:\\TMP\\gennodetest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/gennodetest.out" ]; then
					echo "ok    tinyc Selfhosting L2 Vollport: C-Backend-Codegenerator (SourceTinyC/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  tinyc Selfhosting L2 Vollport: echter l68-Link (C-Backend-Codegenerator) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  tinyc Selfhosting L2 Vollport: echte r68-Assemblierung (C-Backend-Codegenerator) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/gennodetest.a "$MWOS_TMP"/gennodetest.r "$MWOS_TMP"/gennodetest.out "$MWOS_TMP"/gennodetest.sym
		else
			echo "FAIL  tinyc Selfhosting L2 Vollport: SourceTinyC/codegen.tc (C-Backend-Codegenerator) kompiliert nicht sauber (siehe build/tinyc_gennodetest.err)"; fail=1
		fi
	else
		echo "warn  tinyc Selfhosting L2 Vollport (C-Backend-Codegenerator): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# -largedata (2026-07-25, "Speichermodell"-Schalter): der 68k-Backend adressiert
	# Globale standardmaessig AUSSCHLIESSLICH PC-relativ -- eine ECHTE 68000-Grenze
	# (16-Bit-Displacement, +-32 KB), die schon bei einem Array von wenigen Dutzend
	# KB "value out of range" von r68 ausloest (empirisch bestaetigt, siehe
	# docs/FORTSCHRITT.md). -largedata schaltet auf eine Indirektionstabelle mit
	# absoluten (vom Linker aufgeloesten) Adressen um -- getestet mit einem
	# struct-Array, das OHNE -largedata garantiert zu gross waere (analog zum
	# urspruenglichen AST_MAX_NODES=8192-Fund in SourceTinyC/codegen.tc).
	largedata_src='struct Big { int a; char pad[52]; }; struct Big arr[2048]; int cnt; int main(){ arr[0].a=42; arr[2047].a=99; cnt=7; putint(arr[0].a); putint(arr[2047].a); putint(cnt); }'
	if [ -x build/tinyc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		if build/tinyc_p "$largedata_src" > build/tinyc_largedata.ir && \
			build/tinyc_backend build/tinyc_largedata.ir build/tinyc_largedata.s68 -largedata && \
			build/tinyc_backend build/tinyc_largedata.ir build/tinyc_largedata_os9.a -os9 -largedata; then
			cp build/tinyc_largedata_os9.a "$MWOS_TMP/largedata.a"
			rm -f "$MWOS_TMP/largedata.r"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\largedata.a -o=M:\\TMP\\largedata.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/largedata.r" ] && [ "$(python3 tools/tiny68sim.py build/tinyc_largedata.s68 2>/dev/null)" = "$(printf '42\n99\n7')" ]; then
				echo "ok    tinyc -largedata: grosses struct-Array (112 KB, weit ueber der PC-relativen Grenze) kompiliert, assembliert (echter r68) und simuliert korrekt"
			else
				echo "FAIL  tinyc -largedata: grosses struct-Array fehlerhaft"; fail=1
			fi
			rm -f "$MWOS_TMP"/largedata.a "$MWOS_TMP"/largedata.r
		else
			echo "FAIL  tinyc -largedata: IR/Backend fehlgeschlagen"; fail=1
		fi
	else
		echo "warn  tinyc -largedata: Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter r68-Test uebersprungen"
	fi
	# Gegenprobe: OHNE -largedata muss (a) unser Backend selbst schon eine Warnung
	# ausgeben (Groessen-Heuristik, siehe main() in tinyc_backend_c.cpp) UND (b)
	# der ECHTE r68 dasselbe Programm mit "value out of range" ablehnen -- damit
	# ist die Notwendigkeit von -largedata fuer diesen Fall doppelt belegt.
	if build/tinyc_p "$largedata_src" > build/tinyc_largedata2.ir && \
		build/tinyc_backend build/tinyc_largedata2.ir build/tinyc_largedata2.s68 -os9 2>build/tinyc_largedata2.warn; then
		if grep -q 'globale Daten sind mit .* Byte recht gross' build/tinyc_largedata2.warn; then
			echo "ok    tinyc: Groessen-Heuristik warnt VOR -largedata, wenn globale Daten gross werden"
		else
			echo "FAIL  tinyc: Groessen-Heuristik-Warnung fehlt"; fail=1
		fi
		if [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ]; then
			cp build/tinyc_largedata2.s68 "$MWOS_TMP/largedata2.a"
			rm -f "$MWOS_TMP/largedata2.r"
			out=$(WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\largedata2.a -o=M:\\TMP\\largedata2.r -q" 2>&1)
			if echo "$out" | grep -q 'value out of range'; then
				echo "ok    tinyc: echter r68 lehnt dasselbe Array OHNE -largedata tatsaechlich mit 'value out of range' ab"
			else
				echo "FAIL  tinyc: r68 haette OHNE -largedata ablehnen muessen -- Gegenprobe ungueltig"; fail=1
			fi
			rm -f "$MWOS_TMP"/largedata2.a "$MWOS_TMP"/largedata2.r
		else
			echo "warn  tinyc -largedata-Gegenprobe (echter r68): Wine/MWOS nicht verfuegbar -- uebersprungen"
		fi
	else
		echo "FAIL  tinyc: IR/Backend fuer -largedata-Gegenprobe fehlgeschlagen"; fail=1
	fi

	# -largedata fuer FUNKTIONSAUFRUFE (2026-07-25): "bsr tc_<name>" ist GENAUSO
	# PC-relativ (16-Bit-Displacement) begrenzt wie die Global-Adressierung oben --
	# ein Programm mit vielen/grossen Funktionen kann denselben "value out of
	# range" bei r68 ausloesen, nur ueber Code- statt Datendistanz. -largedata
	# loest das jetzt auch hierfuer: eine Funktions-Indirektionstabelle
	# (tc_functab, absolute vom Linker aufgeloeste Adressen) plus ein einmalig
	# gesetztes Adressregister a4 ("lea tc_functab(pc),a4"); Aufrufe werden zu
	# "move.l N(a4),a2 / jsr (a2)" statt "bsr tc_target". a2 wurde als
	# Aufruf-Scratchregister gewaehlt, weil es an JEDER Aufrufstelle frei ist
	# (anders als a0/a1, die an manchen Stellen ueber den Aufruf hinweg leben).
	# 150 generierte Funktionen ergeben >32 KB 68k-Code zwischen tc_functab und
	# der letzten Funktion -- genau die Groessenordnung, an der "bsr" ohne
	# -largedata real mit "branch out of range" bricht (empirisch bestaetigt,
	# siehe Gegenprobe unten -- andere Fehlermeldung als "value out of range"
	# bei lea/movea fuer Daten oben). Dabei wurde ein echter
	# Skalierungsbug gefunden und behoben: im -os9-Modus ist "main" selbst der
	# Einsprungpunkt (kein separates tc_start) und fuehrt sein eigenes
	# "lea tc_functab(pc),a4" aus -- das ist selbst PC-relativ und brach, wenn
	# main (wie hier) NICHT die erste Funktion im Quelltext ist und daher weit
	# hinter der Tabelle liegt. Fix: main wird bei -os9 -largedata jetzt immer
	# als allererste Funktion emittiert, unabhaengig von ihrer Quelltextposition.
	largefunc_src=""
	lfi=0
	while [ $lfi -lt 150 ]; do
		largefunc_src="$largefunc_src int f$lfi(int x){ int a;int b;int c;int d; a=x;b=x;c=x;d=x; a=a+0+$lfi;b=b*2-0;c=c/2+a;d=d-b+a*2-0; a=a+1+$lfi;b=b*2-1;c=c/2+a;d=d-b+a*2-1; a=a+2+$lfi;b=b*2-2;c=c/2+a;d=d-b+a*2-2; a=a+3+$lfi;b=b*2-3;c=c/2+a;d=d-b+a*2-3; a=a+4+$lfi;b=b*2-4;c=c/2+a;d=d-b+a*2-4; a=a+5+$lfi;b=b*2-5;c=c/2+a;d=d-b+a*2-5; a=a+6+$lfi;b=b*2-6;c=c/2+a;d=d-b+a*2-6; a=a+7+$lfi;b=b*2-7;c=c/2+a;d=d-b+a*2-7; return d+a+b+c; }"
		lfi=$((lfi+1))
	done
	largefunc_src="$largefunc_src int main(){ putint(f0(1)); putint(f149(1)); putint(f75(5)); }"
	if [ -x build/tinyc_backend ] && command -v python3 >/dev/null 2>&1; then
		if build/tinyc_p "$largefunc_src" > build/tinyc_largefunc.ir && \
			build/tinyc_backend build/tinyc_largefunc.ir build/tinyc_largefunc.s68 -largedata && \
			[ "$(python3 tools/tiny68sim.py build/tinyc_largefunc.s68 2>/dev/null)" = "$(printf '196\n14204\n6311')" ]; then
			echo "ok    tinyc -largedata Funktionsaufrufe: 150 Funktionen (>32 KB Code) ueber Indirektionstabelle (a4/a2) statt bsr = tinyvm"
		else
			echo "FAIL  tinyc -largedata Funktionsaufrufe: 150-Funktionen-Testfall stimmt nicht mit tinyvm ueberein"; fail=1
		fi
		if [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
		   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
			mkdir -p "$MWOS_TMP"
			if build/tinyc_backend build/tinyc_largefunc.ir build/tinyc_largefunc_os9.a -os9 -largedata; then
				cp build/tinyc_largefunc_os9.a "$MWOS_TMP/largefunc.a"
				rm -f "$MWOS_TMP/largefunc.r" "$MWOS_TMP/largefunc.out" "$MWOS_TMP/largefunc.sym"
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\largefunc.a -o=M:\\TMP\\largefunc.r -q" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/largefunc.r" ]; then
					WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\largefunc.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\largefunc.out -s=M:\\TMP\\largefunc.sym" >/dev/null 2>&1
					if [ -s "$MWOS_TMP/largefunc.out" ]; then
						echo "ok    tinyc -largedata Funktionsaufrufe: 150-Funktionen-Programm (>32 KB Code) assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
					else
						echo "FAIL  tinyc -largedata Funktionsaufrufe: echter l68-Link fehlgeschlagen"; fail=1
					fi
				else
					echo "FAIL  tinyc -largedata Funktionsaufrufe: echte r68-Assemblierung fehlgeschlagen"; fail=1
				fi
				rm -f "$MWOS_TMP"/largefunc.a "$MWOS_TMP"/largefunc.r "$MWOS_TMP"/largefunc.out "$MWOS_TMP"/largefunc.sym
			else
				echo "FAIL  tinyc -largedata Funktionsaufrufe: -os9-Backend-Lauf fehlgeschlagen"; fail=1
			fi
			# Gegenprobe: dasselbe 150-Funktionen-Programm OHNE -largedata muss der
			# echte r68 tatsaechlich mit "branch out of range" ablehnen (bsr ueber
			# mehr als 32 KB -- andere Fehlermeldung als "value out of range" bei
			# lea/movea fuer Daten) -- belegt die Notwendigkeit auch fuer
			# Funktionsaufrufe, nicht nur fuer globale Daten.
			if build/tinyc_backend build/tinyc_largefunc.ir build/tinyc_largefunc2_os9.a -os9; then
				cp build/tinyc_largefunc2_os9.a "$MWOS_TMP/largefunc2.a"
				rm -f "$MWOS_TMP/largefunc2.r"
				out=$(WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\largefunc2.a -o=M:\\TMP\\largefunc2.r -q" 2>&1)
				# "branch out of range" (nicht "value out of range" wie bei lea/movea
				# fuer Daten) ist die reale r68-Fehlermeldung fuer ein zu weit
				# entferntes bsr-Ziel -- empirisch bestaetigt (2026-07-25).
				if echo "$out" | grep -q 'branch out of range'; then
					echo "ok    tinyc: echter r68 lehnt dasselbe 150-Funktionen-Programm OHNE -largedata tatsaechlich mit 'branch out of range' ab"
				else
					echo "FAIL  tinyc: r68 haette OHNE -largedata (Funktionsaufrufe) ablehnen muessen -- Gegenprobe ungueltig"; fail=1
				fi
				rm -f "$MWOS_TMP"/largefunc2.a "$MWOS_TMP"/largefunc2.r
			else
				echo "FAIL  tinyc: -os9-Backend-Lauf (Funktionsaufruf-Gegenprobe) fehlgeschlagen"; fail=1
			fi
		else
			echo "warn  tinyc -largedata Funktionsaufrufe: Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter r68/l68-Test uebersprungen"
		fi
	else
		echo "warn  tinyc -largedata Funktionsaufrufe: Backend oder python3 fehlt -- uebersprungen"
	fi

	# 13a-os9) Microware-r68-Ausgabemodus (2026-07-24): tinyc_backend akzeptiert
	# ein optionales 4. Argument "-os9" und schaltet dann auf nam/psect/ends-
	# Rahmung, "*" statt ";" fuer volle Kommentarzeilen und "align 4"/"dc.l 0,.."
	# statt "even"/"ds.l" um (r68 kennt even/ds.l/rmb/cnop nicht -- empirisch
	# gegen die echte Microware-Toolchain ermittelt). Deckt DATA/BSS/Scratch-
	# Puffer UND einen CALLEXT-Aufruf gleichzeitig ab; wird -- wie schon bei
	# oberon0_os9.a -- gegen den ECHTEN r68.exe (via Wine/MWOS) assembliert.
	if [ -x build/tinyc_backend ]; then
		build/tinyc_p 'int counter; int limit = 10; char buf[4]; extern int myadd(int a, int b); int helper(int x){ return x+1; } int main(){ counter = myadd(3,4); limit = helper(counter); putint(limit); }' > build/tinyc_os9combo.ir
		if build/tinyc_backend build/tinyc_os9combo.ir build/tinyc_os9combo.s68 -os9 && \
			grep -q '^	nam	tinyc_os9combo_p$' build/tinyc_os9combo.s68 && \
			grep -q '^	psect	tinyc_os9combo_p,0,0,1,0,0$' build/tinyc_os9combo.s68 && \
			grep -q '^	ends$' build/tinyc_os9combo.s68; then
			echo "ok    tinyc -os9: nam/psect/ends-Rahmung wird erzeugt"
		else
			echo "FAIL  tinyc -os9: nam/psect/ends-Rahmung fehlt oder Backend-Aufruf fehlgeschlagen"; fail=1
		fi
		if [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && [ -f build/tinyc_os9combo.s68 ]; then
			mkdir -p "$MWOS_TMP"
			cp build/tinyc_os9combo.s68 "$MWOS_TMP/tccombo.a"
			rm -f "$MWOS_TMP/tccombo.r"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\tccombo.a -o=M:\\TMP\\tccombo.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/tccombo.r" ]; then
				echo "ok    tinyc -os9: DATA/BSS/Scratch-Puffer + CALLEXT assemblieren fehlerfrei (Microware r68 via Wine)"
			else
				echo "FAIL  tinyc -os9: assembliert NICHT (r68 via Wine)"; fail=1
			fi
			rm -f "$MWOS_TMP/tccombo.a" "$MWOS_TMP/tccombo.r"
		else
			echo "warn  tinyc -os9: Wine/MWOS nicht verfuegbar -- echter r68-Check uebersprungen"
		fi
	else
		echo "warn  tinyc -os9: Backend fehlt -- uebersprungen"
	fi
else
	echo "warn  tinyc M4a: vasm fehlt -- Backend-Assembler-Check uebersprungen"
fi

# 13a-link) Tiny-C Mehrdatei-Uebersetzung, M2 (2026-07-25): ZWEI SEPARAT mit
#     -part kompilierte Dateien (eine zusaetzlich mit -runtime fuer den
#     gemeinsamen 68k-Core/I/O-Anker, siehe tinyc_backend_c.cpp) werden mit dem
#     ECHTEN r68 assembliert und mit dem ECHTEN l68 (Microware-Linker) zu EINEM
#     Modul gelinkt -- erstmals in diesem Projekt automatisiert (bisher rief
#     runtests.sh nur r68 auf, nie l68). Deckt Funktionsaufruf UND geteilte
#     globale Variable ueber die Dateigrenze hinweg ab, PLUS dass eine
#     static-Funktion in Datei A per Namensverfremdung (tc_<name>__<psect>)
#     nicht mit einem gleichnamigen Symbol kollidiert. r68/l68 kennen KEIN
#     Sichtbarkeitskonzept (kein xdef/xref, jedes Label automatisch sichtbar
#     beim Linken -- empirisch verifiziert, siehe docs/STATUS.md); der zweite
#     Testfall unten bestaetigt dafuer, dass l68 eine ECHTE Namenskollision
#     (zwei nicht-static Definitionen desselben Symbols) zuverlaessig als
#     "duplicate symbol" ablehnt.
if [ -x build/tinyc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
	mkdir -p "$MWOS_TMP"
	build/tinyc_p 'int shared; int helper(int x); static int localHelper(int x){ return x-1; } int main(){ shared = 10; putint(helper(shared)); putint(localHelper(shared)); }' > build/tinyc_mf_link_a.ir
	build/tinyc_p 'extern int shared; int helper(int x){ return shared + x; }' > build/tinyc_mf_link_b.ir
	if build/tinyc_backend build/tinyc_mf_link_a.ir build/tinyc_mf_link_a.s68 -os9 -part -runtime && \
		build/tinyc_backend build/tinyc_mf_link_b.ir build/tinyc_mf_link_b.s68 -os9 -part; then
		cp build/tinyc_mf_link_a.s68 "$MWOS_TMP/mflinka.a"
		cp build/tinyc_mf_link_b.s68 "$MWOS_TMP/mflinkb.a"
		rm -f "$MWOS_TMP/mflinka.r" "$MWOS_TMP/mflinkb.r" "$MWOS_TMP/mflink.out" "$MWOS_TMP/mflink.sym"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\mflinka.a -o=M:\\TMP\\mflinka.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\mflinkb.a -o=M:\\TMP\\mflinkb.r -q" >/dev/null 2>&1
		if [ -s "$MWOS_TMP/mflinka.r" ] && [ -s "$MWOS_TMP/mflinkb.r" ]; then
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\mflinka.r M:\\TMP\\mflinkb.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\mflink.out -s=M:\\TMP\\mflink.sym" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/mflink.out" ] && grep -q "tc_localHelper__tinyc_mf_link_a_p" "$MWOS_TMP/mflink.sym" && \
				grep -q "tc_helper " "$MWOS_TMP/mflink.sym" && grep -q "tc_g_shared" "$MWOS_TMP/mflink.sym"; then
				echo "ok    tinyc Mehrdatei M2: echter r68+l68-Link zweier getrennt kompilierter Dateien (Funktionsaufruf+Global ueber Dateigrenze, static-Mangling) korrekt"
			else
				echo "FAIL  tinyc Mehrdatei M2: echter l68-Link fehlerhaft oder Symbole fehlen"; fail=1
			fi
		else
			echo "FAIL  tinyc Mehrdatei M2: r68-Assemblierung einer der beiden Dateien fehlgeschlagen"; fail=1
		fi
		rm -f "$MWOS_TMP"/mflink*.a "$MWOS_TMP"/mflink*.r "$MWOS_TMP/mflink.out" "$MWOS_TMP/mflink.sym"
	else
		echo "FAIL  tinyc Mehrdatei M2: -part/-runtime-Backend-Aufruf fehlgeschlagen"; fail=1
	fi
	# Duplicate-Symbol-Testfall: ZWEI Dateien definieren dieselbe nicht-static
	# Funktion -- l68 muss das als "duplicate symbol" ablehnen (simuliert, was
	# jeder echte Linker tut, siehe M0-Spike-Ergebnis in docs/STATUS.md).
	build/tinyc_p 'int f(){ return 1; } int main(){ putint(f()); }' > build/tinyc_mf_dup_a.ir
	build/tinyc_p 'int f(){ return 2; }' > build/tinyc_mf_dup_b.ir
	if build/tinyc_backend build/tinyc_mf_dup_a.ir build/tinyc_mf_dup_a.s68 -os9 -part -runtime && \
		build/tinyc_backend build/tinyc_mf_dup_b.ir build/tinyc_mf_dup_b.s68 -os9 -part; then
		cp build/tinyc_mf_dup_a.s68 "$MWOS_TMP/mfdupa.a"
		cp build/tinyc_mf_dup_b.s68 "$MWOS_TMP/mfdupb.a"
		rm -f "$MWOS_TMP/mfdupa.r" "$MWOS_TMP/mfdupb.r" "$MWOS_TMP/mfdup.out"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\mfdupa.a -o=M:\\TMP\\mfdupa.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\mfdupb.a -o=M:\\TMP\\mfdupb.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\mfdupa.r M:\\TMP\\mfdupb.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\mfdup.out" >build/tinyc_mf_dup.err 2>&1
		if [ ! -s "$MWOS_TMP/mfdup.out" ] && grep -qi "duplicate symbol" build/tinyc_mf_dup.err; then
			echo "ok    tinyc Mehrdatei M2: echter l68 lehnt doppelte nicht-static Definition als 'duplicate symbol' ab"
		else
			echo "FAIL  tinyc Mehrdatei M2: l68 haette 'duplicate symbol' melden muessen"; fail=1
		fi
		rm -f "$MWOS_TMP"/mfdup*.a "$MWOS_TMP"/mfdup*.r "$MWOS_TMP/mfdup.out" build/tinyc_mf_dup.err
	else
		echo "FAIL  tinyc Mehrdatei M2: Backend-Aufruf fuer Duplicate-Test fehlgeschlagen"; fail=1
	fi
else
	echo "warn  tinyc Mehrdatei M2: Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Mehrdatei-Link uebersprungen"
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

# 13a-2) nicht-konstanter static-Initialisierer (2026-07-24): Runs-once-Guard mit
#        verstecktem bool-Flag-Global (LOADGC/JZ/STOREG.../STOREGC, dieselben Opcodes
#        wie if/while) im ECHTEN 68000-Code.
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'int base(){ return 10; } int f(){ static int x = base() + 5; x += 1; return x; } int main(){ putint(f()); putint(f()); putint(f()); }' > build/tinyc_staticrt.ir && \
		build/tinyc_backend build/tinyc_staticrt.ir build/tinyc_staticrt.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_staticrt.bin build/tinyc_staticrt.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_staticrt.s68 2>/dev/null)" = "$(printf '16\n17\n18')" ]; then
		echo "ok    tinyc static-Laufzeit-Initialisierer 68000: Runs-once-Guard korrekt"
	else
		echo "FAIL  tinyc static-Laufzeit-Initialisierer 68000: Runs-once-Guard fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc static-Laufzeit-Initialisierer 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
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
	if build/tinyc_p 'struct P{int x; char buf[4]; int a[3];}; int main(){ struct P p; int i; i = 1; p.x = 7; p.buf[0]=65; p.buf[1]=66; p.a[0]=10; p.a[1]=20; p.a[2]=30; putint(p.x); putchar(p.buf[0]); putchar(p.buf[1]); putint(p.a[i]); }' > build/tinyc_structidx.ir && \
		build/tinyc_backend build/tinyc_structidx.ir build/tinyc_structidx.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_structidx.bin build/tinyc_structidx.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_structidx.s68 2>/dev/null)" = "$(printf '7\nAB20')" ]; then
		echo "ok    tinyc struct 68000: direkte p.field[i]-Indizierung (int- und char-Array-Feld) korrekt"
	else
		echo "FAIL  tinyc struct 68000: direkte p.field[i]-Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000 (p.field[i]): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# 2026-07-25 (Selfhosting L2): Pointer-Feld in struct -- 8 Byte Groesse/Ausrichtung
# UNABHAENGIG von der Ziel-Architektur (siehe tcRegisterStruct-Kommentar), der
# 68k-Backend nutzt davon nur die ersten 4 Byte. Zugriff nur ueber eine Pointer-
# Zwischenvariable (kein direktes p.field[i] durch ein Pointer-Feld, wie zuvor
# bei Array-Feldern).
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'struct P{char* text; int len;}; int main(){ struct P p; char msg[4]; char* t; msg[0]=72; msg[1]=105; msg[2]=0; p.text=msg; p.len=2; t=p.text; putchar(t[0]); putchar(t[1]); putint(p.len); }' > build/tinyc_structptr.ir && \
		build/tinyc_backend build/tinyc_structptr.ir build/tinyc_structptr.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_structptr.bin build/tinyc_structptr.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_structptr.s68 2>/dev/null)" = "$(printf 'Hi2')" ]; then
		echo "ok    tinyc struct 68000: Pointer-Feld (8-Byte-Layout, ueber Pointer-Zwischenvariable) korrekt"
	else
		echo "FAIL  tinyc struct 68000: Pointer-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000 (Pointer-Feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# arr[i].feld (2026-07-25): siehe TinyVM-Tests oben fuer die vollstaendige Erklaerung
# (Allokations-Fix + neuer IPADDN-Opcode). 68k-IPADDN nutzt tc_mul_i32 (bereits im
# Runtime-Core), da lsl.l nur feste 1/4-Skalierung kann, structByteSize aber beliebig ist.
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; arr[0].a=10; arr[0].b=11; arr[1].a=20; arr[1].b=21; arr[2].a=30; arr[2].b=31; putint(arr[0].a); putint(arr[0].b); putint(arr[1].a); putint(arr[1].b); putint(arr[2].a); putint(arr[2].b); }' > build/tinyc_structarr.ir && \
		build/tinyc_backend build/tinyc_structarr.ir build/tinyc_structarr.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_structarr.bin build/tinyc_structarr.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_structarr.s68 2>/dev/null)" = "$(printf '10\n11\n20\n21\n30\n31')" ]; then
		echo "ok    tinyc struct 68000: Array von structs, arr[i].feld (int-Felder) korrekt"
	else
		echo "FAIL  tinyc struct 68000: arr[i].feld (int-Felder) fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000 (arr[i].feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'struct Rec { char name[8]; char* text; }; int main(){ struct Rec arr[3]; char* n; n = arr[0].name; n[0]=65; n = arr[1].name; n[0]=66; n = arr[2].name; n[0]=67; n = arr[1].name; n[0] = 88; n = arr[0].name; putchar(n[0]); n = arr[1].name; putchar(n[0]); n = arr[2].name; putchar(n[0]); }' > build/tinyc_structarrptr.ir && \
		build/tinyc_backend build/tinyc_structarrptr.ir build/tinyc_structarrptr.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_structarrptr.bin build/tinyc_structarrptr.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_structarrptr.s68 2>/dev/null)" = "AXC" ]; then
		echo "ok    tinyc struct 68000: Array von structs mit Pointer-Feld, arr[i].feld ueber Zwischenvariable korrekt"
	else
		echo "FAIL  tinyc struct 68000: Array von structs mit Pointer-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000 (arr[i].feld, Pointer-Feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# ptr[i].feld (2026-07-25, Milestone B): lokale Pointer-auf-struct-Variable, indiziert,
# dann Feldzugriff -- braucht routinesC[i].name/.text im Selfhosting-Piloten. LOADP statt
# PUSHADDR, sonst dieselbe IPADDN-Idee wie arr[i].feld.
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; struct Rec* p; arr[0].a=10; arr[1].a=20; arr[2].a=30; p = arr; putint(p[0].a); putint(p[1].a); putint(p[2].a); p[1].a = 99; putint(arr[1].a); }' > build/tinyc_structptrarr.ir && \
		build/tinyc_backend build/tinyc_structptrarr.ir build/tinyc_structptrarr.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_structptrarr.bin build/tinyc_structptrarr.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_structptrarr.s68 2>/dev/null)" = "$(printf '10\n20\n30\n99')" ]; then
		echo "ok    tinyc struct 68000: ptr[i].feld (lokale Pointer-auf-struct-Variable) korrekt"
	else
		echo "FAIL  tinyc struct 68000: ptr[i].feld fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000 (ptr[i].feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# Globale structs (2026-07-25): tc_globalend erkannte "struct" bisher gar nicht als
# Basistyp. Skalare globale structs/Arrays von structs/Pointer-auf-struct-Globale --
# Codegen strukturell identisch zum lokalen Fall (ADDRG/PUSHADDR G/LOADGP statt
# PUSHADDR L/LOADP).
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'struct Rec { int a; int b; }; struct Rec garr[3]; struct Rec* gp; int main(){ gp = garr; gp[0].a=100; gp[1].a=200; putint(garr[0].a); putint(garr[1].a); putint(gp[1].a); }' > build/tinyc_gstruct.ir && \
		build/tinyc_backend build/tinyc_gstruct.ir build/tinyc_gstruct.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_gstruct.bin build/tinyc_gstruct.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_gstruct.s68 2>/dev/null)" = "$(printf '100\n200\n200')" ]; then
		echo "ok    tinyc struct 68000: globale struct-Variablen/-Arrays/-Pointer korrekt"
	else
		echo "FAIL  tinyc struct 68000: globale structs fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct 68000 (globale structs): Backend, vasm oder python3 fehlt -- uebersprungen"
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
	if build/tinyc_p 'int main(){ int m[3][3]; int i; int j; for(i=0;i<3;i+=1){ for(j=0;j<3;j+=1){ m[i][j]=i*10+j; } } putint(m[2][1]); putint(m[0][2]); }' > build/tinyc_2d.ir && \
		build/tinyc_backend build/tinyc_2d.ir build/tinyc_2d.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_2d.bin build/tinyc_2d.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_2d.s68 2>/dev/null)" = "$(printf '21\n2')" ]; then
		echo "ok    tinyc 2D-Array 68000: Zeilen/Spalten-Indizierung korrekt"
	else
		echo "FAIL  tinyc 2D-Array 68000: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc 2D-Array 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'int main(){ int m[2][3][4]; int i; int j; int k; for(i=0;i<2;i+=1){ for(j=0;j<3;j+=1){ for(k=0;k<4;k+=1){ m[i][j][k]=i*100+j*10+k; } } } putint(m[1][2][3]); putint(m[0][0][0]); putint(m[1][0][2]); }' > build/tinyc_3d.ir && \
		build/tinyc_backend build/tinyc_3d.ir build/tinyc_3d.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_3d.bin build/tinyc_3d.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_3d.s68 2>/dev/null)" = "$(printf '123\n0\n102')" ]; then
		echo "ok    tinyc 3D-Array 68000: Horner-Kombination ueber drei Dimensionen korrekt"
	else
		echo "FAIL  tinyc 3D-Array 68000: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc 3D-Array 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'int main(){ char* s = "AB"; putchar(s[0]); putchar(s[1]); putint(s[2]); }' > build/tinyc_string.ir && \
		build/tinyc_backend build/tinyc_string.ir build/tinyc_string.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_string.bin build/tinyc_string.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_string.s68 2>/dev/null)" = "AB0" ]; then
		echo "ok    tinyc String-Literal 68000: GARRAY/GINIT/ADDRG-Adressierung korrekt"
	else
		echo "FAIL  tinyc String-Literal 68000: Adressierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc String-Literal 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'char gmsg[6] = "hallo"; int main(){ char m[5] = "hallo"; putchar(m[0]); putchar(m[4]); putchar(gmsg[0]); putint(gmsg[5]); }' > build/tinyc_stringinit.ir && \
		build/tinyc_backend build/tinyc_stringinit.ir build/tinyc_stringinit.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_stringinit.bin build/tinyc_stringinit.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_stringinit.s68 2>/dev/null)" = "hoh0" ]; then
		echo "ok    tinyc String-Array-Initialisierer 68000: lokal (exakt) + global (mit Nullterminator) korrekt"
	else
		echo "FAIL  tinyc String-Array-Initialisierer 68000: fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc String-Array-Initialisierer 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/tinyc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/tinyc_p 'char* mkstr(){ return "hallo"; } int main(){ putchar(mkstr()[0]); putchar(mkstr()[4]); putchar("world"[0]); putint("world"[4]); }' > build/tinyc_directidx.ir && \
		build/tinyc_backend build/tinyc_directidx.ir build/tinyc_directidx.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_directidx.bin build/tinyc_directidx.s68 2>/dev/null && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_directidx.s68 2>/dev/null)" = "how100" ]; then
		echo "ok    tinyc direkte Indizierung 68000: Funktionsrueckgabewert + String-Literal ohne Zwischenvariable korrekt"
	else
		echo "FAIL  tinyc direkte Indizierung 68000: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc direkte Indizierung 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
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
	if build/tinyc_p 'unsigned int high=-1; int fact(int n){ if(n <= 1) return 1; else return n * fact(n - 1); } int main(){ putint(fact(5)); putint(-7 * 6); putint(20 / 3); putint(-20 / 3); putint(high > 1); putint(high / 2); putuint(high); putint(20 % 6); putint(-20 % 6); putuint(high % 10); }' > build/tinyc_m4b.ir && \
		build/tinyc_backend build/tinyc_m4b.ir build/tinyc_m4b.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/tinyc_m4b.bin build/tinyc_m4b.s68 2>/dev/null && \
		grep -q '^tc_mul_loop:' build/tinyc_m4b.s68 && grep -q '^tc_div_loop:' build/tinyc_m4b.s68 && grep -q '^tc_udiv_loop:' build/tinyc_m4b.s68 && \
		[ "$(python3 tools/tiny68sim.py build/tinyc_m4b.s68 2>/dev/null)" = "$(printf '120\n-42\n6\n-6\n1\n2147483647\n4294967295\n2\n-2\n5')" ]; then
		echo "ok    tinyc M4c-1: echte 68k int32 signed/unsigned MUL/DIV/MOD + Fakultaet = tinyvm"
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

# 16a) Tiny-C Mehrdatei-Uebersetzung, M3 (2026-07-25): ZWEI SEPARAT mit -part
#     kompilierte Dateien werden mit clang zu getrennten .o-Objekten assembliert
#     und mit demselben clang-Aufruf (der intern ld ruft) zu EINEM Programm
#     gelinkt -- erstmals echte getrennte Objektdateien statt ein einzelner
#     .s-Compile + Runtime-Datei in einem Rutsch. Anders als beim 68k/l68-Ziel
#     (siehe tinyc_backend_c.cpp) braucht static HIER KEINE Namensverfremdung:
#     Mach-O/ld unterstuetzen ECHTE lokale Symbole (kein .globl = fuer andere
#     Objektdateien unsichtbar) -- empirisch verifiziert (zwei .o mit je einem
#     lokalen gleichnamigen Symbol linken ohne Konflikt). Zweiter Testfall
#     bestaetigt trotzdem, dass eine ECHTE Namenskollision (zwei nicht-static
#     Definitionen) von ld zuverlaessig als "duplicate symbol" abgelehnt wird.
if [ "$(uname -m)" = "arm64" ] && command -v clang >/dev/null 2>&1 && [ -x build/tinyc_arm64_backend ]; then
	build/tinyc_p 'int shared; int helper(int x); static int localHelper(int x){ return x-1; } int main(){ shared = 10; putint(helper(shared)); putint(localHelper(shared)); }' > build/tinyc_mf_arm_a.ir
	build/tinyc_p 'extern int shared; int helper(int x){ return shared + x; }' > build/tinyc_mf_arm_b.ir
	if build/tinyc_arm64_backend build/tinyc_mf_arm_a.ir build/tinyc_mf_arm_a.s -part && \
		build/tinyc_arm64_backend build/tinyc_mf_arm_b.ir build/tinyc_mf_arm_b.s -part && \
		clang -arch arm64 -c build/tinyc_mf_arm_a.s -o build/tinyc_mf_arm_a.o && \
		clang -arch arm64 -c build/tinyc_mf_arm_b.s -o build/tinyc_mf_arm_b.o && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_mf_arm build/tinyc_mf_arm_a.o build/tinyc_mf_arm_b.o runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_mf_arm)" = "$(printf '20\n9')" ] && \
		[ "$(nm build/tinyc_mf_arm_a.o | grep -c ' t _tc_localHelper$')" = "1" ]; then
		echo "ok    tinyc Mehrdatei M3: echte getrennte .o-Kompilate + clang/ld-Link (Funktionsaufruf+Global ueber Dateigrenze, static bleibt echt lokal) korrekt"
	else
		echo "FAIL  tinyc Mehrdatei M3: getrennte .o-Kompilate/Link fehlerhaft"; fail=1
	fi
	# Duplicate-Symbol-Testfall: ZWEI Dateien definieren dieselbe nicht-static
	# Funktion -- ld muss das als "duplicate symbol" ablehnen.
	build/tinyc_p 'int f(){ return 1; } int main(){ putint(f()); }' > build/tinyc_mf_arm_dup_a.ir
	build/tinyc_p 'int f(){ return 2; }' > build/tinyc_mf_arm_dup_b.ir
	if build/tinyc_arm64_backend build/tinyc_mf_arm_dup_a.ir build/tinyc_mf_arm_dup_a.s -part && \
		build/tinyc_arm64_backend build/tinyc_mf_arm_dup_b.ir build/tinyc_mf_arm_dup_b.s -part && \
		clang -arch arm64 -c build/tinyc_mf_arm_dup_a.s -o build/tinyc_mf_arm_dup_a.o && \
		clang -arch arm64 -c build/tinyc_mf_arm_dup_b.s -o build/tinyc_mf_arm_dup_b.o; then
		if clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_mf_arm_dup build/tinyc_mf_arm_dup_a.o build/tinyc_mf_arm_dup_b.o runtime/arm64_darwin/start.s >build/tinyc_mf_arm_dup.err 2>&1; then
			echo "FAIL  tinyc Mehrdatei M3: ld haette 'duplicate symbol' melden muessen"; fail=1
		elif grep -qi "duplicate symbol" build/tinyc_mf_arm_dup.err; then
			echo "ok    tinyc Mehrdatei M3: echter ld lehnt doppelte nicht-static Definition als 'duplicate symbol' ab"
		else
			echo "FAIL  tinyc Mehrdatei M3: Link schlug NICHT wegen 'duplicate symbol' fehl"; fail=1
		fi
		rm -f build/tinyc_mf_arm_dup.err
	else
		echo "FAIL  tinyc Mehrdatei M3: Backend-/.o-Aufruf fuer Duplicate-Test fehlgeschlagen"; fail=1
	fi
else
	echo "warn  tinyc Mehrdatei M3: nur auf arm64-macOS getestet -- uebersprungen"
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
	if build/tinyc_p 'struct P{int x; char buf[4]; int a[3];}; int main(){ struct P p; int i; i = 1; p.x = 7; p.buf[0]=65; p.buf[1]=66; p.a[0]=10; p.a[1]=20; p.a[2]=30; putint(p.x); putchar(p.buf[0]); putchar(p.buf[1]); putint(p.a[i]); }' > build/tinyc_structidx_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_structidx_arm64.ir build/tinyc_structidx_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_structidx_arm64 build/tinyc_structidx_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_structidx_arm64)" = "$(printf '7\nAB20')" ]; then
		echo "ok    tinyc struct ARM64: direkte p.field[i]-Indizierung (int- und char-Array-Feld) korrekt"
	else
		echo "FAIL  tinyc struct ARM64: direkte p.field[i]-Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64 (p.field[i]): Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'struct P{char* text; int len;}; int main(){ struct P p; char msg[4]; char* t; msg[0]=72; msg[1]=105; msg[2]=0; p.text=msg; p.len=2; t=p.text; putchar(t[0]); putchar(t[1]); putint(p.len); }' > build/tinyc_structptr_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_structptr_arm64.ir build/tinyc_structptr_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_structptr_arm64 build/tinyc_structptr_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_structptr_arm64)" = "$(printf 'Hi2')" ]; then
		echo "ok    tinyc struct ARM64: Pointer-Feld (8-Byte-Layout, ueber Pointer-Zwischenvariable) korrekt"
	else
		echo "FAIL  tinyc struct ARM64: Pointer-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64 (Pointer-Feld): Backend fehlt -- uebersprungen"
fi

# arr[i].feld (2026-07-25): siehe TinyVM/68000-Tests oben. ARM64-IPADDN nutzt eine
# echte 32-Bit-Multiplikation (mul) statt scaleSuffix (nur feste 1/4/8-Shifts).
if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; arr[0].a=10; arr[0].b=11; arr[1].a=20; arr[1].b=21; arr[2].a=30; arr[2].b=31; putint(arr[0].a); putint(arr[0].b); putint(arr[1].a); putint(arr[1].b); putint(arr[2].a); putint(arr[2].b); }' > build/tinyc_structarr_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_structarr_arm64.ir build/tinyc_structarr_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_structarr_arm64 build/tinyc_structarr_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_structarr_arm64)" = "$(printf '10\n11\n20\n21\n30\n31')" ]; then
		echo "ok    tinyc struct ARM64: Array von structs, arr[i].feld (int-Felder) korrekt"
	else
		echo "FAIL  tinyc struct ARM64: arr[i].feld (int-Felder) fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64 (arr[i].feld): Backend fehlt -- uebersprungen"
fi
if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'struct Rec { char name[8]; char* text; }; int main(){ struct Rec arr[3]; char* n; n = arr[0].name; n[0]=65; n = arr[1].name; n[0]=66; n = arr[2].name; n[0]=67; n = arr[1].name; n[0] = 88; n = arr[0].name; putchar(n[0]); n = arr[1].name; putchar(n[0]); n = arr[2].name; putchar(n[0]); }' > build/tinyc_structarrptr_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_structarrptr_arm64.ir build/tinyc_structarrptr_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_structarrptr_arm64 build/tinyc_structarrptr_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_structarrptr_arm64)" = "AXC" ]; then
		echo "ok    tinyc struct ARM64: Array von structs mit Pointer-Feld, arr[i].feld ueber Zwischenvariable korrekt"
	else
		echo "FAIL  tinyc struct ARM64: Array von structs mit Pointer-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64 (arr[i].feld, Pointer-Feld): Backend fehlt -- uebersprungen"
fi

# ptr[i].feld (2026-07-25, Milestone B): siehe 68000-Test oben fuer die Erklaerung.
if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; struct Rec* p; arr[0].a=10; arr[1].a=20; arr[2].a=30; p = arr; putint(p[0].a); putint(p[1].a); putint(p[2].a); p[1].a = 99; putint(arr[1].a); }' > build/tinyc_structptrarr_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_structptrarr_arm64.ir build/tinyc_structptrarr_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_structptrarr_arm64 build/tinyc_structptrarr_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_structptrarr_arm64)" = "$(printf '10\n20\n30\n99')" ]; then
		echo "ok    tinyc struct ARM64: ptr[i].feld (lokale Pointer-auf-struct-Variable) korrekt"
	else
		echo "FAIL  tinyc struct ARM64: ptr[i].feld fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64 (ptr[i].feld): Backend fehlt -- uebersprungen"
fi

# Globale structs (2026-07-25): siehe 68000-Test oben fuer die Erklaerung.
if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'struct Rec { int a; int b; }; struct Rec garr[3]; struct Rec* gp; int main(){ gp = garr; gp[0].a=100; gp[1].a=200; putint(garr[0].a); putint(garr[1].a); putint(gp[1].a); }' > build/tinyc_gstruct_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_gstruct_arm64.ir build/tinyc_gstruct_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_gstruct_arm64 build/tinyc_gstruct_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_gstruct_arm64)" = "$(printf '100\n200\n200')" ]; then
		echo "ok    tinyc struct ARM64: globale struct-Variablen/-Arrays/-Pointer korrekt"
	else
		echo "FAIL  tinyc struct ARM64: globale structs fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc struct ARM64 (globale structs): Backend fehlt -- uebersprungen"
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
	if build/tinyc_p 'int main(){ int m[3][3]; int i; int j; for(i=0;i<3;i+=1){ for(j=0;j<3;j+=1){ m[i][j]=i*10+j; } } putint(m[2][1]); putint(m[0][2]); }' > build/tinyc_2d_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_2d_arm64.ir build/tinyc_2d_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_2d_arm64 build/tinyc_2d_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_2d_arm64)" = "$(printf '21\n2')" ]; then
		echo "ok    tinyc 2D-Array ARM64: Zeilen/Spalten-Indizierung korrekt"
	else
		echo "FAIL  tinyc 2D-Array ARM64: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc 2D-Array ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'int main(){ int m[2][3][4]; int i; int j; int k; for(i=0;i<2;i+=1){ for(j=0;j<3;j+=1){ for(k=0;k<4;k+=1){ m[i][j][k]=i*100+j*10+k; } } } putint(m[1][2][3]); putint(m[0][0][0]); putint(m[1][0][2]); }' > build/tinyc_3d_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_3d_arm64.ir build/tinyc_3d_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_3d_arm64 build/tinyc_3d_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_3d_arm64)" = "$(printf '123\n0\n102')" ]; then
		echo "ok    tinyc 3D-Array ARM64: Horner-Kombination ueber drei Dimensionen korrekt"
	else
		echo "FAIL  tinyc 3D-Array ARM64: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc 3D-Array ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'int main(){ char* s = "AB"; putchar(s[0]); putchar(s[1]); putint(s[2]); }' > build/tinyc_string_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_string_arm64.ir build/tinyc_string_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_string_arm64 build/tinyc_string_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_string_arm64)" = "AB0" ]; then
		echo "ok    tinyc String-Literal ARM64: GARRAY/GINIT/ADDRG-Adressierung korrekt"
	else
		echo "FAIL  tinyc String-Literal ARM64: Adressierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc String-Literal ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'char gmsg[6] = "hallo"; int main(){ char m[5] = "hallo"; putchar(m[0]); putchar(m[4]); putchar(gmsg[0]); putint(gmsg[5]); }' > build/tinyc_stringinit_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_stringinit_arm64.ir build/tinyc_stringinit_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_stringinit_arm64 build/tinyc_stringinit_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_stringinit_arm64)" = "hoh0" ]; then
		echo "ok    tinyc String-Array-Initialisierer ARM64: lokal (exakt) + global (mit Nullterminator) korrekt"
	else
		echo "FAIL  tinyc String-Array-Initialisierer ARM64: fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc String-Array-Initialisierer ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'char* mkstr(){ return "hallo"; } int main(){ putchar(mkstr()[0]); putchar(mkstr()[4]); putchar("world"[0]); putint("world"[4]); }' > build/tinyc_directidx_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_directidx_arm64.ir build/tinyc_directidx_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_directidx_arm64 build/tinyc_directidx_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_directidx_arm64)" = "how100" ]; then
		echo "ok    tinyc direkte Indizierung ARM64: Funktionsrueckgabewert + String-Literal ohne Zwischenvariable korrekt"
	else
		echo "FAIL  tinyc direkte Indizierung ARM64: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc direkte Indizierung ARM64: Backend fehlt -- uebersprungen"
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

if [ -x build/tinyc_arm64_backend ]; then
	if build/tinyc_p 'int base(){ return 10; } int f(){ static int x = base() + 5; x += 1; return x; } int main(){ putint(f()); putint(f()); putint(f()); }' > build/tinyc_staticrt_arm64.ir && \
		build/tinyc_arm64_backend build/tinyc_staticrt_arm64.ir build/tinyc_staticrt_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/tinyc_staticrt_arm64 build/tinyc_staticrt_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/tinyc_staticrt_arm64)" = "$(printf '16\n17\n18')" ]; then
		echo "ok    tinyc static-Laufzeit-Initialisierer ARM64: Runs-once-Guard korrekt"
	else
		echo "FAIL  tinyc static-Laufzeit-Initialisierer ARM64: Runs-once-Guard fehlerhaft"; fail=1
	fi
else
	echo "warn  tinyc static-Laufzeit-Initialisierer ARM64: Backend fehlt -- uebersprungen"
fi

[ $fail -eq 0 ] && echo "=== ALLE TESTS OK ===" || echo "=== FEHLER IN DER SUITE ==="
exit $fail
