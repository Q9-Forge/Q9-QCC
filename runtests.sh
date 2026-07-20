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

[ $fail -eq 0 ] && echo "=== ALLE TESTS OK ===" || echo "=== FEHLER IN DER SUITE ==="
exit $fail
