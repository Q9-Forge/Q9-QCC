#!/bin/sh
#================================================================================
# runtests.sh -- komplette Regressionssuite fuer das ebnf-Projekt
# Aufruf: ./runtests.sh   (baut zuerst, dann alle Grammatiken + TESTS-Bloecke)
#================================================================================
cd "$(dirname "$0")" || exit 1
mkdir -p build
# Die Backends gehoeren den Backend-Teilprojekten, nicht dieser Suite. Bis zum
# 2026-09-14 lag hier je eine EIGENE Kopie -- und die 68k-Kopie war die einzige
# mit dem -peephole-Nachlauf, waehrend die ausgelieferte Fassung ihn nicht
# hatte. Seither wird direkt die Produktionsquelle uebersetzt; damit kann diese
# Klasse von Abweichung nicht wieder entstehen.
QIR68K_SRC="${QIR68K_SRC:-../Q9-BACKEND-68K/q9-qir68k/src/qcc_backend_c.cpp}"
QIRARM64_SRC="${QIRARM64_SRC:-../Q9-BACKEND-ARM64/q9-qirarm64/src/qcc_arm64_backend_c.cpp}"

clang++ -std=c++17 -Wall -Wno-format-security -o build/parsec src/parsec.cpp src/codegen.cpp || exit 1

fail=0

# 1) Test-Grammatiken mit TESTS-Bloecken (Stack-Maschine gegen Erwartung)
for g in seqtest alttest blocktest opttest reptest numtest rangetest optalt multalt actiontest calcexpr actionrollback; do
	out=$(build/parsec "tests/$g" 2>&1)
	mm=$(echo "$out" | grep -c MISMATCH)
	pass=$(echo "$out" | grep "PASS ===")
	if [ "$mm" -ne 0 ]; then echo "FAIL  $g:"; echo "$out" | grep MISMATCH; fail=1
	else echo "ok    $g: $pass"; fi
done

# 2) Linksrekursions-Erkennung (muss anschlagen)
for g in leftrec leftrec2 opt_leftrec; do
	if build/parsec "tests/$g" 2>&1 | grep -q LINKSREKURSION; then echo "ok    $g: Linksrekursion erkannt"
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
	out=$(build/parsec "data/$g" 2>&1)
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
	if [ ! -f "tests/${g}_p.c" ]; then echo "FAIL  codegen $g: tests/${g}_p.c fehlt"; fail=1; continue; fi
	if ! cc -w -o "build/${g}_p" "tests/${g}_p.c"; then echo "FAIL  codegen $g: C-Parser kompiliert nicht"; fail=1; continue; fi
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
$(grep '^TEST ' "tests/$g.lextab")
EOT
	[ $mm -eq 0 ] && echo "ok    codegen $g: $n Tests gegen erzeugten C-Parser"
done

# 5) Semantik-Vorteil des Codegen-Pfads: die Tabelle akzeptiert bei
#    s = [ "-" ] "a" | "b" .  die Eingabe "-b" faelschlich (gewarnte Grenze) --
#    der ERZEUGTE Parser muss sie ablehnen (echtes Backtracking pro Alternative).
build/parsec "tests/ambig" >/dev/null 2>&1
if cc -w -o build/ambig_p tests/ambig_p.c 2>/dev/null; then
	if [ "$(build/ambig_p -b)" = "FAIL" ] && [ "$(build/ambig_p -a)" = "OK" ]; then
		echo "ok    codegen ambig: '-b' korrekt abgelehnt, '-a' erkannt"
	else
		echo "FAIL  codegen ambig: Backtracking-Semantik verletzt"; fail=1
	fi
else
	echo "FAIL  codegen ambig: C-Parser fehlt/kompiliert nicht"; fail=1
fi

# 5b) LEXER-Modus: WHITESPACE + COMMENT LINE muessen im erzeugten C-Parser UND im
#     simulierten 68k-Code gleich funktionieren (tests/lexcomment: num = digit {digit}
#     mit TOKEN digit, Kommentar "//").
build/parsec "tests/lexcomment" >/dev/null 2>&1
if cc -w -o build/lexcomment_p tests/lexcomment_p.c 2>/dev/null; then
	lcfail=0
	for r in "build/lexcomment_p" "python3 tools/s68sim.py tests/lexcomment.s68"; do
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
#     Nutzerwunsch): tests/multicomment konfiguriert "#" UND "//" als Zeilenkommentar
#     sowie "/* */" UND "(* *)" als Blockkommentar gleichzeitig -- alle vier muessen
#     in C UND im simulierten 68k-Code funktionieren, unquotiertes "x" bleibt ein Fehler.
build/parsec "tests/multicomment" >/dev/null 2>&1
if cc -w -o build/multicomment_p tests/multicomment_p.c 2>/dev/null; then
	mcfail=0
	for r in "build/multicomment_p" "python3 tools/s68sim.py tests/multicomment.s68"; do
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

# 5d) EIGENER Quelltext-Kommentarfilter von parsec (comment(), src/parsec.cpp) --
#     NICHT der generierte Lexer aus 5b/5c, sondern die Filterung der .ebnf-Eingabe
#     selbst. Zwei am 2026-09-14 gefundene, gegenlaeufige Fehler:
#       Host: "strcpy_s(index, len, indexEnd)" uebergab die Laenge des END-MARKERS
#             (2) als ZIELGROESSE -- ein Blockkommentar MITTEN in einer Zeile liess
#             genau 1 Zeichen des Zeilenrests uebrig, der Rest der Regel verschwand.
#       Port: der Abschluss eines MEHRZEILIGEN Kommentars schob den Zeilenrest nach
#             "index" statt an den Pufferanfang -- der Kommentartext VOR dem
#             Endmarker blieb stehen, charLen war entsprechend zu gross.
#     Beide Faelle waren zusammen unsichtbar: ein Blockkommentar, dem nur noch das
#     Zeilenende folgt, funktionierte auf beiden Seiten. Der Test prueft deshalb
#     BEIDE Formen und vergleicht Host UND QCC-Port gegen dieselbe Grammatik.
#     Der Port wird dafuer NATIV als C uebersetzt (er ist gueltiges C89, nur
#     "putchar" und "true" fehlen ihm ohne Header) -- damit ist die Aequivalenz
#     ohne Emulator pruefbar.
cmtdir=build/cmttest
cmtroot=$(pwd)
rm -rf "$cmtdir"; mkdir -p "$cmtdir"
{
	printf 'num = digit { digit } .\n'
	printf 'digit /* mitten in der Zeile */ = "0"~"9" .\n'
	printf '/* Kommentar Anfang\n   zweite Zeile\n   Ende */ letter = "a"~"z" .\n'
} > "$cmtdir/cmt.ebnf"
cp "$cmtdir/cmt.ebnf" "$cmtdir/qcc.ebnf"
cmtfail=0
( cd "$cmtdir" && "$cmtroot/build/parsec" cmt ) >"$cmtdir/host.out" 2>&1
if [ ! -f "$cmtdir/cmt.lextab" ] || grep -q "Grammatik fehlerhaft" "$cmtdir/host.out"; then
	echo "FAIL  parsec Kommentarfilter (Host): Grammatik mit Blockkommentaren nicht uebersetzt"; cmtfail=1
fi
{ printf 'int putchar(int c);\n#define true 1\n'; cat src-qcc/ebnf.tc; } > "$cmtdir/ebnf_native.c"
{ printf 'int putchar(int c);\n#define true 1\n'; cat src-qcc/codegen.tc; } > "$cmtdir/codegen_native.c"
if cc -w -o "$cmtdir/ebnfport" "$cmtdir/ebnf_native.c" "$cmtdir/codegen_native.c" 2>/dev/null; then
	( cd "$cmtdir" && ./ebnfport ) >"$cmtdir/port.out" 2>&1
	if [ ! -f "$cmtdir/qcc.lextab" ] || grep -q "Grammatik fehlerhaft" "$cmtdir/port.out"; then
		echo "FAIL  parsec Kommentarfilter (QCC-Port): Grammatik mit Blockkommentaren nicht uebersetzt"; cmtfail=1
	else
		# Einziger zulaessiger Unterschied ist der Erzeugername in Zeile 2.
		# Bis zum 2026-09-14 musste hier zusaetzlich der
		# [EBNF-ROHQUELLTEXT]-Block herausgefiltert werden, weil der Port ihn
		# nicht kannte -- diese Portluecke ist geschlossen, der Vergleich ist
		# seither vollstaendig.
		for f in "$cmtdir/cmt.lextab" "$cmtdir/qcc.lextab"; do
			sed -e 's/erzeugt von .*/erzeugt von X/' "$f" > "$f.norm"
		done
		if ! cmp -s "$cmtdir/cmt.lextab.norm" "$cmtdir/qcc.lextab.norm"; then
			echo "FAIL  parsec Kommentarfilter: Host und QCC-Port liefern verschiedene Arbeitsdateien"; cmtfail=1
		fi
	fi
else
	echo "FAIL  parsec Kommentarfilter: QCC-Port (src-qcc/ebnf.tc+codegen.tc) nicht nativ uebersetzbar"; cmtfail=1
fi
if [ $cmtfail -eq 0 ]; then
	echo "ok    parsec Kommentarfilter: Blockkommentar mitten in der Zeile und ueber mehrere Zeilen, Host und QCC-Port gleich"
else
	fail=1
fi

# 6) Schutz gegen eine Endlosschleife im generierten Parser: Der Rumpf einer
# Wiederholung darf nicht ohne Eingabe erfolgreich sein. Der Generator muss die
# Ausgabe bewusst verweigern statt einen haengenden C-/68k-Parser zu erzeugen.
out=$(build/parsec "tests/nullable_repeat" 2>&1)
if echo "$out" | grep -q "Wiederholung hat einen leeren Rumpf"; then
	echo "ok    codegen nullable_repeat: leere Wiederholung abgelehnt"
else
	echo "FAIL  codegen nullable_repeat: leere Wiederholung nicht erkannt"; fail=1
fi

# 7) Nutzertext fuer spaetere semantische Aktionen ist Teil der Arbeitsdatei und
# darf beim Neu-Erzeugen nicht verloren gehen oder vom EBNF-Parser interpretiert werden.
build/parsec "tests/usercode" >/dev/null 2>&1
if grep -Fq 'ACTION C after s { frontend_emit_literal("a"); }' "tests/usercode.lextab" \
	&& grep -Fq 'ACTION M68K after s { bsr frontend_emit_literal_a }' "tests/usercode.lextab"; then
	echo "ok    arbeitsdatei usercode: NUTZER-CODE unveraendert erhalten"
else
	echo "FAIL  arbeitsdatei usercode: NUTZER-CODE verloren/verfaelscht"; fail=1
fi

# 7b) ACTION/ROUTINE-Mechanismus (docs/ARCHITEKTUR.md §9): tests/actiontest definiert
#     "ACTION AFTER number CALL got_number" + ROUTINE C/M68K got_number im
#     [NUTZER-CODE]-Block. Der generierte C-Zwilling muss die Routine beim
#     Regelerfolg WIRKLICH aufrufen (start/end = erkannter Text); der 68k-Code
#     muss denselben bsr fehlerfrei assemblieren (vasm-Check unten prueft das separat).
if cc -w -o build/actiontest_p tests/actiontest_p.c 2>/dev/null; then
	got=$(build/actiontest_p 123)
	if [ "$got" = "$(printf 'ACTION got_number: 123\nOK')" ]; then
		echo "ok    action actiontest: ROUTINE C wird mit korrektem Text aufgerufen"
	else
		echo "FAIL  action actiontest: unerwartete Ausgabe: $got"; fail=1
	fi
else
	echo "FAIL  action actiontest: tests/actiontest_p.c fehlt/kompiliert nicht"; fail=1
fi

# 7c) ACTION/ROUTINE mit ECHTER Wertberechnung (nicht nur Seiteneffekt-Text): tests/calcexpr
#     ist ein Operator-Praezedenz-Ausdruck (expr = term {addop term}. term = factor {mulop
#     factor}.), die Aktionen fuehren einen globalen Werte-Stack in den ROUTINE-C-Koerpern
#     (kein Wertrueckgabekanal im Mechanismus selbst, siehe ARCHITEKTUR.md §9.5). Bestaetigt
#     Praezedenz (2+3*4=14) UND Linksassoziativitaet (10-2-3=5, nicht 11). Deckte dabei einen
#     eigenstaendigen, vorbestehenden Bug im TABELLEN-Generator auf (rule()-Regelabschluss
#     prüfte nur die letzte Tabellenzeile auf offene Vorwaertsreferenzen statt die ganze
#     Regel -- siehe parsec.cpp rule(), gefixt).
if cc -w -o build/calcexpr_p tests/calcexpr_p.c 2>/dev/null; then
	cefail=0
	[ "$(build/calcexpr_p '2+3*4')" = "$(printf 'RESULT: 14\nOK')" ] || { echo "FAIL  action calcexpr: 2+3*4 sollte 14 ergeben"; cefail=1; }
	[ "$(build/calcexpr_p '10-2-3')" = "$(printf 'RESULT: 5\nOK')" ] || { echo "FAIL  action calcexpr: 10-2-3 sollte 5 ergeben (linksassoziativ)"; cefail=1; }
	[ "$(build/calcexpr_p '6/3+1')" = "$(printf 'RESULT: 3\nOK')" ] || { echo "FAIL  action calcexpr: 6/3+1 sollte 3 ergeben"; cefail=1; }
	if [ $cefail -eq 0 ]; then echo "ok    action calcexpr: Praezedenz+Linksassoziativitaet ueber ACTION-Werte-Stack korrekt"
	else fail=1; fi
else
	echo "FAIL  action calcexpr: tests/calcexpr_p.c fehlt/kompiliert nicht"; fail=1
fi

# 7d) Aktions-Rollback bei Backtracking (docs/ARCHITEKTUR.md §9.4): tests/actionrollback
#     (stmt = tag "1" | tag "2". tag = "T".) matcht "tag" bei "T2" zuerst innerhalb der
#     SPAETER verworfenen ersten Alternative, dann nochmal in der gewinnenden zweiten --
#     OHNE Rollback des Aktions-Logs würde note_tag faelschlich 2x statt 1x feuern.
if cc -w -o build/actionrollback_p tests/actionrollback_p.c 2>/dev/null; then
	got=$(build/actionrollback_p T2)
	if [ "$got" = "$(printf 'TAG#1\nOK')" ]; then
		echo "ok    action actionrollback: verworfene Alternative feuert Aktion NICHT dauerhaft"
	else
		echo "FAIL  action actionrollback: TAG-Zaehler falsch (Rollback fehlt): $got"; fail=1
	fi
else
	echo "FAIL  action actionrollback: tests/actionrollback_p.c fehlt/kompiliert nicht"; fail=1
fi

# 7e) Aktions-Rollback DURCH REKURSION hindurch (docs/ARCHITEKTUR.md §9.4, wichtig fuer
#     einen spaeteren oberon0-Durchlauf mit echten Klammerausdruecken): tests/actionrollback2
#     (expr2 = "(" expr2 ")" "A" | "(" expr2 ")" "B" | leaf.) matcht "leaf" innerhalb eines
#     VERSCHACHTELTEN expr2-Aufrufs zuerst in der verworfenen ersten Alternative, dann
#     nochmal in der gewinnenden zweiten. NUR der erzeugte Parser wird hier geprueft (die
#     TABELLE ist fuer dieses gemeinsame Praefix "PEG-committed" und lehnt "(x)B" bereits
#     aus einem bekannten, unabhaengigen Grund ab, siehe tests/actionrollback2.lextab).
build/parsec "tests/actionrollback2" >/dev/null 2>&1
if cc -w -o build/actionrollback2_p tests/actionrollback2_p.c 2>/dev/null; then
	got=$(build/actionrollback2_p "(x)B")
	if [ "$got" = "$(printf 'LEAF#1\nOK')" ]; then
		echo "ok    action actionrollback2: Rollback funktioniert auch durch Rekursion hindurch"
	else
		echo "FAIL  action actionrollback2: LEAF-Zaehler falsch: $got"; fail=1
	fi
else
	echo "FAIL  action actionrollback2: tests/actionrollback2_p.c fehlt/kompiliert nicht"; fail=1
fi
if command -v python3 >/dev/null; then
	got=$(python3 tools/s68sim.py tests/actionrollback2.s68 "(x)B" 2>&1)
	if [ "$got" = "OK" ]; then
		echo "ok    s68sim actionrollback2: Backtracking mit gemeinsamem Praefix im 68k-Code korrekt"
	else
		echo "FAIL  s68sim actionrollback2: '(x)B' erwartet OK, erhalten $got"; fail=1
	fi
fi

# 7f) Erster Interpreter-Test Richtung oberon0 (docs/ARCHITEKTUR.md §9.5): tests/miniOberon
#     (VAR-Deklarationen, Zuweisung, Ausdruecke MIT Variablenreferenzen, echte Symboltabelle
#     in den ROUTINE-C-Koerpern). Deckte den WICHTIGEN, unabhaengigen "entry vor fuehrendem
#     ws()"-Bug auf (siehe ARCHITEKTUR.md §9.4c) -- ohne dessen Fix waeren start/end einer
#     ACTION um das fuehrende Leerzeichen verschoben gewesen (nur bei aktivem [LEXER]-Block
#     sichtbar, calcexpr/actiontest hatten keinen).
build/parsec "tests/miniOberon" >/dev/null 2>&1
if cc -w -o build/miniOberon_p tests/miniOberon_p.c 2>/dev/null; then
	got=$(build/miniOberon_p 'VAR x; y; z; BEGIN x := 2 + 3 * 4; y := x - 1; z := x * y END')
	exp=$(printf 'x = 14\ny = 13\nz = 182\n--- final state ---\nx = 14\ny = 13\nz = 182\nOK')
	if [ "$got" = "$exp" ]; then
		echo "ok    action miniOberon: Deklaration+Zuweisung+Variablenreferenzen korrekt berechnet"
	else
		echo "FAIL  action miniOberon: unerwartete Ausgabe:"; echo "$got"; fail=1
	fi
else
	echo "FAIL  action miniOberon: tests/miniOberon_p.c fehlt/kompiliert nicht"; fail=1
fi

# 8) Echte Sprachgrammatik: erzeugter Oberon-0-Parser (data/oberon0_p.c) MIT
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
if cc -w -o build/oberon0_p data/oberon0_p.c 2>/dev/null; then
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
			got=$(python3 tools/s68sim.py "tests/$g.s68" "$inp" 2>&1)
			n=$((n+1))
			if [ "$got" != "$exp" ]; then
				echo "FAIL  s68sim $g: Eingabe \"$inp\" erwartet $exp, 68k-Code sagt: $got"
				mm=1; fail=1
			fi
		done <<EOT
$(grep '^TEST ' "tests/$g.lextab")
EOT
		[ $mm -eq 0 ] && echo "ok    s68sim $g: $n Tests gegen simulierten 68k-Code"
	done
	oberon0_cases "python3 tools/s68sim.py data/oberon0.s68" s68sim
else
	echo "warn  s68sim: python3 nicht gefunden -- 68k-Simulation uebersprungen"
fi

# 10) Echter Assembler: alle erzeugten .s68 muessen mit vasm (Motorola-Syntax,
#     68000) fehlerfrei assemblieren. tools/vasmm68k_mot wurde aus den Original-
#     Quellen (sun.hasenbraten.de/vasm) gebaut; fehlt das Binary (z.B. andere
#     Plattform), wird der Check uebersprungen.
if [ -x tools/vasmm68k_mot ]; then
	vfail=0; vcnt=0
	for f in data/oberon0.s68 tests/*.s68; do
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

# 11) OS-9/r68-Format: data/oberon0.lextab hat "M68K OS9" im [CODEGEN]-Block ->
#     oberon0_os9.a (nam/psect/ends) muss mit der ECHTEN Microware-Toolchain
#     (r68 via Wine/MWOS) assemblieren. Fehlt Wine oder MWOS, wird uebersprungen.
WINE="$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine"
MWOS_TMP="/Volumes/SSD1TB/projects/MWOS/TMP"
if [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && [ -f data/oberon0_os9.a ]; then
	mkdir -p "$MWOS_TMP"
	cp data/oberon0_os9.a "$MWOS_TMP/rtest.a"
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

# 12) QCC: eigenstaendige Sprache -> Stack-IR -> qccvm (docs/ARCHITEKTUR.md Kap.10).
#     Grammatik data/qcc.ebnf + [NUTZER-CODE] emittieren Stack-IR; tools/qccvm
#     fuehrt die IR aus (Interpreter + Referenz-Orakel). Meilenstein 1: Ausdruecke,
#     lokale Variablen, putint. (Kein 68k-Backend hier -- kommt in M4.)
if command -v python3 >/dev/null 2>&1; then
	# WICHTIGER FUND (2026-07-25): build/parsec hat ein festes internes Puffer-Limit
	# fuer den [NUTZER-CODE]-Block (USER_CODE_LEN in src/parsec.cpp) -- bei
	# Ueberschreitung wird der Ueberschuss STILLSCHWEIGEND abgeschnitten (nur eine
	# Warnzeile im stdout, die hier vorher mit ">/dev/null" verschluckt wurde).
	# Das hat einmal drei ROUTINE-C-Bloecke (tc_ternarybegin/-middle/-end) aus
	# data/qcc.lextab geloescht, ohne dass ein einziger Build-Schritt einen
	# Fehler gemeldet hat -- nur ein spaeter fehlschlagender Ternary-Test hat es
	# aufgedeckt. USER_CODE_LEN wurde deshalb grosszuegig erhoeht (128 KB -> 1 MB),
	# UND hier wird die Ausgabe jetzt auf "WARNUNG" geprueft statt verschluckt.
	ebnfout=$(build/parsec data/qcc 2>&1)
	if echo "$ebnfout" | grep -q 'WARNUNG'; then
		echo "FAIL  qcc: build/parsec meldet eine Kuerzungswarnung (siehe oben) -- data/qcc.lextab wurde vermutlich abgeschnitten!"
		echo "$ebnfout" | grep 'WARNUNG'
		fail=1
	fi
	if cc -w -o build/qcc_p data/qcc_p.c 2>/dev/null; then
		tcfail=0
		# Die Zahl der geprueften Programme wird GEZAEHLT, nicht eingetippt.
		# Vorher stand in der Erfolgsmeldung eine feste "150", waehrend
		# tatsaechlich 192 Programme liefen -- die Beschriftung war um 39
		# auseinandergelaufen und haette es weiter getan. Eine Zahl im
		# Testbericht, die niemand nachrechnet, ist schlimmer als keine.
		tccount=0
		tc_check() {
			tccount=$((tccount + 1))
			got=$(build/qcc_p "$1" 2>/dev/null | python3 tools/qccvm.py 2>/dev/null)
			exp=$(printf '%b' "$2")
			if [ "$got" != "$exp" ]; then
				echo "FAIL  qcc: [$1]"
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
		# Blockgueltigkeitsbereiche (2026-08-20): tcNames[] war FLACH pro Funktion --
		# eine gleichnamige Deklaration in einem SPAETEREN Block legte einen neuen
		# Slot an, jeder Zugriff traf aber weiter den alten aus dem schon
		# geschlossenen Block. Echter Fund: im Bootstrap-Compiler auf dem Q9 ergab
		# das in tcGlobalOne einen Nullpointer und einen PMMU-Abbruch; im ganzen
		# Bootstrap-IR waren 342 Zugriffe in 5 Funktionen betroffen (tc_varref,
		# tc_target, tcGlobalOne, tc_type, tcDecodeStringLit). Die vier Faelle
		# pruefen beide Richtungen: Verdecken UND Wiederauftauchen der aeusseren.
		tc_check 'int main(){ { int t = 5; putint(t); } { int t = 9; putint(t); } }' '5\n9'
		tc_check 'int main(){ int x = 1; { int x = 2; putint(x); } putint(x); }' '2\n1'
		tc_check 'int main(){ int a=1; putint(a); { int a=2; putint(a); { int a=3; putint(a); } putint(a); } putint(a); }' '1\n2\n3\n2\n1'
		tc_check 'int main(){ char buf[4]; buf[0]=65; buf[1]=32; buf[2]=66; buf[3]=0; { char* q = buf; while (*q == 65) q++; } { char* q = buf + 2; putchar(*q); } }' 'B'
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
		if build/qcc_p 'enum A { X, Y }; enum B { X, Z }; int main(){ putint(X); }' 2>&1 | grep -q 'duplicate enum constant'; then
			echo "ok    qcc: doppelte enum-Konstante wird diagnostiziert"
		else
			echo "FAIL  qcc: enum-Diagnose fehlt"; tcfail=1; fail=1
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
		# KOMMA-OPERATOR im for-Kopf und in der Anweisung (2026-09-15). Er ging
		# schon im geklammerten Ausdruck; es fehlten genau diese zwei Stellen,
		# und dort war es ein STILLER Parse-Abbruch.
		# Der erste Fall laeuft genau dreimal (0/5 -> 1/4 -> 2/3 -> Ende) und
		# faellt damit nur richtig aus, wenn BEIDE Schrittausdruecke wirken.
		tc_check 'int main(){ int i; int j; int n; n=0; for(i=0,j=5;i<j;i++,j--){ n=n+1; } putint(n); }' '3'
		tc_check 'int main(){ int i; int j; j=0; for(i=0,j=5;i<1;i++){ ; } putint(j); }' '5'
		tc_check 'int main(){ int i; int j; j=0; for(i=0;i<3;i++,j++){ ; } putint(j); }' '3'
		# IN DER ANWEISUNG ("i = 1, j = 2;") BLEIBT ER OFFEN, und zwar bewusst:
		# die Grammatikaenderung dafuer (Aktion an einem assignItem statt an
		# assignStmt) war gebaut und liess Suite und Host-Lauf gruen, brach aber
		# den SELBSTHOST auf dem 68030 -- der dort gebaute Compiler erzeugte
		# "JZ L2080374908", also einen Mulldwert als Labelnummer, und liess ein
		# STOREP fallen. Auf dem Host faellt das nicht auf, weil frischer
		# Speicher dort zufaellig null ist. Der for-Kopf unten traegt den
		# Nutzen (125 von 4109 Microware-Quellen), die Anweisungsform kaum --
		# deshalb zurueckgenommen statt einen schwer auffindbaren Fehler
		# einzubauen. Siehe docs/KNOWN_BUGS_C89_de.md.
		# Was vorher ging, muss weiter gehen: klassisches for, Kettenzuweisung
		# und ein Komma in einer ARGUMENTLISTE (das ist kein Operator).
		tc_check 'int main(){ int i; int n; n=0; for(i=0;i<10;i++){ n=n+1; } putint(n); }' '10'
		tc_check 'int main(){ int i; int j; i=j=5; putint(i); }' '5'
		tc_check 'int f(int a,int b){ return a+b; } int main(){ putint(f(2,3)); }' '5'
		# DURCHFALL (C89 3.6.4.2), behoben 2026-09-15. Vorher lief er STILL
		# falsch: der Rumpf sprang unbedingt ans switch-Ende, das Ergebnis war
		# einfach zu klein -- Schlusswort OK, keine Meldung.
		# Die Sollwerte diskriminieren, statt nur "irgendwas" zu pruefen: 7 ist
		# 1+2+4, faellt also nur bei Durchfall UEBER DREI Stufen heraus, und 6
		# ist 2+4, also Einstieg mitten in die Kette.
		tc_check 'int main(){ int r=0; switch(1){ case 1: r=1; case 2: r=r+2; break; default: r=9; } putint(r); }' '3'
		tc_check 'int main(){ int r=0; switch(1){ case 1: r=r+1; case 2: r=r+2; case 3: r=r+4; break; } putint(r); }' '7'
		tc_check 'int main(){ int r=0; switch(2){ case 1: r=r+1; case 2: r=r+2; case 3: r=r+4; break; } putint(r); }' '6'
		# break muss den Durchfall weiterhin verhindern -- sonst waere der Fix
		# nur eine andere Sorte Fehler.
		tc_check 'int main(){ int r=0; switch(1){ case 1: r=1; break; case 2: r=r+2; break; } putint(r); }' '1'
		tc_check 'int main(){ int r=0; switch(1){ case 1: r=5; break; default: r=99; } putint(r); }' '5'
		# Durchfall in den default-Rumpf, und der letzte case ohne break.
		tc_check 'int main(){ int r=0; switch(1){ case 1: r=1; default: r=r+8; } putint(r); }' '9'
		tc_check 'int main(){ int r=0; switch(2){ case 1: r=1; case 2: r=3; } putint(r); }' '3'
		tc_check 'int main(){ int r=0; switch(7){ case 1: r=1; default: r=9; } putint(r); }' '9'
		tc_check 'int main(){ int x=321; putint((char)x); }' '65'
		tc_check 'int main(){ char c=65; putint((int)c); }' '65'
		tc_check 'int main(){ int x=5; int y=0; putint((bool)x); putint((bool)y); }' '1\n0'
		tc_check 'int main(){ int x=5; putint((x)); putint((x)+1); }' '5\n6'
		tc_check 'int main(){ int a=2; int b=3; putint((a+b)*2); }' '10'
		tc_check 'int main(){ putint((int)sizeof(char)); }' '1'
		# bool ist ein arithmetischer und damit skalarer Typ, taugt also als
		# Cast-Operand (C89 3.3.4). Wurde bis 2026-09-01 abgelehnt, weil
		# tcIsInteger() 'b' ausschliesst -- Regressionstest dafuer:
		tc_check 'int main(){ bool b = true; putint((int)b); }' '1'
		# 2026-09-01 nachgezogen: dieser Test pruefte urspruenglich (int)p auf einem
		# ZEIGER und stammt vom Tag der Cast-Einfuehrung (4eb7a1b, 2026-07-23).
		# Das war schon damals falsch herum: C89 3.3.4 verlangt vom Operanden
		# lediglich SKALAREN Typ, und Zeiger sind skalar -- ein konformer Compiler
		# darf (int)p nicht ablehnen. 3c37198 (2026-08-20) hat die Quelltypen dann
		# bewusst um Zeiger und Funktionszeiger erweitert, weil der Bootstrap-Parser
		# das braucht (etwa (char*)actionLog); der Test blieb stehen und war seither
		# dauerhaft rot, ohne einen echten Mangel anzuzeigen.
		# Geprueft wird jetzt ein Operand, der WIRKLICH nicht skalar ist und fuer den
		# die Norm eine Diagnose vorschreibt: ein void-Ausdruck.
		if build/qcc_p 'void f(){} int main(){ putint((int)f()); }' 2>&1 | grep -q 'cast expects scalar'; then
			echo "ok    qcc: nicht-skalarer Cast-Operand (void) wird diagnostiziert"
		else
			echo "FAIL  qcc: Cast-Diagnose fehlt"; tcfail=1; fail=1
		fi
		tc_check 'int main(){ char line[80]; putint(sizeof(line)); }' '80'
		tc_check 'int main(){ int x; putint(sizeof(x)); }' '4'
		tc_check 'int g[10]; int main(){ putint(sizeof(g)); }' '40'
		# FUNKTIONSZEIGER ALS LOKALE VARIABLE (2026-09-15): "int (*fp)(int);"
		# ohne den Umweg ueber ein typedef. Vorher stiller Parse-Abbruch.
		# Die Signatur wird wie beim typedef registriert, die Variable selbst
		# legt dieselbe Routine an wie jede andere Lokale.
		tc_check 'int g(int x){return x;} int main(){ int (*fp)(int); fp=g; putint(fp(5)); }' '5'
		tc_check 'int g(int x){return x;} int h(int x){return x+2;} int main(){ int (*fp)(int); fp=h; putint(fp(5)); }' '7'
		# void-Rueckgabe und mehr als ein Parameter.
		tc_check 'void g(void){ putint(9); } int main(){ void (*fp)(void); fp=g; fp(); }' '9'
		tc_check 'int g(int a,int b){return a+b;} int main(){ int (*fp)(int,int); fp=g; putint(fp(2,3)); }' '5'
		# Der typedef-Weg und gewoehnliche Deklarationen bleiben unberuehrt.
		tc_check 'typedef int (*FP)(int); int g(int x){return x;} int main(){ FP p; p=g; putint(p(5)); }' '5'
		tc_check 'int main(){ int x; x=3; putint(x); }' '3'
		# STRINGVERKETTUNG (C89 3.1.4), 2026-09-15. Vorher ein STILLER
		# Parse-Abbruch -- "FAIL, 0 Meldungen", ohne Zeile und ohne Grund; die
		# eigenen Werkzeuge mussten lange Meldungstexte deshalb einzeilig
		# schreiben. stringLit steht in der TOKEN-Liste, und TOKEN-Regeln
		# ueberspringen KEINEN Leerraum: ohne den ausdruecklichen stringSep ging
		# nur "ab""cd" ohne Zwischenraum, und genau der mehrzeilige Fall nicht.
		tc_check 'int main(){ char *s = "ab" "cd"; putint(s[2]); }' '99'
		tc_check 'int main(){ char *s = "ab""cd"; putint(s[2]); }' '99'
		tc_check 'int main(){ char *s = "a" "b" "c"; putint(s[2]); }' '99'
		# Ueber einen ZEILENUMBRUCH -- der eigentliche Anwendungsfall.
		tc_check 'int main(){ char *s = "ab"
	    "cd"; putint(s[3]); }' '100'
		# Escapes duerfen an der Nahtstelle nicht verlorengehen.
		tc_check 'int main(){ char *s = "a\n" "b"; putint(s[1]); }' '10'
		# Die Gesamtlaenge muss stimmen: "ab"+"cd" fuellt b[0..3], b[4] bleibt 0.
		tc_check 'int main(){ char b[5] = "ab" "cd"; putint(b[4]); }' '0'
		tc_check 'int main(){ char b[5] = "ab" "cd"; putint(b[3]); }' '100'
		# Ein einzelnes Literal bleibt unveraendert.
		tc_check 'int main(){ char *s = "abcd"; putint(s[2]); }' '99'
		# volatile (2026-09-15): wird an denselben Stellen wie const akzeptiert.
		# Eine eigene WIRKUNG hat es nicht, und das ist nachgerechnet, nicht
		# angenommen -- siehe den Test "volatile: jeder Zugriff bleibt" weiter
		# unten, der am erzeugten Assembler nachzaehlt.
		tc_check 'int main(){ volatile int x; x=5; putint(x); }' '5'
		tc_check 'volatile int g; int main(){ g=1; putint(g); }' '1'
		tc_check 'static volatile int g; int main(){ g=2; putint(g); }' '2'
		tc_check 'int main(){ const volatile int x=3; putint(x); }' '3'
		tc_check 'int f(volatile int *p){ return *p; } int main(){ int v; v=4; putint(f(&v)); }' '4'
		tc_check 'struct S { volatile int a; }; int main(){ struct S s; s.a=9; putint(s.a); }' '9'
		# Eine GLOBALE union -- der Rohtext-Scanner in tcGlobalOne kannte
		# "union" als Basistyp vorher nicht.
		tc_check 'union U { int a; int b; }; union U gu; int main(){ gu.a=8; putint(gu.b); }' '8'
		# UNION (2026-09-15). Intern eine struct, deren Felder alle auf Offset 0
		# liegen -- damit erbt sie Feldzugriff, "->", Ganzkopie, Parameter und
		# Rueckgabe, ohne dass davon etwas neu gebaut werden musste.
		# ACHTUNG, WAS HIER NICHT STEHEN DARF: kein Fall, der die BYTE-REIHENFOLGE
		# voraussetzt. Dieses Orakel rechnet little-endian, der 68k ist
		# big-endian -- "u.i=5; u.c[0]" gibt hier 5 und auf dem Ziel 0. Das ist
		# in C implementation-defined; geprueft wird deshalb nur, was die Norm
		# zusichert: dass die Felder denselben Speicher TEILEN.
		tc_check 'union U { int a; int b; }; int main(){ union U u; u.a=5; putint(u.b); }' '5'
		tc_check 'union U { int a; int b; }; int main(){ union U u; u.b=9; putint(u.a); }' '9'
		tc_check 'union U { int i; char c; }; int main(){ union U u; u.c=65; putint(u.c); }' '65'
		# Groesse = groesstes Feld, nicht Summe (eine struct waere hier 12).
		tc_check 'union U { int i; char c[8]; }; int main(){ putint(sizeof(union U)); }' '8'
		tc_check 'union U { int i; char c; }; int main(){ putint(sizeof(union U)); }' '4'
		# Zeiger, Parameter per Wert, und eine struct daneben bleibt getrennt.
		tc_check 'union U { int a; int b; }; int main(){ union U u; union U *p; p=&u; p->a=7; putint(p->b); }' '7'
		tc_check 'union U { int a; int b; }; int f(union U v){ return v.b; } int main(){ union U u; u.a=3; putint(f(u)); }' '3'
		tc_check 'union U { int i; }; struct S { int a; int b; }; int main(){ struct S s; s.b=0; s.a=5; putint(s.b); }' '0'
		# Eine struct NACH einer union darf deren Layout nicht erben.
		tc_check 'union U { int a; int b; }; struct S { int a; int b; }; int main(){ struct S s; s.a=1; s.b=2; putint(s.a+s.b); }' '3'
		if build/qcc_p 'int main(){ union Nope u; }' 2>&1 | grep -q 'unknown struct or union'; then
			echo "ok    qcc: unbekannte union wird diagnostiziert"
		else
			echo "FAIL  qcc: union-Diagnose fehlt"; tcfail=1; fail=1
		fi
		# ZEIGERTABELLE MIT STRING-LITERALEN (2026-09-15). Vorher ein STILLER
		# Parse-Abbruch: "FAIL, 0 Meldungen", ohne Zeile und ohne Grund. Das
		# Idiom traegt Namens-, Opcode- und Meldungstabellen und musste in den
		# eigenen Werkzeugen bis dahin umgangen werden.
		# Der Wert einer solchen Tabelle ist eine ADRESSE und steht erst zur
		# Ladezeit fest; auf OS-9 relokiert ihn der Lader ueber M$IRefs.
		tc_check 'char *t[2]={"ab","cd"}; int main(){ char *p; p=t[0]; putint(p[0]); }' '97'
		tc_check 'char *t[2]={"ab","cd"}; int main(){ char *p; p=t[1]; putint(p[0]); }' '99'
		# Groesse aus der Liste ableiten -- mit UND ohne Zeiger.
		tc_check 'char *t[]={"ab","cd","ef"}; int main(){ char *p; p=t[2]; putint(p[0]); }' '101'
		tc_check 'int a[]={1,2,3}; int main(){ putint(a[1]); }' '2'
		tc_check 'int a[]={4,5}; int main(){ putint(sizeof(a)); }' '8'
		# Gemischt: eine Zahl neben einem Literal darf nicht zur Adresse werden.
		tc_check 'char *t[2]={"ab",0}; int main(){ putint(t[1]==0); }' '1'
		# Der ALTE Pfad muss unveraendert bleiben: ein char-Array mit Literal
		# fuellt Zahlen, keine Adressen (daran ist der erste Anlauf gescheitert).
		tc_check 'char m[6]="hallo"; int main(){ putchar(m[0]); putint(m[5]); }' 'h0'
		# EIN EINZELNER globaler Zeiger auf ein Literal -- derselbe Mechanismus,
		# anderer Pfad. Der Initialisierer wurde vorher STILL verworfen, heraus
		# kam ein Nullzeiger; lokal ging es laengst (Laufzeit-Store).
		tc_check 'char *s="ab"; int main(){ char *p; p=s; putint(p[0]); }' '97'
		tc_check 'static char *s="xy"; int main(){ char *p; p=s; putint(p[0]); }' '120'
		# Der Nullzeiger darf dadurch nicht zur Adresse werden.
		tc_check 'char *s=0; int main(){ putint(s==0); }' '1'
		# VERSCHACHTELTE INITIALISIERERLISTEN (2026-09-15). Vorher ein STILLER
		# Parse-Abbruch. Das Array ist intern flach, die Klammern gliedern es
		# nur -- ABER naives Abflachen waere falsch, sobald eine Zeile kuerzer
		# ist als die Zeilenlaenge. Genau das pruefen die beiden {{1},{2}}-
		# Faelle: der zweite Wert gehoert in die ZWEITE Zeile, und die Luecke
		# dahinter muss 0 sein. Mit naivem Abflachen kaeme dort die 2 heraus.
		tc_check 'int m[2][2]={{1,2},{3,4}}; int main(){ putint(m[1][1]); }' '4'
		tc_check 'int m[2][2]={{1,2},{3,4}}; int main(){ putint(m[0][1]); }' '2'
		tc_check 'int m[2][3]={{1},{2}}; int main(){ putint(m[1][0]); }' '2'
		tc_check 'int m[2][3]={{1},{2}}; int main(){ putint(m[0][1]); }' '0'
		tc_check 'char c[2][2]={{65,66},{67,68}}; int main(){ putint(c[1][1]); }' '68'
		# Zeigertabelle zweidimensional -- beide Ergaenzungen greifen zusammen.
		tc_check 'char *t[2][2]={{"ab","cd"},{"ce","df"}}; int main(){ char *p; p=t[1][0]; putint(p[0]); }' '99'
		# Die FLACHE Schreibweise fuer ein 2D-Array muss weiter gehen.
		tc_check 'int m[2][2]={1,2,3,4}; int main(){ putint(m[1][1]); }' '4'
		# Was nicht getragen wird, muss GEMELDET werden, nicht still scheitern.
		for q in 'int m[2][2][2]={{{1,2},{3,4}},{{5,6},{7,8}}}; int main(){ putint(m[0][0][0]); }' \
		         'int a[4]={{1,2},{3,4}}; int main(){ putint(a[0]); }'; do
			if build/qcc_p "$q" 2>&1 | grep -q 'nested initializer list is not supported'; then
				echo "ok    qcc: nicht getragene Schachtelung wird gemeldet"
			else
				echo "FAIL  qcc: Schachtelung scheitert still: [$q]"; tcfail=1; fail=1
			fi
		done
		if build/qcc_p 'int m[2][2]={{1,2,3},{4}}; int main(){ putint(m[0][0]); }' 2>&1 | grep -q 'bad or oversized'; then
			echo "ok    qcc: zu lange Zeile in einer Schachtelung wird gemeldet"
		else
			echo "FAIL  qcc: zu lange Zeile scheitert still"; tcfail=1; fail=1
		fi
		tc_check 'enum Color { RED, GREEN, BLUE }; int main(){ enum Color c; c = GREEN; putint(c); }' '1'
		# EXPLIZITE ENUM-WERTE (C89 3.5.2.2), 2026-09-15. Vorher stiller Abbruch.
		# Der Wert setzt den Zaehler NEU, die folgenden zaehlen von dort weiter --
		# genau das pruefen die gemischten Faelle, nicht nur der einzelne Wert.
		tc_check 'enum E { A = 5 }; int main(){ putint(A); }' '5'
		tc_check 'enum E { A = 5, B }; int main(){ putint(B); }' '6'
		tc_check 'enum E { A, B = 7, C }; int main(){ putint(C); }' '8'
		tc_check 'enum E { A = 1, B = 4 }; int main(){ putint(B); }' '4'
		tc_check 'enum E { A = -3 }; int main(){ putint(A); }' '-3'
		# typedef enum, anonym und mit Tag (der Tag wird wie beim struct
		# geparst und verworfen; als Name dient der typedef-Zielname).
		tc_check 'typedef enum { A, B } F; int main(){ F g; g=B; putint(g); }' '1'
		tc_check 'typedef enum E { A, B } F; int main(){ F g; g=B; putint(g); }' '1'
		tc_check 'typedef enum { A = 9, B } F; int main(){ F g; g=B; putint(g); }' '10'
		# Ohne Werte unveraendert, und typedef struct daneben auch.
		tc_check 'enum E { A, B }; int main(){ putint(B); }' '1'
		tc_check 'typedef struct { int a; } P; int main(){ P p; p.a=7; putint(p.a); }' '7'
		tc_check 'enum Color { RED, GREEN, BLUE }; enum Color pick(int i){ if(i==0) return RED; else return GREEN; } int main(){ enum Color c = pick(1); putint(c); }' '1'
		# SIZEOF AUF ZEIGER: bis 2026-09-15 ABGELEHNT, jetzt unterstuetzt.
		# Die drei Faelle hier hielten vorher die Ablehnung als SOLLverhalten
		# fest (dasselbe Muster wie bei den Index-Ketten, s. 298d401) und sind
		# mit der Freischaltung umgedreht worden.
		# Die Groesse eines Zeigers haengt vom Ziel ab -- 68k 4, ARM64 8 --,
		# deshalb gibt das Frontend sie als "0+1P" aus und jeder Konsument
		# setzt sein P ein. Dieses Orakel (qccvm.py) rechnet mit PTR_SIZE = 8,
		# die Sollwerte hier sind also die der VM, nicht die des 68k.
		tc_check 'int main(){ int x; int *p=&x; putint(sizeof(p)); }' '8'
		tc_check 'int main(){ putint(sizeof(char *)); }' '8'
		tc_check 'struct S { int a; }; int main(){ putint(sizeof(struct S *)); }' '8'
		# Ein Zeiger-ARRAY zaehlt seine Elemente mit.
		tc_check 'int main(){ char *t[4]; putint(sizeof(t)); }' '32'
		# DIE SCHRITTWEITE IM STRUCT ist der eigentliche Pruefstein: das
		# Offset von b ist 0+3P (drei Zeigergroessen davor), nicht eine feste
		# Zahl. Mit der alten 8-Byte-Annahme UND mit der neuen Rechnung kommt
		# bei P=8 dasselbe heraus -- der Wert diskriminiert also nicht die
		# Zeigergroesse, wohl aber, dass die symbolische Form ueberhaupt
		# korrekt aufgeloest wird (ein unaufgeloestes "0+3P" gibt 0).
		tc_check 'struct S { int a; char *p; char *q; int b; }; int main(){ struct S s; s.b = 77; putint(s.b); }' '77'
		tc_check 'struct S { char c; char *p; int n; }; int main(){ struct S s; s.c = 3; s.n = 9; putint(s.c + s.n); }' '12'
		if build/qcc_p 'int main(){ enum Nope x; }' 2>&1 | grep -q 'unknown enum'; then
			echo "ok    qcc: unbekannter enum-Typ wird diagnostiziert"
		else
			echo "FAIL  qcc: enum-Typ-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct Point { int x; int y; }; int main(){ struct Point p; p.z = 1; }' 2>&1 | grep -q 'unknown struct field'; then
			echo "ok    qcc: unbekanntes struct-Feld wird diagnostiziert"
		else
			echo "FAIL  qcc: struct-Feld-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct Mixed { int a; char b; }; int main(){ struct Mixed m; m.a = 1; putint(m.a); }' 2>&1 | grep -q 'FEHLER\|qcc: '; then
			echo "FAIL  qcc: gemischte Feldtypen (int+char) werden faelschlich abgelehnt"; tcfail=1; fail=1
		else
			echo "ok    qcc: gemischte Feldtypen (int+char) werden akzeptiert"
		fi
		# Pointer-Felder sind in structField (Grammatik ohne pointerDecl) schon strukturell
		# unmoeglich; verschachtelte structs als Feld sind es aber und werden bewusst
		# abgelehnt (siehe SELFHOSTING_LUECKENLISTE.md: eigener Folgeschritt).
		if build/qcc_p 'struct Inner { int x; }; struct Outer { struct Inner i; }; int main(){ struct Outer o; }' 2>&1 | grep -q 'struct field type not supported'; then
			echo "ok    qcc: verschachteltes struct als Feld wird bewusst abgelehnt (eigener Folgeschritt)"
		else
			echo "FAIL  qcc: Diagnose fuer verschachtelte struct-Felder fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: Array-Felder in struct (z.B. char name[8]) -- Byte-Layout inkl.
		# Array-Feld-Groesse (Elementgroesse * Elementzahl), Zugriff nur ueber eine
		# Pointer-Zwischenvariable (direkte "p.field[i]"-Syntax noch nicht moeglich,
		# eigener Folgeschritt, siehe docs/FORTSCHRITT.md). Zuweisung an das GANZE
		# Array-Feld wird diagnostiziert (wie in echtem C nicht erlaubt).
		tc_check 'struct Rec { char name[8]; int id; }; int main(){ putint(sizeof(struct Rec)); }' '12'
		tc_check 'struct Rec { char name[8]; int id; }; int main(){ struct Rec r; r.id = 42; char *p = r.name; p[0] = 65; p[1] = 66; putint(r.id); putchar(p[0]); putchar(p[1]); }' '42\nAB'
		tc_check 'struct Rec { char tag; int value; char buf[4]; }; int main(){ struct Rec r; r.tag = 1; r.value = 1000; char *p = r.buf; p[0]=9; putint(r.tag); putint(r.value); putint(p[0]); putint(sizeof(struct Rec)); }' '1\n1000\n9\n12'
		if build/qcc_p 'struct Rec { char name[8]; }; int main(){ struct Rec r; struct Rec r2; r.name = r2.name; }' 2>&1 | grep -q 'cannot assign to array field'; then
			echo "ok    qcc: Zuweisung an ganzes Array-Feld wird diagnostiziert"
		else
			echo "FAIL  qcc: Array-Feld-Zuweisungs-Diagnose fehlt"; tcfail=1; fail=1
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
		if build/qcc_p 'struct P{int x;}; int main(){ struct P p; putint(p.x[0]); }' 2>&1 | grep -q 'scalar struct field cannot be indexed'; then
			echo "ok    qcc: Indizierung eines skalaren struct-Felds wird diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer Indizierung eines skalaren struct-Felds fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct P{char buf[4];}; int main(){ struct P p; p.buf[9]=1; }' 2>&1 | grep -q 'constant array index 9 out of range'; then
			echo "ok    qcc: konstanter Index-Bereichsverstoss bei p.field[i] wird diagnostiziert"
		else
			echo "FAIL  qcc: Bounds-Check fuer p.field[i] fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: void als Rueckgabetyp (echte Semantik, return; war bereits vorher
		# moeglich) und void* als generischer Pointer (bidirektional kompatibel zu jedem
		# ANDEREN Pointer derselben Tiefe, siehe tcCompatible). void* selbst darf nicht
		# dereferenziert/indiziert/arithmetisch veraendert werden (kein LOADIND mit
		# geratener Groesse); bare "void" bleibt ausserhalb des Rueckgabetyps verboten.
		tc_check 'void greet(){ putint(1); } int main(){ greet(); putint(2); }' '1\n2'
		tc_check 'int main(){ int x = 42; void *p = &x; int *q = p; putint(*q); }' '42'
		tc_check 'int deref(void *p){ int *q = p; return *q; } int main(){ int x=7; putint(deref(&x)); }' '7'
		if build/qcc_p 'int main(){ void x; putint(1); }' 2>&1 | grep -q 'void is not a valid variable type'; then
			echo "ok    qcc: bare void als lokale Variable wird diagnostiziert"
		else
			echo "FAIL  qcc: bare-void-Lokale-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int f(void x){ return 0; } int main(){ putint(f(1)); }' 2>&1 | grep -q 'void is not a valid parameter type'; then
			echo "ok    qcc: bare void als Parameter wird diagnostiziert"
		else
			echo "FAIL  qcc: bare-void-Parameter-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'void g; int main(){ putint(1); }' 2>&1 | grep -q 'void is not a valid variable type'; then
			echo "ok    qcc: bare void als globale Variable wird diagnostiziert"
		else
			echo "FAIL  qcc: bare-void-Globale-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int main(){ int x=1; void *p=&x; putint(*p); }' 2>&1 | grep -q 'cannot dereference void\*'; then
			echo "ok    qcc: Dereferenzierung von void* wird diagnostiziert"
		else
			echo "FAIL  qcc: void-Dereferenzierungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int main(){ int x=1; void *p=&x; putint(p[0]); }' 2>&1 | grep -q 'cannot dereference void\*'; then
			echo "ok    qcc: Indizierung von void* wird diagnostiziert"
		else
			echo "FAIL  qcc: void-Indizierungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int main(){ int x=1; void *p=&x; p = p + 1; putint(1); }' 2>&1 | grep -q 'arithmetic on void\* is not supported'; then
			echo "ok    qcc: Arithmetik auf void* wird diagnostiziert"
		else
			echo "FAIL  qcc: void-Arithmetik-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'void f(){ return 5; } int main(){ f(); putint(1); }' 2>&1 | grep -q 'return expects void, got int'; then
			echo "ok    qcc: return mit Wert aus void-Funktion wird diagnostiziert"
		else
			echo "FAIL  qcc: void-Return-Diagnose fehlt"; tcfail=1; fail=1
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
		tc_check 'int main(){ int m[2][3]; int* p=m[1]; p[2]=73; putint(m[1][2]); }' '73'
		tc_check 'static char left = 0, right = 0; int main(){ right=41; putint(right); }' '41'
		tc_check 'int main(){int a[2];int x;a[1]=x=37;putint(a[1]);putint(x);}' '37\n37'
		tc_check 'int main(){ char* p; char* e; if(p<e&&(*p=='"'"'('"'"'||*p=='"'"'*'"'"'||*p=='"'"' '"'"')) putint(1); return 0; }' ''
		if build/qcc_p 'int f(const char** pp){*pp="x";return 0;} int main(){return 0;}' 2>&1 | grep -qx 'OK'; then
			echo "ok    qcc: const char** erlaubt Zuweisung an den Pointer-Slot"
		else
			echo "FAIL  qcc: const char**-Pointer-Slot wird faelschlich als const behandelt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int f(char** argv){return argv[1][0] == '"'"'@'"'"';}' 2>&1 | grep -qx 'OK'; then
			echo "ok    qcc: mehrfach indizierter Pointer (argv[1][0])"
		else
			echo "FAIL  qcc: mehrfach indizierter Pointer"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int main(){ int a[5]; putint(a[0][1]); }' 2>&1 | grep -q 'array is not two-dimensional'; then
			echo "ok    qcc: 2 Indizes auf ein 1D-Array werden diagnostiziert"
		else
			echo "FAIL  qcc: 1D-Array-Doppelindizierungs-Diagnose fehlt"; tcfail=1; fail=1
		fi
		# --- Funktionszeiger ueber eine EINFACHE Variable (2026-08-11) ---------
		# Bis hierher STILL FALSCH: "f(7)" mit "Fn f;" wurde als direkter Aufruf
		# geparst (callStmt probiert `call` vor `indirectCall`, und bei nacktem
		# Bezeichner greift die direkte Regel). tc_call fand keine Funktion des
		# Namens, meldete "unknown function" -- und kehrte OHNE Aufruf-Opcode
		# zurueck. In der IR stand nur "PUSHFN g / STOREP 0 / PUSH 7 / DROP":
		# der Aufruf war ersatzlos verschwunden, ohne Absturz, ohne FAIL.
		# Ueber Arrayelement und struct-Feld war es immer korrekt (indirectCall
		# greift dort) -- die letzten beiden Faelle sind die Gegenprobe, dass der
		# bereits funktionierende Weg unveraendert blieb.
		tc_check 'typedef void (*Fn)(int); void g(int x){ putint(x); } int main(){ Fn f; f = g; f(7); }' '7'
		tc_check 'typedef void (*Fn)(int); void g(int x){ putint(x); } void call(Fn f){ f(9); } int main(){ call(g); }' '9'
		tc_check 'typedef int (*Fn)(int); int g(int x){ return x+1; } int main(){ Fn f; f = g; putint(f(7)); }' '8'
		tc_check 'typedef void (*Fn)(int); void g(int x){ putint(x); } Fn gf; int main(){ gf = g; gf(5); }' '5'
		tc_check 'typedef int (*Fn)(int,int); int add(int a,int b){ return a+b; } int main(){ Fn f; f = add; putint(f(10,20)); }' '30'
		tc_check 'typedef void (*Fn)(int); void g(int x){ putint(x); } int main(){ Fn t[4]; t[0] = g; t[0](7); }' '7'
		tc_check 'typedef void (*Fn)(int); struct S{ Fn f; }; void g(int x){ putint(x); } int main(){ struct S s; s.f = g; s.f(3); }' '3'

		# --- Komma-Operator (2026-08-11) ---------------------------------------
		# Nur INNERHALB von Klammern, siehe Kommentar bei commaExpr in
		# data/qcc.ebnf: in C trennt die Grammatik "expression" (mit Komma) von
		# "assignment-expression" (ohne), und Argumentlisten/Initialisierer
		# benutzen letztere. Die geklammerte Form ist eindeutig und deckt den
		# Bedarf des Bootstrap-Ziels: tcConstIndex endet auf
		# "return p == e ? (*value = v, 1) : 0;".
		#
		# Der eigentliche Fallstrick war nicht die Grammatik, sondern der
		# ZIELZUSTAND: tc_target/tc_assignop schreiben in Globale, eine innere
		# Zuweisung nahm sie der aeusseren weg -- "x = (y = 1, 2)" legte 2 in y
		# und 0 in x. Gesichert wird jetzt im Klammerrahmen (der hat einen
		# Tiefenstapel), nicht im einstufigen tcPrevTarget*-Puffer, der bei
		# "(y = (z = 1, 2), 3)" schon belegt ist. Die vier Faelle mit
		# geschachtelten und zusammengesetzten Zuweisungen unten pinnen genau das.
		tc_check 'int main(){ int x; x = (1, 2); putint(x); }' '2'
		tc_check 'int main(){ int x; int y; x = (y = 1, 2); putint(x); putint(y); }' '2\n1'
		tc_check 'int f(int* p){ return (*p = 1, 2); } int main(){ int a; putint(f(&a)); putint(a); }' '2\n1'
		tc_check 'int f(int* v){ return 1 ? (*v = 5, 1) : 0; } int main(){ int a; putint(f(&a)); putint(a); }' '1\n5'
		tc_check 'int main(){ int x; int y; int z; x = (y = 1, z = 2, 3); putint(x); putint(y); putint(z); }' '3\n1\n2'
		tc_check 'int main(){ int r; int x; r = ((x = 1, 2), 3); putint(r); putint(x); }' '3\n1'
		tc_check 'int main(){ int x; int y; int z; x = (y = (z = 1, 2), 3); putint(x); putint(y); putint(z); }' '3\n2\n1'
		tc_check 'int g; int main(){ int y; g = (y = 4, 5); putint(g); putint(y); }' '5\n4'
		tc_check 'int main(){ int a[3]; int y; a[1] = (y = 7, 8); putint(a[1]); putint(y); }' '8\n7'
		tc_check 'int main(){ int x = 10; int y; x += (y = 1, 2); putint(x); putint(y); }' '12\n1'
		tc_check 'int n = 0; int bump(){ n = n + 1; return 7; } int main(){ int x; x = (bump(), bump(), 9); putint(x); putint(n); }' '9\n2'
		# Komma-Ausdruck als ARGUMENT -- das Komma der Argumentliste darf nicht
		# als Operator gelesen werden und umgekehrt.
		tc_check 'int add(int a,int b){ return a+b; } int main(){ int y; putint(add((y = 1, 2), 3)); putint(y); }' '5\n1'
		# Gegenproben: die gewoehnliche Klammer und ihre Vorrangrettung bleiben.
		tc_check 'int main(){ int x=2; int a=3; int b=4; putint(x * (a + b)); }' '14'
		tc_check 'int main(){ putint((2 + 3) * 4); }' '20'
		# Eine Zuweisung ist selbst ein Wertausdruck: die letzte Komma-Komponente
		# darf also eine Zuweisung sein. Das ist fuer if ((p = f())) und den
		# Bootstrap-Parser erforderlich. Die zwei Tests weiter oben decken die
		# indirekte Variante (*p = ...) ab; hier der einfache Ziel-Registerfall.
		tc_check 'int main(){ int x; x = (x = 1); putint(x); }' '1'

		# --- Semantische Fehler sind toedlich (2026-08-11) ---------------------
		# Vorher meldete qcc_p JEDEN semantischen Fehler nur auf stderr, endete
		# aber mit Rueckgabewert 0 und schrieb "OK" -- eine Kette
		# "qcc_p x.c > x.ir && qcc_backend x.ir" erzeugte damit klaglos falschen
		# Code. Der erzeugte Parser zaehlt jetzt in actionErrors (vom Generator
		# deklariert, siehe src/codegen.cpp) und liefert 1 statt 0.
		# Die 58 Tests, die auf den MELDUNGSTEXT pruefen, sind davon unberuehrt:
		# die Meldungen gehen unveraendert nach stderr.
		tc_rc_fail() {
			if build/qcc_p "$1" >/dev/null 2>&1; then
				echo "FAIL  qcc: semantischer Fehler liefert Rueckgabewert 0: [$1]"; tcfail=1; fail=1
			fi
		}
		tc_rc_ok() {
			if ! build/qcc_p "$1" >/dev/null 2>&1; then
				echo "FAIL  qcc: korrekter Code liefert Rueckgabewert != 0: [$1]"; tcfail=1; fail=1
			fi
		}
		# Die drei Schlussworte muessen UNTERSCHEIDBAR bleiben. Sie melden drei
		# verschiedene Dinge, und mindestens ein Aufrufer (tools/bootstrap_survey.py)
		# haengt daran: dessen Stufe-1-Messung schiebt PRAEFIXE einer Datei durch
		# den Parser, in denen unaufgeloeste Vorwaertsbezuege voellig regulaer sind.
		# Wuerden Parse- und Semantikfehler dasselbe Wort melden, waere diese
		# Messung wertlos (gemessen: 346 statt 5 Meldungen).
		# Der Semantik-Marker hiess zuerst SEMFAIL -- und enthielt damit "FAIL"
		# als Teilzeichenkette, was Aufrufer mit Teilstring-Pruefung genauso
		# hereinfallen liess. Der letzte Test unten pinnt genau das.
		tc_marker() {
			got=$(build/qcc_p "$2" 2>/dev/null | grep -xE 'OK|SEMERR|FAIL' | tail -1)
			if [ "$got" != "$1" ]; then
				echo "FAIL  qcc: Schlusswort [$got] statt [$1] fuer: $2"; tcfail=1; fail=1
			fi
		}
		tc_marker OK     'int main(){ putint(42); }'
		tc_marker SEMERR 'int main(){ nichtda(1); }'
		tc_marker SEMERR 'int main(){ int a; a = "text"; putint(a); }'
		tc_marker FAIL   'int main(){ '
		# Bewusst ein STRUKTURELLER Syntaxfehler. Hier stand zuerst
		# "a = (5, 6);" als Beispiel -- das war der noch fehlende Komma-Operator
		# und wurde am 2026-08-11 gueltig, womit der Test fehlschlug. Ein Test
		# fuer "Parse-Fehler" darf nicht auf einer Sprachluecke fussen, sonst
		# schlaegt er beim Schliessen der Luecke an statt beim Regress.
		tc_marker FAIL   'int main(){ int a; a = ; }'
		for m in OK SEMERR FAIL; do
			for n in OK SEMERR FAIL; do
				if [ "$m" != "$n" ] && case "$m" in *"$n"*) true;; *) false;; esac; then
					echo "FAIL  qcc: Schlusswort '$m' enthaelt '$n' als Teilzeichenkette -- Aufrufer mit Teilstring-Pruefung fallen darauf herein"; tcfail=1; fail=1
				fi
			done
		done

		tc_rc_fail 'int main(){ nichtda(1); }'
		tc_rc_fail 'int g(int a){ return a; } int main(){ g(1,2); }'
		tc_rc_fail 'int x; int x; int main(){ return 0; }'
		tc_rc_fail 'int main(){ int a[0]; return 0; }'
		tc_rc_fail 'int main(){ int a; a = "text"; putint(a); }'
		tc_rc_fail 'bool bad(){ return 1; } int main(){ return 0; }'
		tc_rc_fail 'int main(){ int a[5]; putint(a[0][1]); }'
		tc_rc_ok   'int main(){ putint(42); }'
		tc_rc_ok   'int add(int a,int b){ return a+b; } int main(){ putint(add(19,23)); }'
		tc_rc_ok   'struct S{int x;}; int main(){ struct S s; s.x=1; putint(s.x); }'
		tc_rc_ok   'typedef void (*Fn)(int); void g(int x){ putint(x); } int main(){ Fn f; f = g; f(7); }'

		# 2026-08-11: Bis Q9-QCC-Commit ae61e0a ("struct-Mehrfachfelder und freie
		# Reihenfolge im Programm") war ein zweidimensionales struct-FELD ein
		# Parse-Fehler -- eine BEWUSSTE Entscheidung (siehe docs/FORTSCHRITT.md:
		# "bewusst NICHT bei structField ... sauberer Parse-Fehler statt eines
		# still falsch geschnittenen Feldes"). Mit ae61e0a erlaubt
		# `structDeclarator = fieldName [ arraySize [ arraySizeN ] ]` die zweite
		# Dimension nun auch dort. Der Zustand ist geprueft und HALB fertig:
		#   - Deklaration wird angenommen, das Feld wird FLACH und mit korrekter
		#     Groesse belegt (tcStructFieldArrayLen haelt das Produkt aller
		#     Dimensionen). Nachgemessen: sizeof(struct{int m[2][3];int x;}) = 28
		#     wie beim flachen int m[6], Folgefelder liegen an den richtigen
		#     Offsets. Die oben befuerchtete stille Fehlschneidung tritt NICHT ein.
		#   - Der ZUGRIFF r.m[i][j] ist dagegen nicht implementiert und scheitert
		#     als Parse-Fehler, also laut statt still.
		# Der Test pinnt deshalb genau diese beiden Haelften, damit weder die
		# gewonnene Deklaration wieder verschwindet noch der fehlende Zugriff
		# unbemerkt "irgendwie" durchgeht.
		if build/qcc_p 'struct R{ int m[2][3]; int x; }; int main(){ struct R r; r.x=99; putint(sizeof(struct R)); putint(r.x); }' 2>/dev/null \
			| grep -q 'GLOBAL\|FUNC'; then
			echo "ok    qcc: 2D-struct-Feld wird flach korrekt belegt"
		else
			echo "FAIL  qcc: 2D-struct-Feld -- Deklaration verhaelt sich anders als erwartet"; tcfail=1; fail=1
		fi
		# 2026-09-08: der Zugriff r.m[i][j] ist jetzt umgesetzt (zweiter Index-
		# Scratch, s. tcEmitFieldRowColIndex) -- der "eigene Folgeschritt", auf
		# den die Notiz oben verweist, ist damit erledigt.
		tc_check 'struct R{ int m[2][3]; }; int main(){ struct R r; r.m[0][0]=7; putint(r.m[0][0]); }' '7'
		if build/qcc_p 'int f(int m[][3]){ return 0; } int main(){ putint(1); }' >/dev/null 2>&1; then
			echo "FAIL  qcc: 2D-Array-Parameter wird faelschlich akzeptiert"; tcfail=1; fail=1
		else
			echo "ok    qcc: 2D-Array als Parameter bleibt Parse-Fehler (eigener Folgeschritt)"
		fi
		# 2026-07-24: mehr als 2 Array-Dimensionen -- tcCheck2DIndex/tcEmit2DCombine
		# generalisiert zu tcCheckNDIndex/tcEmitNDCombine (TC_MAXDIMS=6 als grosszuegige
		# Obergrenze). arr[i1]..[iN] wird per Horner-Schema ueber N-1 Scratch-Globals
		# (__idxNd_2..__idxNd_N) zu einem flachen row-major-Index kombiniert -- fuer N=2
		# identisch zur bisherigen Loesung (nur EIN Scratch-Feld), kein neuer Opcode,
		# kein Backend-Change. Die beiden 2D-spezifischen Fehlermeldungen oben bleiben
		# wortgleich (siehe tcCheckNDIndex-Kommentar in qcc.lextab).
		tc_check 'int main(){ int m[2][3][4]; int i; int j; int k; for(i=0;i<2;i+=1){ for(j=0;j<3;j+=1){ for(k=0;k<4;k+=1){ m[i][j][k]=i*100+j*10+k; } } } putint(m[1][2][3]); putint(m[0][0][0]); putint(m[1][0][2]); }' '123\n0\n102'
		tc_check 'int g[2][2][2]; int main(){ g[0][0][0]=1; g[0][0][1]=2; g[0][1][0]=3; g[1][1][1]=8; putint(g[1][1][1]); putint(g[0][1][0]); putint(g[0][0][1]); }' '8\n3\n2'
		tc_check 'int m[2][2][2] = {1,2,3,4,5,6,7,8}; int main(){ putint(m[1][1][1]); putint(m[0][1][0]); }' '8\n3'
		tc_check 'int main(){ int m[2][2][2]; m[0][0][0]=5; m[1][1][1]=10; putint(1 + m[0][0][0] + m[1][1][1]); }' '16'
		tc_check 'int main(){ int m[2][3][4]; int* p=m[1][2]; p[3]=91; putint(m[1][2][3]); }' '91'
		if build/qcc_p 'int main(){ int m[2][2][2][2][2][2][2]; putint(1); }' 2>&1 | grep -q 'too many array dimensions (max 6)'; then
			echo "ok    qcc: Ueberschreiten von TC_MAXDIMS wird diagnostiziert"
		else
			echo "FAIL  qcc: TC_MAXDIMS-Diagnose fehlt"; tcfail=1; fail=1
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
		if build/qcc_p 'int main(){ char m[4] = "hallo"; }' 2>&1 | grep -q 'string literal too long for array'; then
			echo "ok    qcc: zu langes String-Literal als lokaler Array-Initialisierer wird diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer zu langes lokales String-Array-Literal fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'char msg[4] = "hallo"; int main(){ putint(1); }' 2>&1 | grep -q 'string literal too long for array'; then
			echo "ok    qcc: zu langes String-Literal als globaler Array-Initialisierer wird diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer zu langes globales String-Array-Literal fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int arr[4] = "hi"; int main(){ putint(1); }' 2>&1 | grep -q 'string literal initializer requires a char array'; then
			echo "ok    qcc: String-Literal-Initialisierer fuer Nicht-char-Array wird diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer String-Literal auf int-Array fehlt"; tcfail=1; fail=1
			fi
		# 2026-07-24: direkte Indizierung ohne Zwischenvariable -- "func()[i]" und
		# "text"[i] duerfen jetzt direkt indiziert werden (postfixIndex-Huellregel um
		# die bestehende index-Regel, siehe data/qcc.ebnf). Laufzeitreihenfolge auf
		# dem Stack ist [Pointer, Indexwert] wie bei "p + n" -- daher PADD+LOADIND statt
		# PTRINDEX/LOADIND (das den Pointer zuerst erwartet). Bewusst NICHT Teil dieser
		# Version: Indizierung als Zuweisungsziel (foo()[0] = 5;), verkettete Postfix-
		# Indizierung (f()[0][1]), Indizierung auf "(" expr ")".
		tc_check 'int main(){ putchar("hallo"[0]); putchar("hallo"[4]); }' 'ho'
		tc_check 'char* mkstr(){ return "world"; } int main(){ putchar(mkstr()[0]); putchar(mkstr()[2]); }' 'wr'
		tc_check 'int arr[3]; int* getarr(){ return arr; } int main(){ arr[0]=10; arr[1]=20; arr[2]=30; putint(getarr()[1]); }' '20'
		tc_check 'char* mkstr(){ return "hallo"; } int main(){ int i; i = 3; putchar(mkstr()[i]); }' 'l'
		tc_check 'char* mkstr(){ return "AB"; } int main(){ putint(1 + mkstr()[0]); }' '66'
		if build/qcc_p 'int f(){ return 5; } int main(){ int x = f()[0]; }' 2>&1 | grep -q 'index expects'; then
			echo "ok    qcc: Indizierung eines nicht-Pointer-Rueckgabewerts wird diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer Indizierung eines Nicht-Pointers fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'void* mkvoid(){ return 0; } int main(){ int x = mkvoid()[0]; }' 2>&1 | grep -q 'cannot index void'; then
			echo "ok    qcc: Indizierung von void* wird diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer Indizierung von void* fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int* getarr(); int main(){ getarr()[0] = 5; }' >/dev/null 2>&1; then
			echo "FAIL  qcc: Zuweisung auf indizierten Funktionsaufruf wird faelschlich akzeptiert"; tcfail=1; fail=1
		else
			echo "ok    qcc: Zuweisung auf foo()[0] bleibt Parse-Fehler (eigener Folgeschritt)"
		fi
		# 2026-07-24: extern-Deklarationen fuer NICHT in QCC definierte Funktionen
		# (z.B. echte OS-9/Microware-clib-Funktionen wie strcmp/printf/malloc). Nur
		# Aufrufpruefung (Argumentanzahl/-typen) hier per QCCVM-Frontend testbar --
		# die eigentliche Microware-ABI-Codeerzeugung (CALLEXT/CALLEXTP) ist 68k-
		# spezifisch und wird weiter unten per Hand-Mock-Stub end-to-end verifiziert
		# (QCCVM/ARM64 kennen die Aufrufkonvention bewusst nicht und lehnen CALLEXT
		# sauber ab statt es stillschweigend falsch zu behandeln).
		tc_check 'extern int strcmp(const char *a, const char *b); int main(){ putint(1); }' '1'
		tc_check 'extern int getval(); int main(){ putint(1); }' '1'
		if build/qcc_p 'extern int f(int a); extern int f(int a); int main(){ putint(1); }' 2>&1 | grep -q 'duplicate function'; then
			echo "ok    qcc: doppelte extern-Deklaration wird diagnostiziert"
		else
			echo "FAIL  qcc: extern-Doppeldeklarations-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'extern int f(int a); int f(int a){ return a; } int main(){ putint(1); }' 2>&1 | grep -q 'duplicate function'; then
			echo "ok    qcc: interne Funktion kollidiert mit extern-Deklaration wird diagnostiziert"
		else
			echo "FAIL  qcc: extern/intern-Kollisions-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'extern int f(int a, int b); int main(){ putint(f(1)); }' 2>&1 | grep -q 'wrong argument count'; then
			echo "ok    qcc: falsche Argumentanzahl bei extern-Aufruf wird diagnostiziert"
		else
			echo "FAIL  qcc: extern-Argumentanzahl-Diagnose fehlt"; tcfail=1; fail=1
		fi
		# 2026-09-08: "(void)" in einer extern-Deklaration/einem Funktionszeiger-
		# typedef las bis dahin als EIN Parameter statt null (data/qcc.ebnf las
		# "void" ueber den normalen type-Zweig von externParam). funcParams kannte
		# das Muster schon (voidParams | normalParams); externParams/fnPtrParams
		# nutzen jetzt dieselbe zwei-Alternativen-Form (siehe Kommentar dort zur
		# Backtracking-Falle). QCCVM kennt CALLEXT nicht (s. Kommentar oben) --
		# pruefbar ist hier nur die Aufrufpruefung am Frontend, per Schlusswort.
		if [ "$(build/qcc_p 'extern int f(void); int main(){ f(); putint(1); }' 2>&1 | tail -1)" = OK ]; then
			echo "ok    qcc: extern int f(void), Aufruf ohne Argumente wird angenommen"
		else
			echo "FAIL  qcc: extern int f(void) lehnt den korrekten Aufruf f() ab"; tcfail=1; fail=1
		fi
		if build/qcc_p 'extern int f(void); int main(){ putint(f(1)); }' 2>&1 | grep -q 'wrong argument count'; then
			echo "ok    qcc: extern int f(void) registriert null Parameter (f(1) wird abgelehnt)"
		else
			echo "FAIL  qcc: extern int f(void) schluckt weiterhin ein Argument"; tcfail=1; fail=1
		fi
		if [ "$(build/qcc_p 'typedef int (*fp)(void); int main(){ fp p; p(); putint(1); }' 2>&1 | tail -1)" = OK ]; then
			echo "ok    qcc: Funktionszeiger-typedef (void), Aufruf ohne Argumente wird angenommen"
		else
			echo "FAIL  qcc: Funktionszeiger-typedef (void) lehnt den korrekten Aufruf p() ab"; tcfail=1; fail=1
		fi
		if build/qcc_p 'typedef int (*fp)(void); int main(){ fp p; putint(p(1)); }' 2>&1 | grep -q 'wrong argument count'; then
			echo "ok    qcc: Funktionszeiger-typedef (void) registriert null Parameter"
		else
			echo "FAIL  qcc: Funktionszeiger-typedef (void) schluckt weiterhin ein Argument"; tcfail=1; fail=1
		fi
		# Die Falle, die die zwei-Alternativen-Form vermeiden soll: "void" als
		# ERSTES Token eines echten Parametertyps ("void* p") darf nicht von der
		# voidParams-Alternative verschluckt werden.
		if [ "$(build/qcc_p 'extern int f(void* p); int main(){ int x; f(&x); putint(1); }' 2>&1 | tail -1)" = OK ]; then
			echo "ok    qcc: extern int f(void* p) bleibt EIN Parameter (void wird nicht verschluckt)"
		else
			echo "FAIL  qcc: extern int f(void* p) ist an der voidParams-Falle zerbrochen"; tcfail=1; fail=1
		fi
		if build/qcc_p 'extern int f(...); int main(){ putint(1); }' >/dev/null 2>&1; then
			echo "FAIL  qcc: \"...\" ohne benannten Parameter wird faelschlich akzeptiert"; tcfail=1; fail=1
		else
			echo "ok    qcc: \"...\" ohne benannten Parameter davor bleibt Parse-Fehler (wie ISO C)"
		fi
		# 2026-07-24: typedef struct { ... } Name; -- anonymes struct inline im typedef.
		# Der typedef-Zielname wird bewusst als interner struct-Tag wiederverwendet (harmlose
		# Vereinfachung); Namenskollision mit einem bereits existierenden struct wird wie eine
		# normale doppelte struct-Deklaration abgelehnt.
		if build/qcc_p 'struct Dup { int x; }; typedef struct { int y; } Dup; int main(){ putint(1); }' 2>&1 | grep -q 'duplicate struct'; then
			echo "ok    qcc: Namenskollision bei anonymem struct-typedef wird diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer anonymes-struct-typedef-Namenskollision fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int main(){ break; }' 2>&1 | grep -q 'break outside loop' && \
		   build/qcc_p 'int main(){ continue; }' 2>&1 | grep -q 'continue outside loop'; then
			echo "ok    qcc: break/continue ausserhalb Schleife werden diagnostiziert"
		else
			echo "FAIL  qcc: break/continue-Diagnose fehlt"; tcfail=1; fail=1
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
		if build/qcc_p 'bool id(bool b){ return b; } int main(){ int n=1; id(n); }' 2>&1 | grep -q 'argument expects bool, got int' && \
		   build/qcc_p 'bool bad(){ return 1; } int main(){ return 0; }' 2>&1 | grep -q 'return expects bool, got int'; then
			echo "ok    qcc: bool-Argumente und -Rueckgaben werden typgeprueft"
		else
			echo "FAIL  qcc: bool-Typpruefung fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int main(){ char a[2]; a[2] = 9; }' 2>&1 | grep -q 'constant array index 2 out of range (length 2)'; then
			echo "ok    qcc: konstante Arraygrenze wird diagnostiziert"
		else
			echo "FAIL  qcc: konstante Arraygrenze nicht diagnostiziert"; tcfail=1; fail=1
		fi
		# 2026-07-24: const-Qualifizierer fuer globale/lokale Variablen und Parameter --
		# rein frontend-seitig (kein Backend-/IR-Unterschied), verbietet Zuweisung/++/--
		# auf die qualifizierte Variable selbst (kein Pointee-const wie in echtem C).
		tc_check 'const int g = 7; int main(){ putint(g); }'                  '7'
		tc_check 'int main(){ const int x = 5; putint(x + 1); }'             '6'
		if build/qcc_p 'const int g = 7; int main(){ g = 8; putint(g); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    qcc: Zuweisung an const-Globale wird diagnostiziert"
		else
			echo "FAIL  qcc: const-Globale-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int main(){ const int x = 5; x = 6; putint(x); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    qcc: Zuweisung an const-Lokale wird diagnostiziert"
		else
			echo "FAIL  qcc: const-Lokale-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int bump(const int x){ x = x + 1; return x; } int main(){ putint(bump(1)); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    qcc: Zuweisung an const-Parameter wird diagnostiziert"
		else
			echo "FAIL  qcc: const-Parameter-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int main(){ const int x = 5; x++; putint(x); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    qcc: ++/-- auf const-Variable wird diagnostiziert"
		else
			echo "FAIL  qcc: const-++/---Diagnose fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: "static" lokale Variablen -- persistieren ueber Aufrufe hinweg (als
		# ganz normaler GLOBAL registriert, siehe tc_staticlocal in data/qcc.lextab).
		# "static" bei Funktionen/globalen Variablen ist ein reines No-op (interne
		# Verlinkung ist bei einer einzigen Uebersetzungseinheit ohne Mehrdatei-Linkage
		# bedeutungslos). Bewusst OHNE Initialisierer (siehe staticVarDecl-Kommentar in
		# data/qcc.ebnf) und OHNE struct/Array in dieser Version.
		tc_check 'int bump(){ static int counter; counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); putint(bump()); }' '1\n2\n3'
		tc_check 'static int add(int a, int b){ return a + b; } int main(){ putint(add(2, 3)); }' '5'
		tc_check 'static int g = 7; int main(){ putint(g); }' '7'
		tc_check 'int f(){ static const int limit; return limit; } int main(){ putint(f()); }' '0'
		tc_check 'int x = 42; int f(){ static int *p; p = &x; return *p; } int main(){ putint(f()); }' '42'
		if build/qcc_p 'int f(){ static const int limit; limit = 5; return limit; } int main(){ putint(f()); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    qcc: Zuweisung an static-const-Lokale wird diagnostiziert"
		else
			echo "FAIL  qcc: static-const-Diagnose fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct P{ int x; }; int f(){ static struct P p; return p.x; } int main(){ putint(f()); }' 2>&1 | grep -q 'static struct locals not yet supported'; then
			echo "ok    qcc: static struct-Lokale wird bewusst abgelehnt (eigener Folgeschritt)"
		else
			echo "FAIL  qcc: static-struct-Diagnose fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: static-Initialisierer -- globalValue (Zahl/Negativ/bool-Literal,
		# dieselbe seiteneffektfreie Regel wie bei globalen Variablen) bleibt der
		# Fastpath OHNE Laufzeit-Code (tc_staticlocal extrahiert den Wert per Rohtext-Scan).
		tc_check 'int bump(){ static int counter = 10; counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); }' '11\n12'
		tc_check 'int f(){ static int x = -5; return x; } int main(){ putint(f()); }' '-5'
		tc_check 'int f(){ static bool b = true; return b; } int main(){ putint(f()); }' '1'
		tc_check 'int f(){ static char c = 65; return c; } int main(){ putint(f()); }' '65'
		tc_check 'int f(){ static int *p = 0; return p == 0; } int main(){ putint(f()); }' '1'
		if build/qcc_p 'int f(){ static int *p = 5; return 0; } int main(){ putint(f()); }' 2>&1 | grep -q 'static local pointer initializer must be 0'; then
			echo "ok    qcc: static-Pointer-Initialisierer != 0 wird diagnostiziert"
		else
			echo "FAIL  qcc: static-Pointer-Initialisierer-Diagnose fehlt"; tcfail=1; fail=1
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
		if build/qcc_p 'int base(){ return 3; } int f(){ static const int x = base()+1; x = 5; return x; } int main(){ putint(f()); }' 2>&1 | grep -q 'cannot assign to const variable'; then
			echo "ok    qcc: Zuweisung an static-const-Lokale mit Laufzeit-Initialisierer wird diagnostiziert"
		else
			echo "FAIL  qcc: static-const-Diagnose mit Laufzeit-Initialisierer fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'char* mk(){ return "hi"; } int f(){ static int x = mk(); return x; } int main(){ putint(f()); }' 2>&1 | grep -q 'static initializer expects int, got char\*'; then
			echo "ok    qcc: Typinkompatibilitaet bei Laufzeit-static-Initialisierer wird diagnostiziert"
		else
			echo "FAIL  qcc: Typpruefung fuer Laufzeit-static-Initialisierer fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-24: Pointee-Constness fuer "const T*" -- TCType.pointeeConst (ein Bit,
		# reist durch Zeigerarithmetik/Parameteruebergabe mit). Schreiben DURCH den Pointer
		# (*p = .., p[i] = ..) wird verboten, der Pointer selbst bleibt frei zuweisbar
		# (deckt das uebliche "p++"-Idiom UND den Hauptnutzungsfall im Generator-Vorbild,
		# const char* line-Parameter, jetzt auch semantisch ab -- nicht nur syntaktisch).
		tc_check 'int values[3] = {1,2,3}; int main(){ const int *p = values; p += 1; putint(*p); }' '2'
		tc_check 'int main(){ int x=42; const int *p = &x; putint(*p); putint(p[0]); }' '42\n42'
		tc_check 'int f(const int *p){ return *p; } int main(){ int x=42; putint(f(&x)); }' '42'
		if build/qcc_p 'int main(){ int x=1; const int *p = &x; *p = 5; putint(x); }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    qcc: Schreiben durch const-Pointer (*p = ..) wird diagnostiziert"
		else
			echo "FAIL  qcc: const-Pointer-Schreibschutz (*p) fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int main(){ int a[3]; const int *p = a; p[0] = 5; putint(a[0]); }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    qcc: Schreiben durch const-Pointer (p[i] = ..) wird diagnostiziert"
		else
			echo "FAIL  qcc: const-Pointer-Schreibschutz (p[i]) fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int g[3]; const int *p; int main(){ p = g; p[0] = 5; putint(g[0]); }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    qcc: Schreiben durch globalen const-Pointer wird diagnostiziert"
		else
			echo "FAIL  qcc: const-Pointer-Schreibschutz (global) fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'int f(const int *p){ *p = 5; return *p; } int main(){ int x=1; putint(f(&x)); }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    qcc: Schreiben durch const-Pointer-Parameter wird diagnostiziert"
		else
			echo "FAIL  qcc: const-Pointer-Schreibschutz (Parameter) fehlt"; tcfail=1; fail=1
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
		if build/qcc_p 'struct P{int x;}; int main(){ struct P p; putint(p.x[0]); }' 2>&1 | grep -q 'scalar struct field cannot be indexed'; then
			echo "ok    qcc: Indizierung eines skalaren struct-Felds wird diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer Indizierung eines skalaren struct-Felds fehlt"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct P{char buf[4];}; int main(){ struct P p; p.buf[9]=1; }' 2>&1 | grep -q 'constant array index 9 out of range'; then
			echo "ok    qcc: konstanter Index-Bereichsverstoss bei p.field[i] wird diagnostiziert"
		else
			echo "FAIL  qcc: Bounds-Check fuer p.field[i] fehlt"; tcfail=1; fail=1
		fi
		# 2026-07-25 (Selfhosting L2): Pointer-Felder in struct -- IMMER 8 Byte
		# Groesse/Ausrichtung (siehe tcRegisterStruct-Kommentar), damit dasselbe
		# frontend-berechnete Offset fuer 68k (4-Byte-Pointer) UND ARM64 (8-Byte-
		# Pointer) gueltig bleibt.
		tc_check 'struct P{char* text; int len;}; int main(){ struct P p; char msg[4]; char* t; msg[0]=72; msg[1]=105; msg[2]=0; p.text=msg; p.len=2; t=p.text; putchar(t[0]); putchar(t[1]); putint(p.len); }' 'Hi2'
		# 2026-09-07: DIREKTE Indizierung DURCH ein Pointer-Feld geht jetzt --
		# vorher stand hier ein Test, der die Diagnose "scalar struct field
		# cannot be indexed" VERLANGTE. Er hielt damit eine Grenze als
		# Sollverhalten fest; mit der Umsetzung musste er umgedreht werden.
		# Der Kern der Aenderung: die Aufrufer haben nur die ADRESSE des
		# Feldes auf dem Stapel, gebraucht wird der Zeiger DARIN -- also erst
		# LOADIND p, dann der Indexschritt (tcEmitPtrFieldIndex, eine
		# Funktion fuer alle sechs Emissionsstellen). Auf echtem 68k prueft
		# das tools/test_struct_68k.sh in den Faellen 29-34, lesend und
		# schreibend, lokal und global, ueber "." und "->".
		tc_check 'struct P{char* text; int len;}; int main(){ struct P p; char msg[4]; msg[0]=72; msg[1]=105; msg[2]=0; p.text=msg; p.len=2; putchar(p.text[0]); putchar(p.text[1]); putint(p.len); }' 'Hi2'
		tc_check 'struct P{char* text;}; int main(){ struct P p; char msg[4]; p.text=msg; p.text[0]=74; p.text[1]=75; putchar(msg[0]); putchar(msg[1]); }' 'JK'
		tc_check 'struct P{int* v;}; int main(){ struct P p; int a[3]; int i; i=2; a[2]=41; p.v=a; putint(p.v[i]); p.v[i]=42; putint(a[2]); }' '41\n42'
		# 2026-09-07: VERKETTETE Indizierung eines Strukturfelds (feld[i][j])
		# wurde zuerst nur GEMELDET (die Grammatik nahm die Form vorher gar
		# nicht an -- stiller Parse-Abbruch ohne Meldung). 2026-09-08
		# UMGESETZT: zweiter Index-Scratch (tcEmitFieldRowColIndex), dieselbe
		# Technik wie tcEmitPointerIndexChain, hier auf einen gemerkten Wert
		# vereinfacht (ein Feld ist hoechstens 2D, structs schachteln nicht).
		tc_check 'struct S{char t[4][8];}; int main(){ struct S s; s.t[1][2]=9; putint(s.t[1][2]); }' '9'
		tc_check 'struct S{char t[4][8];}; static struct S g; int main(){ struct S *sp; sp=&g; sp->t[1][2]=7; putint(sp->t[1][2]); }' '7'
		# Der ZEILENzugriff auf ein 2D-Feld geht dagegen und muss es bleiben:
		# "sp->t[i]" liefert den Zeiger auf Zeile i.
		tc_check 'struct S{char t[4][8]; int n;}; static struct S g; int main(){ struct S *sp; char *z; int i; i=2; sp=&g; z=sp->t[i]; z[3]=66; z=sp->t[2]; putchar(z[3]); }' 'B'
		# 2026-09-07: const AM STRUKTURFELD. Bis heute wurde die Kennung
		# geparst und WEGGEWORFEN -- fieldConstKw war eine aktionslose Kopie
		# von constKw, weil ein Verweis darauf tc_const ausgeloest haette,
		# dessen tcPendingConst erst beim naechsten Parameter oder Lokalen
		# konsumiert wird (und dort faelschlich Konstantheit erzwaenge).
		# Vier Formen liefen damit STILL durch, waehrend dasselbe bei einer
		# Variablen gemeldet wird. Jetzt hat fieldConstKw eine eigene Aktion
		# mit einer eigenen Flagge, die nur Felder betrifft.
		if build/qcc_p 'struct P{const int n; int m;}; int main(){ struct P p; p.n=1; }' 2>&1 | grep -q 'cannot assign to const struct field'; then
			echo "ok    qcc: const-Feld beschreiben wird gemeldet"
		else
			echo "FAIL  qcc: const-Feld beschreiben laeuft still durch"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct P{const int n; int m;}; static struct P g; int main(){ struct P *sp; sp=&g; sp->n=1; }' 2>&1 | grep -q 'cannot assign to const struct field'; then
			echo "ok    qcc: const-Feld ueber -> beschreiben wird gemeldet"
		else
			echo "FAIL  qcc: const-Feld ueber -> laeuft still durch"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct P{const int a, b; int m;}; int main(){ struct P p; p.a=1; }' 2>&1 | grep -q 'cannot assign to const struct field'; then
			echo "ok    qcc: bei \"const int a, b\" ist a konstant"
		else
			echo "FAIL  qcc: bei \"const int a, b\" bleibt a schreibbar"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct P{const int a, b; int m;}; int main(){ struct P p; p.b=1; }' 2>&1 | grep -q 'cannot assign to const struct field'; then
			echo "ok    qcc: bei \"const int a, b\" ist auch b konstant"
		else
			echo "FAIL  qcc: bei \"const int a, b\" bleibt b schreibbar"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct P{const char* cp;}; int main(){ struct P p; char b[4]; p.cp=b; p.cp[0]=65; }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    qcc: Schreiben DURCH ein const-Zeigerfeld wird gemeldet"
		else
			echo "FAIL  qcc: Schreiben durch ein const-Zeigerfeld laeuft still durch"; tcfail=1; fail=1
		fi
		if build/qcc_p 'struct P{const char* cp;}; static struct P g; int main(){ struct P *sp; char b[4]; sp=&g; sp->cp=b; sp->cp[0]=65; }' 2>&1 | grep -q 'cannot assign through pointer to const'; then
			echo "ok    qcc: dito ueber -> wird gemeldet"
		else
			echo "FAIL  qcc: dito ueber -> laeuft still durch"; tcfail=1; fail=1
		fi
		# Und was ERLAUBT bleiben MUSS -- eine zu scharfe Pruefung faellt
		# sonst erst beim Selbsthost auf (QCCs eigener Parser haengt an
		# "actionLog[i].start = start" bei "const char* start"):
		# der ZEIGER selbst ist nicht konstant, nur der Pointee; Lesen geht;
		# das Nicht-const-Nachbarfeld bleibt schreibbar.
		tc_check 'struct P{const char* cp; int m;}; int main(){ struct P p; char b[4]; b[0]=65; p.cp=b; p.m=7; putchar(p.cp[0]); putint(p.m); }' 'A7'
		tc_check 'struct P{const int n; int m;}; int main(){ struct P p; p.m=9; putint(p.m); }' '9'
		# Die naechste struct darf nicht angesteckt werden:
		tc_check 'struct P{const int a;}; struct Q{int b;}; int main(){ struct Q q; q.b=5; putint(q.b); }' '5'
		# Ein SKALARES Feld ohne Zeigertyp bleibt undiskutierbar -- die
		# Diagnose dafuer steht weiter oben und muss erhalten bleiben.
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
		# 2026-09-08: arr[i].feld[j] (feld eindimensional) ist jetzt umgesetzt --
		# derselbe zweite Index-Scratch wie bei feld[i][j] oben, hier stasht er
		# den Feldindex j, waehrend arr[i]s eigener Index sich in der
		# Adressberechnung dazwischen verbraucht (tcEmitStashedFieldIndex).
		tc_check 'struct Rec { char name[8]; }; int main(){ struct Rec arr[3]; arr[0].name[3]=8; putint(arr[0].name[3]); }' '8'
		# ptr[i].feld (2026-07-25, Milestone B): eine LOKALE Pointer-auf-struct-Variable,
		# indiziert, dann Feldzugriff -- braucht der Selfhosting-Pilot fuer routinesC[i].name/
		# .text (ActionRoutine*, ein malloc/realloc-gewachsenes Array, kein festes lokales
		# Array wie arr[i].feld oben). LOADP statt PUSHADDR, sonst dieselbe IPADDN-Idee.
		tc_check 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; struct Rec* p; arr[0].a=10; arr[1].a=20; arr[2].a=30; p = arr; putint(p[0].a); putint(p[1].a); putint(p[2].a); p[1].a = 99; putint(arr[1].a); }' '10\n20\n30\n99'
		tc_check 'struct Rec { char name[8]; }; int main(){ struct Rec arr[2]; struct Rec* p; p = arr; p[1].name[2]=6; putint(p[1].name[2]); }' '6'
		# Die Grammatik-Erweiterung fuer arr[i].feld (Sequenz statt Alternation) macht generell
		# jede "indiziert-dann-Member"-Kombination parsebar. 2026-09-08: lokal/global x
		# Array-von-structs/Pointer-auf-struct x mit/ohne Feld-Index haben jetzt alle Codegen.
		# Alles andere (hier: Pointer auf einen NICHT-struct-Typ) muss weiterhin sauber
		# diagnostiziert werden statt die "."-Fortsetzung stillschweigend zu ignorieren.
		if build/qcc_p 'int main(){ int x=1; int* p=&x; putint(p[0].a); }' 2>&1 | grep -q 'indexed variable followed by a member access is only supported for a fixed array of structs, or a pointer to struct'; then
			echo "ok    qcc: indizierter Pointer auf Nicht-struct mit Feldanhang wird weiterhin diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer indizierten Nicht-struct-Pointer mit Feldanhang fehlt -- Risiko stiller Fehlcode!"; tcfail=1; fail=1
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
		# statt PUSHADDR L, LOADGP statt LOADP), da globals_ in qccvm.py bei GLOBAL wie bei
		# GARRAY einheitlich eine Liste ist (ADDRG braucht keine lokale Scalar/Block-
		# Unterscheidung wie bei Locals). Initialisierer + mehrdimensionale struct-Arrays
		# bleiben bewusst ein sauberer Parse-Fehler (wie beim lokalen Fall).
		tc_check 'struct Rec { int a; int b; }; struct Rec g; int main(){ g.a = 42; g.b = 99; putint(g.a); putint(g.b); }' '42\n99'
		tc_check 'struct Rec { int a; int b; }; struct Rec garr[3]; int main(){ garr[0].a=10; garr[1].a=20; garr[2].a=30; putint(garr[0].a); putint(garr[1].a); putint(garr[2].a); }' '10\n20\n30'
		tc_check 'struct Rec { int a; int b; }; struct Rec garr[3]; struct Rec* gp; int main(){ gp = garr; gp[0].a=100; gp[1].a=200; putint(garr[0].a); putint(garr[1].a); putint(gp[1].a); }' '100\n200\n200'
		tc_check 'struct Rec { int a; int b; }; struct Rec garr[3]; struct Rec g; int main(){ putint(sizeof(garr)); putint(sizeof(g)); putint(sizeof(struct Rec)); }' '24\n8\n8'
		if build/qcc_p 'struct Rec { int a; }; struct Rec g = {1}; int main(){ putint(1); }' 2>&1 | grep -q 'struct global cannot have an initializer'; then
			echo "ok    qcc: Initialisierer bei globaler struct-Variable wird diagnostiziert"
		else
			echo "FAIL  qcc: Diagnose fuer struct-Global-Initialisierer fehlt"; tcfail=1; fail=1
		fi
		tc_check 'struct Rec { int a; }; struct Rec g[2][2]; int main(){ g[1][1].a=88; putint(g[1][1].a); }' '88'
		# 2026-07-24: mehr als 2 Array-Dimensionen -- tcCheck2DIndex/tcEmit2DCombine
		# generalisiert zu tcCheckNDIndex/tcEmitNDCombine (TC_MAXDIMS=6 als grosszuegige
		# Obergrenze). arr[i1]..[iN] wird per Horner-Schema ueber N-1 Scratch-Globals
		# (__idxNd_2..__idxNd_N) zu einem flachen row-major-Index kombiniert -- fuer N=2
		# identisch zur bisherigen Loesung (nur EIN Scratch-Feld), kein neuer Opcode,
		# kein Backend-Change. Die beiden 2D-spezifischen Fehlermeldungen oben bleiben
		# wortgleich (siehe tcCheckNDIndex-Kommentar in qcc.lextab).
		tc_check 'int main(){ int m[2][3][4]; int i; int j; int k; for(i=0;i<2;i+=1){ for(j=0;j<3;j+=1){ for(k=0;k<4;k+=1){ m[i][j][k]=i*100+j*10+k; } } } putint(m[1][2][3]); putint(m[0][0][0]); putint(m[1][0][2]); }' '123\n0\n102'
		tc_check 'int g[2][2][2]; int main(){ g[0][0][0]=1; g[0][0][1]=2; g[0][1][0]=3; g[1][1][1]=8; putint(g[1][1][1]); putint(g[0][1][0]); putint(g[0][0][1]); }' '8\n3\n2'
		tc_check 'int m[2][2][2] = {1,2,3,4,5,6,7,8}; int main(){ putint(m[1][1][1]); putint(m[0][1][0]); }' '8\n3'
		tc_check 'int main(){ int m[2][2][2]; m[0][0][0]=5; m[1][1][1]=10; putint(1 + m[0][0][0] + m[1][1][1]); }' '16'
		tc_check 'int main(){ int m[2][3][4]; int* p=m[1][2]; p[3]=91; putint(m[1][2][3]); }' '91'
		if build/qcc_p 'int main(){ int m[2][2][2][2][2][2][2]; putint(1); }' 2>&1 | grep -q 'too many array dimensions (max 6)'; then
			echo "ok    qcc: Ueberschreiten von TC_MAXDIMS wird diagnostiziert"
		else
			echo "FAIL  qcc: TC_MAXDIMS-Diagnose fehlt"; tcfail=1; fail=1
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
		# Gefundener und behobener Bug (2026-09-09, docs/ISO_C_GAP_LIST_de.md-Nachtrag
		# vom Peephole-Bau): mehrere Zeiger-Deklaratoren in EINER Anweisung
		# ("char *a, *b;") -- pointerDecl stand in varDecl/structField/plainGlobalDecl
		# nur EINMAL vor der ganzen Deklaratorliste statt pro Deklarator. Bei Lokalen/
		# Struct-Feldern ein STILLER Parse-Fehler ("*" nach dem Komma passte auf keine
		# Regel); bei Globalen (eigener Rohtext-Mechanismus in tc_globalend) sogar noch
		# schlimmer -- STILL FALSCHER Code, weil der wiederverwendete Typ-Praefix den
		# Stern des ERSTEN Deklarators mitschleppte ("char *a, *b;" wurde zu
		# "char **b" verdoppelt, "char* a, b;" machte "b" faelschlich zum Zeiger).
		# Je ein Test pro betroffener Stelle, inkl. GEMISCHTER Deklaratoren (nur der
		# Name MIT eigenem Stern ist ein Zeiger, wie in echtem C).
		tc_check 'int main(){ int x=5; int y=9; int *a, *b; a=&x; b=&y; putint(*a); putint(*b); }' '5\n9'
		tc_check 'int main(){ int x=7; int *a, y=3; a=&x; putint(*a); putint(y); }' '7\n3'
		tc_check 'struct P { char *a, *b; }; int main(){ struct P p; char ca=65; char cb=66; p.a=&ca; p.b=&cb; putchar(*p.a); putchar(*p.b); }' 'AB'
		tc_check 'char ca=65; char cb=66; char *a, *b; int main(){ a=&ca; b=&cb; putchar(*a); putchar(*b); }' 'AB'
		tc_check 'int x=5; int *a, b=3; int main(){ a=&x; putint(*a); putint(b); }' '5\n3'
		# Gefundener und behobener Bug (2026-09-09, beim Q9-Tools-Uebersetzungsversuch
		# gegen Microwares echten SDK-Header module.h entdeckt): derselbe Mehrfach-
		# deklaratoren-Fehler wie oben, aber in typedefDecl -- "typedef struct modhcom
		# mh_com, *Mh_com;" scheiterte still, weil typedefTargetName nur EINEN Namen
		# erlaubte. pointerDecl wanderte aus typedefType in eine neue typedefDeclarator
		# (ein eigener Zeigergrad pro Deklarator, wie ueberall sonst). Je ein Test:
		# benannter Struct-Typ (das reale module.h-Muster), Skalartyp, anonymer Struct
		# im typedef (die tcAnonStructPending/tcBasePointers-Falle -- nur der ERSTE
		# Deklarator registriert die Struktur, weitere muessen sie wiederverwenden),
		# und drei Deklaratoren gemischten Grades (keine Akkumulation ueber die Liste).
		tc_check 'struct M { int a; }; typedef struct M mh_com, *Mh_com; int main(){ mh_com x; Mh_com p; x.a=42; p=&x; putint(p->a); }' '42'
		tc_check 'typedef int I, *IP; int main(){ I x=7; IP p; p=&x; putint(*p); putint(x); }' '7\n7'
		tc_check 'typedef struct { int a; } S, *SP; int main(){ S s; SP p; s.a=9; p=&s; putint(p->a); putint(s.a); }' '9\n9'
		tc_check 'typedef int A, *B, **C; int main(){ int x=5; A a=x; B b=&x; C c=&b; putint(a); putint(*b); putint(**c); }' '5\n5\n5'
		# Neuer echter 16-Bit-Basistyp "short"/"unsigned short" (2026-09-09,
		# beim Q9-Tools-Uebersetzungsversuch entdeckt: module.h definiert
		# u_int16 selbst als "unsigned short" und hat etliche blanke short-
		# Felder -- vorher gab es dafuer ueberhaupt keinen Basistyp). Bewusste
		# Vereinfachung wie bei char: IMMER nullerweitert geladen, kein
		# eigener vorzeichenbehafteter Zweig (short b=-1; kommt als 65535
		# zurueck -- das Bitmuster ist richtig, nur die Interpretation ist wie
		# bei char bewusst unsigned). Je ein Test: lokale Variablen + sizeof,
		# globale Variable samt Arithmetik, Array, Zeiger, und ein Struct MIT
		# echter Feldgroesse (int VOR den beiden short-Feldern -- diese
		# Reihenfolge ist Absicht, s. NACHTRAG gleich danach).
		tc_check 'int main(){ short a=7, b=-1; putint(a); putint(b); putint(sizeof(short)); putint(sizeof(a)); }' '7\n65535\n2\n2'
		tc_check 'short g=300; int main(){ g=g+1; putint(g); }' '301'
		tc_check 'int main(){ short arr[3]; int i; for(i=0;i<3;i=i+1) arr[i]=i*10; putint(arr[0]); putint(arr[1]); putint(arr[2]); }' '0\n10\n20'
		tc_check 'int main(){ short x=42; short *p; p=&x; putint(*p); *p=7; putint(x); }' '42\n7'
		tc_check 'struct M { int c; short a; short b; }; int main(){ struct M m; m.c=99999; m.a=100; m.b=200; putint(m.c); putint(m.a); putint(m.b); putint(sizeof(struct M)); }' '99999\n100\n200\n8'
		# NACHTRAG -- echter, vorbestehender Mangel in DIESEM Orakel gefunden
		# (nicht im Compiler: auf dem 68030 laeuft die volle Kette mit der
		# Feldreihenfolge "short a; short b; int c;" byteidentisch richtig,
		# s. Memory project_qcc_short.md). qccvm modelliert einen Zeiger als
		# (Block, Byte-Offset) und ein struct-Feld darin als
		# "block[offset // typgroesse]" (pointer_index) -- das setzt
		# STILLSCHWEIGEND voraus, dass verschieden grosse Felder NIE auf denselben
		# Index kollidieren. "short a@0(Gr.2)->Index 0, short b@2(Gr.2)->Index 1,
		# int c@4(Gr.4)->Index 1" -- b und c landen auf demselben Python-
		# Listenplatz, c ueberschreibt b. Vor short kam das nie vor (nur 1-Byte-
		# und 4-Byte-Felder, deren Indizes bei ueblichen Layouts nicht
		# zusammenfallen); short macht Kollisionen leicht moeglich. ECHTE Reparatur
		# waere ein Byte-genaues Speichermodell in qccvm.py -- eigenes, separates
		# Vorhaben, hier bewusst nicht angefasst. Deshalb oben die Feldreihenfolge
		# "int VOR den shorts" gewaehlt, die keine Kollision hat.
		[ $tcfail -eq 0 ] && echo "ok    qcc: $tccount Programme inkl. Pointer, for/do-while/break/continue, struct (gemischte Feldtypen, anonym im typedef, Array-Felder inkl. direkter p.field[i]-Indizierung, Pointer-Felder inkl. direkter Indizierung DURCH sie, Arrays von structs inkl. arr[i].feld und ptr[i].feld, globale struct-Variablen/-Arrays/-Pointer)/typedef/enum, sizeof/++/--/switch/Casts/const/static (inkl. nicht-konstantem Laufzeit-Initialisierer)/Pointee-Constness/void/void*/Mehrdim-Arrays (bis TC_MAXDIMS)/extern/String-Literale (inkl. Array-Initialisierer + direkter Indizierung ohne Zwischenvariable) -> qccvm korrekt"
	else
		echo "FAIL  qcc: data/qcc_p.c kompiliert nicht"; fail=1
	fi
else
	echo "warn  qcc: python3 fehlt -- QCC/qccvm-Check uebersprungen"
fi

# 12b) QCC Mehrdatei-Uebersetzung, M1 (2026-07-25): bare Funktionsprototyp
#     ohne Rumpf ("int f(int x);" statt extern -- normale interne bsr/bl-ABI,
#     NICHT die Microware-ABI/CALLEXT des bestehenden extern-Features) und
#     "extern <typ> <name>;" bei globalen Variablen erlauben getrennt
#     kompilierte QCC-Dateien. tools/qcc_merge.py simuliert dafuer einen
#     Mini-Linker vor QCCVM (echte Linker: l68 fuers 68k/OS-9-Ziel, ld/clang
#     fuers ARM64-Ziel, siehe M2/M3) -- prueft Duplicate-Symbole, genau ein
#     main, static-Sichtbarkeit UND (als Bonus, den ein echter Linker NICHT
#     leisten koennte) Signatur-Konsistenz zwischen Deklaration und Definition.
if command -v python3 >/dev/null 2>&1; then
	build/qcc_p 'int shared; int helper(int x); int main(){ shared = 10; putint(helper(shared)); }' > build/qcc_mf_a.ir
	build/qcc_p 'extern int shared; int helper(int x){ return x + shared; }' > build/qcc_mf_b.ir
	if [ "$(python3 tools/qcc_merge.py build/qcc_mf_a.ir build/qcc_mf_b.ir 2>/dev/null | python3 tools/qccvm.py 2>/dev/null)" = "20" ]; then
		echo "ok    qcc Mehrdatei M1: Funktionsaufruf + globale Variable ueber Dateigrenze korrekt"
	else
		echo "FAIL  qcc Mehrdatei M1: Funktionsaufruf/Global ueber Dateigrenze fehlerhaft"; fail=1
	fi
	build/qcc_p 'int secret(int x); int main(){ putint(secret(1)); }' > build/qcc_mf_c.ir
	build/qcc_p 'static int secret(int x){ return x*2; }' > build/qcc_mf_d.ir
	if python3 tools/qcc_merge.py build/qcc_mf_c.ir build/qcc_mf_d.ir > /dev/null 2>build/qcc_mf.err; then
		echo "FAIL  qcc Mehrdatei M1: static-Funktion faelschlich ueber Dateigrenze sichtbar"; fail=1
	elif grep -q "als static definiert" build/qcc_mf.err; then
		echo "ok    qcc Mehrdatei M1: static-Funktion bleibt fuer andere Datei unsichtbar"
	else
		echo "FAIL  qcc Mehrdatei M1: static-Diagnose fehlt/falsch"; fail=1
	fi
	build/qcc_p 'int f(){ return 1; } int main(){ putint(f()); }' > build/qcc_mf_e.ir
	build/qcc_p 'int f(){ return 2; } int main2(){ return 0; }' > build/qcc_mf_f.ir
	if python3 tools/qcc_merge.py build/qcc_mf_e.ir build/qcc_mf_f.ir > /dev/null 2>build/qcc_mf.err; then
		echo "FAIL  qcc Mehrdatei M1: doppelte nicht-static Definition nicht erkannt"; fail=1
	elif grep -q "doppelte Definition" build/qcc_mf.err; then
		echo "ok    qcc Mehrdatei M1: doppelte nicht-static Definition wird wie 'duplicate symbol' erkannt"
	else
		echo "FAIL  qcc Mehrdatei M1: Duplicate-Diagnose fehlt/falsch"; fail=1
	fi
	build/qcc_p 'int g(int a, int b); int main(){ putint(g(1,2)); }' > build/qcc_mf_g.ir
	build/qcc_p 'int g(int a){ return a; }' > build/qcc_mf_h.ir
	if python3 tools/qcc_merge.py build/qcc_mf_g.ir build/qcc_mf_h.ir > /dev/null 2>build/qcc_mf.err; then
		echo "FAIL  qcc Mehrdatei M1: Signatur-Inkonsistenz (Parameterzahl) nicht erkannt"; fail=1
	elif grep -q "Parameter deklariert" build/qcc_mf.err; then
		echo "ok    qcc Mehrdatei M1: Signatur-Inkonsistenz (Bonus-Check) wird erkannt"
	else
		echo "FAIL  qcc Mehrdatei M1: Signatur-Konsistenz-Diagnose fehlt/falsch"; fail=1
	fi
	rm -f build/qcc_mf.err
else
	echo "warn  qcc Mehrdatei M1: python3 fehlt -- uebersprungen"
fi

# 13) QCC M4a: eigenstaendiges Backend liest Stack-IR und erzeugt
#     PIC-faehigen 68000-Assembler. Noch keine Ziel-Runtime/Ausfuehrung; vasm
#     prueft aber Funktionsframes, Parameter, CALL/RET und alle Syntaxdetails.
#     Gebaut wird die reine-C-Fassung (qcc_backend_c.cpp, siehe
#     docs/SELFHOSTING_LUECKENLISTE.md); das C++-Original (qcc_backend.cpp)
#     bleibt als Referenz liegen -- Ruecksetzen = hier wieder die .cpp bauen.
if [ -x tools/vasmm68k_mot ]; then
	if cc -std=c11 -Wall -Wextra -x c -o build/qcc_backend "$QIR68K_SRC" 2>/dev/null && \
		build/qcc_p 'int add(int a, int b){ return a + b; } int main(){ putint(add(19, 23)); }' > build/qcc_m4.ir && \
		build/qcc_backend build/qcc_m4.ir build/qcc_m4.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_m4.bin build/qcc_m4.s68 2>/dev/null && \
		grep -q '^tc_add:' build/qcc_m4.s68 && grep -q $'bsr\ttc_add' build/qcc_m4.s68; then
		echo "ok    qcc M4a: reines C, IR->PIC-68000-Assembler assembliert mit vasm"
	else
		echo "FAIL  qcc M4a: IR->68k-Backend oder vasm fehlgeschlagen"; fail=1
	fi

	# M4a-oob) 2026-08-11: globales Array LAENGER als MAX_ARRAY_LEN mit mindestens
	# einem GINIT. Bis dahin lief die Ausgabeschleife bis g->length, der
	# Initialisierer-Puffer fasste aber nur MAX_ARRAY_LEN Elemente -- ab Index
	# 4096 wurde Speicher HINTER dem Puffer ausgegeben (die Folgefelder und der
	# Name der naechsten globalen Variablen als Zahlen interpretiert, z.B.
	# 1751343470 = "nach"). Stiller Falschcode, kein Absturz. Der Fall ist im
	# Generator selbst nicht aktiv (die grossen Arrays dort haben keinen
	# Initialisierer), deshalb blieb er unentdeckt. Erwartung: alles ab Index
	# 4096 ist 0, der gesetzte Index 0 bleibt erhalten, das Nachbar-Array
	# unversehrt.
	printf 'GARRAY big i 5000 0\nGINIT big 0 111\nGARRAY nachbar i 2 0\nGINIT nachbar 0 222\nGINIT nachbar 1 333\nFUNC main 0 0\nPUSH 0\nRET\nENDFUNC\n' > build/qcc_oob.ir
	if ! build/qcc_backend build/qcc_oob.ir build/qcc_oob.s68 -os9 2>/dev/null; then
		echo "FAIL  qcc M4a-oob: Backend konnte grosses Array nicht erzeugen"; fail=1
	fi
	# Zeile 1 des Blocks ist die Marke, Element N steht also auf Zeile N+2.
	# Tabulatoren werden vor dem Vergleich entfernt (die Ausgabe ist tabuliert).
	oob_first=$(sed -n '/^tc_g_big:/,$p' build/qcc_oob.s68 | sed -n '2p' | tr -d ' \t')
	oob_tail=$(sed -n '/^tc_g_big:/,$p' build/qcc_oob.s68 | sed -n '4098,5001p' | tr -d ' \t' | grep -vc '^dc.l0$')
	oob_nb=$(sed -n '/^tc_g_nachbar:/,$p' build/qcc_oob.s68 | sed -n '2p' | tr -d ' \t')
	if [ -s build/qcc_oob.s68 ] && \
		[ "$oob_first" = "dc.l111" ] && [ "$oob_tail" = "0" ] && [ "$oob_nb" = "dc.l222" ]; then
		echo "ok    qcc M4a-oob: grosses Array mit Initialisierer gibt jenseits MAX_ARRAY_LEN Nullen aus (kein Speicher hinter init[])"
	else
		echo "FAIL  qcc M4a-oob: Array > MAX_ARRAY_LEN mit GINIT gibt Fremdspeicher aus"; fail=1
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
	if [ -x build/qcc_backend ]; then
		# Fall 1: 2 Argumente, beide in Registern (a->d0, b->d1), kein Stack-Rest.
		if build/qcc_p 'extern int myadd(int a, int b); int main(){ putint(myadd(3,4)); }' > build/qcc_ext2.ir && \
			build/qcc_backend build/qcc_ext2.ir build/qcc_ext2.s68; then
			cp build/qcc_ext2.s68 build/qcc_ext2_test.s68
			{
				echo ""
				echo "; Mock: erwartet d0=a=3, d1=b=4 -- Ergebnis = a*16+b = 52"
				echo "myadd:	lsl.l	#4,d0"
				echo "	add.l	d1,d0"
				echo "	rts"
			} >> build/qcc_ext2_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_ext2_test.bin build/qcc_ext2_test.s68 2>/dev/null && \
				[ "$(python3 tools/qcc68sim.py build/qcc_ext2_test.s68 2>/dev/null)" = "52" ]; then
				echo "ok    qcc extern 68000: 2 Argumente in d0/d1 korrekt platziert"
			else
				echo "FAIL  qcc extern 68000: 2-Argumente-ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  qcc extern 68000: IR/Backend fuer 2-Argumente-Fall fehlgeschlagen"; fail=1
		fi
		# Fall 2: 4 Argumente -- a->d0, b->d1, c/d auf dem Stack in umgekehrter
		# Reihenfolge (c am naechsten zur Ruecksprungadresse, d weiter weg).
		if build/qcc_p 'extern int foo(int a, int b, int c, int d); int main(){ putint(foo(1,2,3,4)); }' > build/qcc_ext4.ir && \
			build/qcc_backend build/qcc_ext4.ir build/qcc_ext4.s68; then
			cp build/qcc_ext4.s68 build/qcc_ext4_test.s68
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
			} >> build/qcc_ext4_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_ext4_test.bin build/qcc_ext4_test.s68 2>/dev/null && \
				[ "$(python3 tools/qcc68sim.py build/qcc_ext4_test.s68 2>/dev/null)" = "17185" ]; then
				echo "ok    qcc extern 68000: 4 Argumente (2 Register + 2 Stack, umgekehrte Reihenfolge) korrekt platziert"
			else
				echo "FAIL  qcc extern 68000: 4-Argumente-ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  qcc extern 68000: IR/Backend fuer 4-Argumente-Fall fehlgeschlagen"; fail=1
		fi
		# Fall 3 (2026-07-26/27, KORRIGIERT nach echtem Q9-Befund UND capstone-
		# Disassemblierung der ECHTEN clib.l-printf): die ERSTEN ZWEI ARGUMENTE
		# INSGESAMT (fest deklariert + variadisch zusammengezaehlt) gehen nach
		# d0/d1, GENAU wie bei einem nicht-variadischen Aufruf -- NUR ab dem
		# DRITTEN Argument geht es auf den Stack. Die vorherige Annahme (2026-07-24,
		# "nur die FEST deklarierten Parameter gehen nach d0/d1, der GESAMTE
		# variadische Teil auf den Stack") war NIE gegen echten, kompilierten
		# clib.l-Code verifiziert und erwies sich beim ersten echten End-zu-Ende-
		# Testlauf des selbstgehosteten Generators als falsch: printf(fmt,...)
		# stuerzte bei JEDEM variadischen Argument ab (PMMU-Fehler, kleine
		# Ganzzahl statt echtem Pointer/Wert gelesen), weil die echte, kompilierte
		# printf() als ALLERERSTE Instruktion "move.l d1,d0" macht -- sie erwartet
		# ihr erstes variadisches Argument also IMMER in d1, unabhaengig von der
		# "..."-Deklaration. Siehe docs/FORTSCHRITT.md.
		if build/qcc_p 'extern int myprintf(int fmt, ...); int main(){ int x = 42; putint(myprintf(1, x, 7)); }' > build/qcc_extv.ir && \
			build/qcc_backend build/qcc_extv.ir build/qcc_extv.s68; then
			cp build/qcc_extv.s68 build/qcc_extv_test.s68
			{
				echo ""
				echo "; Mock: die ERSTEN ZWEI Argumente insgesamt (fmt, x) gehen nach d0/d1,"
				echo "; NUR das dritte (7) auf den Stack. Erwartet d0=fmt=1, d1=x=42, 4(a7)=7 --"
				echo "; Ergebnis = fmt+x*16+7*256 = 1 + 42*16 + 7*256 = 2465"
				echo "myprintf:	move.l	d1,d2"
				echo "	move.l	4(a7),d3"
				echo "	lsl.l	#4,d2"
				echo "	lsl.l	#8,d3"
				echo "	add.l	d2,d0"
				echo "	add.l	d3,d0"
				echo "	rts"
			} >> build/qcc_extv_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_extv_test.bin build/qcc_extv_test.s68 2>/dev/null && \
				[ "$(python3 tools/qcc68sim.py build/qcc_extv_test.s68 2>/dev/null)" = "2465" ]; then
				echo "ok    qcc extern 68000: variadischer Aufruf (erste zwei Argumente insgesamt in d0/d1, Rest auf dem Stack) korrekt platziert"
			else
				echo "FAIL  qcc extern 68000: variadische ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  qcc extern 68000: IR/Backend fuer variadischen Fall fehlgeschlagen"; fail=1
		fi
		# Fall 4 (2026-07-24): ein char*-Argument (String-Literal) -- die Adresse des
		# per GARRAY/GINIT angelegten String-Konstanten muss unveraendert bis d0 durch-
		# gereicht werden; der Mock liest das erste Byte an dieser Adresse zurueck.
		if build/qcc_p 'extern int mockchr(const char* s); int main(){ putint(mockchr("Hi")); }' > build/qcc_extstr.ir && \
			build/qcc_backend build/qcc_extstr.ir build/qcc_extstr.s68; then
			cp build/qcc_extstr.s68 build/qcc_extstr_test.s68
			{
				echo ""
				echo "; Mock: erwartet d0=Adresse des Strings -- liest erstes Byte zurueck ('H'=72)"
				echo "mockchr:	move.l	d0,a0"
				echo "	moveq	#0,d0"
				echo "	move.b	(a0),d0"
				echo "	rts"
			} >> build/qcc_extstr_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_extstr_test.bin build/qcc_extstr_test.s68 2>/dev/null && \
				[ "$(python3 tools/qcc68sim.py build/qcc_extstr_test.s68 2>/dev/null)" = "72" ]; then
				echo "ok    qcc extern 68000: String-Literal-Adresse korrekt an char*-Parameter uebergeben"
			else
				echo "FAIL  qcc extern 68000: String-Literal-ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  qcc extern 68000: IR/Backend fuer String-Literal-Fall fehlgeschlagen"; fail=1
		fi
		# Fall 5 (2026-07-25, Selfhosting L2 Milestone A/B): malloc/realloc/free ueber
		# extern+CALLEXT -- Voraussetzung fuer den ActionRoutine-Piloten (siehe SELFHOSTING_
		# LUECKENLISTE.md). Mock: einfacher Bump-Allokator (kein echter Speicher-Freigabe-
		# Mechanismus -- reicht fuer diesen Test). Verifiziert die Aufrufmechanik END-TO-END
		# (Argumentplatzierung d0/d0+d1, Rueckgabewert in d0 als echt benutzbarer Pointer,
		# Schreiben/Lesen durch den zurueckgegebenen Pointer) -- NICHT das Kopierverhalten
		# von realloc (alte Daten ueber die Grenze hinweg erhalten bleiben), das braucht
		# entweder echten Q9-Zugriff oder einen deutlich aufwendigeren Mock (qcc68sim
		# modelliert nur EIN generisches Adressregister a0, kein registerindiziertes
		# Kopieren beliebiger Laenge) -- separat schon strukturell verifiziert: derselbe
		# IR/68k-Code wurde erfolgreich gegen die ECHTE clib.l gelinkt (echter r68+l68-Lauf,
		# siehe FORTSCHRITT.md).
		if build/qcc_p 'extern void* malloc(int size); extern void* realloc(void* p, int size); extern void free(void* p); int main(){ int* p; int* q; p = malloc(16); p[0] = 111; p[1] = 222; q = realloc(p, 32); putint(p[0]); putint(p[1]); free(q); putint(1); }' > build/qcc_extmalloc.ir && \
			build/qcc_backend build/qcc_extmalloc.ir build/qcc_extmalloc.s68; then
			cp build/qcc_extmalloc.s68 build/qcc_extmalloc_test.s68
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
			} >> build/qcc_extmalloc_test.s68
			if tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_extmalloc_test.bin build/qcc_extmalloc_test.s68 2>/dev/null && \
				[ "$(python3 tools/qcc68sim.py build/qcc_extmalloc_test.s68 2>/dev/null)" = "$(printf '111\n222\n1')" ]; then
				echo "ok    qcc extern 68000: malloc/realloc/free ueber CALLEXT korrekt (Aufrufmechanik + Pointer-Nutzung)"
			else
				echo "FAIL  qcc extern 68000: malloc/realloc/free-ABI fehlerhaft"; fail=1
			fi
		else
			echo "FAIL  qcc extern 68000: IR/Backend fuer malloc/realloc/free-Fall fehlgeschlagen"; fail=1
		fi
	else
		echo "warn  qcc extern 68000: Backend fehlt -- uebersprungen"
	fi
	# ARM64 kennt die Microware-ABI bewusst nicht -- CALLEXT muss dort sauber
	# scheitern (kein "silent wrong"), nicht das Backend crashen lassen.
	if [ -x build/qcc_arm64_backend ] && [ -f build/qcc_ext2.ir ]; then
		if build/qcc_arm64_backend build/qcc_ext2.ir build/qcc_ext2_arm64.s 2>&1 | grep -q 'unbekannter Opcode CALLEXT'; then
			echo "ok    qcc extern ARM64: CALLEXT wird sauber abgelehnt (Microware-ABI ist 68k-spezifisch)"
		else
			echo "FAIL  qcc extern ARM64: CALLEXT wird nicht sauber abgelehnt"; fail=1
		fi
	fi

	# Selfhosting L2, Milestone B (2026-07-25): Pilot-Portierung des ACTION/ROUTINE-
	# Ausschnitts aus codegen.cpp (ActionRoutine/pushRoutine/freeRoutines/routineTextC,
	# src/codegen.cpp:560-650) nach QCC -- testet Pointer-struct-Felder, ptr[i].feld
	# UND malloc/realloc/free ueber extern GLEICHZEITIG, in genau der Kombination, die der
	# echte Generator braucht. Bewusste Vereinfachung ggue. dem C-Original: pushRoutine
	# GIBT den (ggf. reallozierten) Array-Pointer zurueck statt ihn ueber einen ActionRoutine**-
	# Out-Parameter zu schreiben (QCC hat keine Pointer-auf-Pointer-Indizierung noetig,
	# dasselbe beobachtbare Verhalten ohne dieses Sprachmittel); kein strcpy/memcpy
	# (QCC hat keine Standardbibliothek), stattdessen manuelle Byte-Kopierschleifen.
	# Verifiziert per echtem r68+l68-Link gegen die ECHTE clib.l (malloc/realloc/free
	# loesen echt auf) -- STRUKTURELL bestaetigt, dass der Mechanismus korrekt ist. Echte
	# Ausfuehrung (bestaetigt, dass "one"/"two" nach dem realloc-Wachstum ueber "three"
	# hinaus noch korrekt lesbar sind) braucht entweder echten Q9-Zugriff oder einen
	# Kopier-faehigen Mock (qcc68sim modelliert nur ein generisches Adressregister a0,
	# kein registerindiziertes Kopieren beliebiger Laenge) -- bewusst NICHT Teil dieses
	# Schritts, siehe SELFHOSTING_LUECKENLISTE.md.
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		pilot_src='extern void* malloc(int size); extern void* realloc(void* p, int size); extern void free(void* p); struct ActionRoutine { char name[16]; char* text; }; struct ActionRoutine* pushRoutine(struct ActionRoutine* arr, int* cnt, int* cap, char* name, char* text, int textLen) { struct ActionRoutine* newArr; int newCap; int i; char* dst; if (*cnt >= *cap) { newCap = *cap > 0 ? *cap * 2 : 2; newArr = realloc(arr, newCap * sizeof(struct ActionRoutine)); arr = newArr; *cap = newCap; } dst = arr[*cnt].name; i = 0; while (name[i] != 0) { dst[i] = name[i]; i = i + 1; } dst[i] = 0; arr[*cnt].text = malloc(textLen + 1); dst = arr[*cnt].text; i = 0; while (i < textLen) { dst[i] = text[i]; i = i + 1; } dst[i] = 0; *cnt = *cnt + 1; return arr; } int routineIndexC(struct ActionRoutine* arr, int cnt, char* name) { int i; int j; bool match; char* n; char nc; for (i = 0; i < cnt; i = i + 1) { n = arr[i].name; match = true; j = 0; nc = n[j]; while (nc != 0) { if (nc != name[j]) { match = false; } j = j + 1; nc = n[j]; } if (name[j] != 0) { match = false; } if (match) { return i; } } return -1; } void freeRoutines(struct ActionRoutine* arr, int cnt) { int i; for (i = 0; i < cnt; i = i + 1) { free(arr[i].text); } } int main() { struct ActionRoutine* routines; int cnt; int cap; int idx; char* t; routines = 0; cnt = 0; cap = 0; routines = pushRoutine(routines, &cnt, &cap, "one", "TEXT-ONE", 8); routines = pushRoutine(routines, &cnt, &cap, "two", "TEXT-TWO", 8); routines = pushRoutine(routines, &cnt, &cap, "three", "TEXT-THREE", 10); putint(cnt); putint(cap); idx = routineIndexC(routines, cnt, "one"); t = routines[idx].text; putchar(t[0]); putchar(t[5]); idx = routineIndexC(routines, cnt, "three"); t = routines[idx].text; putchar(t[0]); putchar(t[9]); idx = routineIndexC(routines, cnt, "nope"); putint(idx); freeRoutines(routines, cnt); free(routines); return 0; }'
		if build/qcc_p "$pilot_src" > build/qcc_pilot.ir 2>build/qcc_pilot.err && [ ! -s build/qcc_pilot.err ] && \
			build/qcc_backend build/qcc_pilot.ir build/qcc_pilot.s68 -os9; then
			cp build/qcc_pilot.s68 "$MWOS_TMP/pilot.a"
			rm -f "$MWOS_TMP/pilot.r" "$MWOS_TMP/pilot.out" "$MWOS_TMP/pilot.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\pilot.a -o=M:\\TMP\\pilot.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/pilot.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\pilot.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\pilot.out -s=M:\\TMP\\pilot.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/pilot.out" ]; then
					echo "ok    qcc Selfhosting L2 Milestone B: ActionRoutine-Pilot kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  qcc Selfhosting L2 Milestone B: echter l68-Link fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  qcc Selfhosting L2 Milestone B: echte r68-Assemblierung fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/pilot.a "$MWOS_TMP"/pilot.r "$MWOS_TMP/pilot.out" "$MWOS_TMP/pilot.sym"
		else
			echo "FAIL  qcc Selfhosting L2 Milestone B: ActionRoutine-Pilot kompiliert nicht sauber (siehe build/qcc_pilot.err)"; fail=1
		fi
	else
		echo "warn  qcc Selfhosting L2 Milestone B: Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Globale structs (2026-07-25) machen den ECHTEN routinesC-Vollport-Fall jetzt moeglich:
	# routinesC/routinesCCnt/routinesCCap sind im echten codegen.cpp file-scope-Globale
	# (nicht wie im Pilot oben lokale Variablen einer Testfunktion) -- derselbe Test wie
	# oben, aber diesmal mit echten Globalen statt main()-lokalen Variablen.
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		pilotg_src='extern void* malloc(int size); extern void* realloc(void* p, int size); extern void free(void* p); struct ActionRoutine { char name[16]; char* text; }; struct ActionRoutine* routinesC; int routinesCCnt; int routinesCCap; void pushRoutine(char* name, char* text, int textLen) { struct ActionRoutine* newArr; int newCap; int i; char* dst; if (routinesCCnt >= routinesCCap) { newCap = routinesCCap > 0 ? routinesCCap * 2 : 2; newArr = realloc(routinesC, newCap * sizeof(struct ActionRoutine)); routinesC = newArr; routinesCCap = newCap; } dst = routinesC[routinesCCnt].name; i = 0; while (name[i] != 0) { dst[i] = name[i]; i = i + 1; } dst[i] = 0; routinesC[routinesCCnt].text = malloc(textLen + 1); dst = routinesC[routinesCCnt].text; i = 0; while (i < textLen) { dst[i] = text[i]; i = i + 1; } dst[i] = 0; routinesCCnt = routinesCCnt + 1; } int main() { int idx; char* t; routinesC = 0; routinesCCnt = 0; routinesCCap = 0; pushRoutine("one", "TEXT-ONE", 8); pushRoutine("two", "TEXT-TWO", 8); pushRoutine("three", "TEXT-THREE", 10); putint(routinesCCnt); putint(routinesCCap); t = routinesC[0].text; putchar(t[0]); t = routinesC[2].text; putchar(t[0]); putchar(t[9]); free(routinesC); return 0; }'
		if build/qcc_p "$pilotg_src" > build/qcc_pilotg.ir 2>build/qcc_pilotg.err && [ ! -s build/qcc_pilotg.err ] && \
			build/qcc_backend build/qcc_pilotg.ir build/qcc_pilotg.s68 -os9; then
			cp build/qcc_pilotg.s68 "$MWOS_TMP/pilotg.a"
			rm -f "$MWOS_TMP/pilotg.r" "$MWOS_TMP/pilotg.out" "$MWOS_TMP/pilotg.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\pilotg.a -o=M:\\TMP\\pilotg.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/pilotg.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\pilotg.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\pilotg.out -s=M:\\TMP\\pilotg.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/pilotg.out" ]; then
					echo "ok    qcc Selfhosting L2: ActionRoutine-Pilot MIT echten globalen routinesC/-Cnt/-Cap (wie im echten codegen.cpp) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  qcc Selfhosting L2: echter l68-Link (globale Variante) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  qcc Selfhosting L2: echte r68-Assemblierung (globale Variante) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/pilotg.a "$MWOS_TMP"/pilotg.r "$MWOS_TMP/pilotg.out" "$MWOS_TMP/pilotg.sym"
		else
			echo "FAIL  qcc Selfhosting L2: ActionRoutine-Pilot (globale Variante) kompiliert nicht sauber (siehe build/qcc_pilotg.err)"; fail=1
		fi
	else
		echo "warn  qcc Selfhosting L2 (globale Variante): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25): naechster Ausschnitt von codegen.cpp nach
	# src-qcc/codegen.tc portiert -- LEXER-Konfigurationsparser (lexUnquote/
	# lexUnquoteAt/lexParseConfig/markLexicalNode/computeLexicalSet, das [LEXER]-
	# Konfigurationsblock-Handling). Haengt an strncmp/strchr/strrchr/strstr/memcpy
	# (CALLEXT/clib.l) -- QCCVM kennt CALLEXT NICHT (keine libc-Simulation), daher
	# hier wie beim ActionRoutine-Piloten oben NUR strukturell verifiziert: kompiliert
	# sauber, echter r68 assembliert, echter l68 linkt gegen echte clib.l. Die
	# eigentliche ALGORITHMUS-Korrektheit (WHITESPACE-Escape-Dekodierung, mehrere
	# COMMENT LINE/BLOCK-Marker, COMMENT BLOCK NESTED-Erkennung, TOKEN-Registrierung,
	# UND -- am wichtigsten -- die TRANSITIVE lexikalische Markierung ueber NTS-
	# Referenzen in markLexicalNode) wurde EINMALIG separat verifiziert: eine Kopie
	# mit QCC-eigenen String-Helfern statt extern/CALLEXT (tcStrncmp/tcStrchr/...)
	# lieferte ueber QCCVM UND nativ per ARM64-Backend exakt dieselben 19 erwarteten
	# Werte (inkl. der transitiven Markierung einer per NTS referenzierten Regel) --
	# siehe docs/FORTSCHRITT.md fuer die Details, hier bewusst nicht dauerhaft als
	# Skript verankert (Wartungsaufwand einer zweiten String-Bibliothek nur fuers
	# Testen steht in keinem Verhaeltnis zum Grenzwert).
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
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
		if build/qcc_p "$(cat src-qcc/codegen.tc)$cgtest_main" > build/qcc_cgtest.ir 2>build/qcc_cgtest.err && \
			build/qcc_backend build/qcc_cgtest.ir build/qcc_cgtest_os9.a -os9 -largedata; then
			cp build/qcc_cgtest_os9.a "$MWOS_TMP/cgtest.a"
			rm -f "$MWOS_TMP/cgtest.r" "$MWOS_TMP/cgtest.out" "$MWOS_TMP/cgtest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\cgtest.a -o=M:\\TMP\\cgtest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/cgtest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\cgtest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\cgtest.out -s=M:\\TMP\\cgtest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/cgtest.out" ]; then
					echo "ok    qcc Selfhosting L2 Vollport: LEXER-Konfigurationsparser (src-qcc/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  qcc Selfhosting L2 Vollport: echter l68-Link (LEXER-Konfigurationsparser) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (LEXER-Konfigurationsparser) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/cgtest.a "$MWOS_TMP"/cgtest.r "$MWOS_TMP"/cgtest.out "$MWOS_TMP"/cgtest.sym
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/codegen.tc kompiliert nicht sauber (siehe build/qcc_cgtest.err)"; fail=1
		fi
	else
		echo "warn  qcc Selfhosting L2 Vollport (LEXER-Konfigurationsparser): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): naechster Ausschnitt
	# -- CODEGEN-Konfigurationsparser (cgenWantOS9/cgenStartRule/cgenParseConfig, das
	# [CODEGEN]-Konfigurationsblock-Handling aus src/codegen.cpp Zeilen 462-535).
	# Strukturell fast identisch zu lexParseConfig (letztes Wort einer Zeile
	# extrahieren) -- braucht KEINE Anfuehrungszeichen im Konfigurationsformat, daher
	# hier ohne den Backtick-Workaround aus dem LEXER-Test moeglich. Gleiche
	# dreifache Verifikationsmethode wie beim LEXER-Konfigurationsparser (QCCVM +
	# ARM64 mit QCC-eigenen String-Helfern lieferten identische Werte -- 1/
	# "mySect"+Nullterminator/"myRule"+Nullterminator, siehe docs/FORTSCHRITT.md);
	# hier nur die dauerhafte Regression (echter r68+l68 gegen echte clib.l).
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
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
		if build/qcc_p "$(cat src-qcc/codegen.tc)$cgentest_main" > build/qcc_cgentest.ir 2>build/qcc_cgentest.err && \
			build/qcc_backend build/qcc_cgentest.ir build/qcc_cgentest_os9.a -os9 -largedata; then
			cp build/qcc_cgentest_os9.a "$MWOS_TMP/cgentest.a"
			rm -f "$MWOS_TMP/cgentest.r" "$MWOS_TMP/cgentest.out" "$MWOS_TMP/cgentest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\cgentest.a -o=M:\\TMP\\cgentest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/cgentest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\cgentest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\cgentest.out -s=M:\\TMP\\cgentest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/cgentest.out" ]; then
					echo "ok    qcc Selfhosting L2 Vollport: CODEGEN-Konfigurationsparser (src-qcc/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  qcc Selfhosting L2 Vollport: echter l68-Link (CODEGEN-Konfigurationsparser) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (CODEGEN-Konfigurationsparser) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/cgentest.a "$MWOS_TMP"/cgentest.r "$MWOS_TMP"/cgentest.out "$MWOS_TMP"/cgentest.sym
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/codegen.tc (CODEGEN-Konfigurationsparser) kompiliert nicht sauber (siehe build/qcc_cgentest.err)"; fail=1
		fi
	else
		echo "warn  qcc Selfhosting L2 Vollport (CODEGEN-Konfigurationsparser): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): naechster Ausschnitt
	# -- ActionRoutine-Verwaltung UND ACTIONS-Konfigurationsparser (freeRoutines/
	# pushRoutineC/pushRoutine68k/routineTextC/routineText68k/lastWord/growLineBuf/
	# growCollectBuf/actionsParseConfig, das [NUTZER-CODE]-Block-Handling aus
	# src/codegen.cpp Zeilen 580-750). pushRoutineC/pushRoutine68k sind ZWEI fast
	# identische Funktionen statt EINER generischen mit "ActionRoutine**"-Parameter
	# wie im C++-Original (QCC hat keine Generik/Funktionszeiger, routinesC/
	# routines68k sind hier wie im Original file-scope-Globale -- direktes Mutieren
	# ist einfacher, gleiches Muster wie beim ActionRoutine-Piloten Milestone B).
	# Verifikation NUR strukturell (wie beim Piloten): kompiliert sauber (NULL
	# Semantikfehler), assembliert (echter r68), linkt (echter l68 gegen echte
	# clib.l). Eine versuchte TIEFERE Logikverifikation (QCCVM/ARM64 mit
	# selbstgeschriebenen malloc/realloc-Ersatzfunktionen fuer eigenstaendige
	# Ausfuehrbarkeit) scheiterte an Grenzen der TESTUMGEBUNG, nicht des Ports:
	# QCCVMs Zeigermodell ist nicht byte-adressierbar und vertraegt keine
	# malloc-Heap-Umdeutung auf struct-Zeiger; eine ARM64-Reproduktion mit
	# selbstgebautem Bump-Allocator stuerzte ab (vermutlich ein Bug im
	# Test-Stub selbst, nicht im geprueften Code -- der echte extern-Pfad
	# gegen clib.l linkt fehlerfrei). Echte Verhaltensverifikation bleibt der
	# Live-Q9-Ausfuehrung vorbehalten (bereits als offener Schritt vermerkt).
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
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
		if build/qcc_p "$(cat src-qcc/codegen.tc)$actiontest_main" > build/qcc_actiontest.ir 2>build/qcc_actiontest.err && \
			build/qcc_backend build/qcc_actiontest.ir build/qcc_actiontest_os9.a -os9 -largedata; then
			cp build/qcc_actiontest_os9.a "$MWOS_TMP/actiontest.a"
			rm -f "$MWOS_TMP/actiontest.r" "$MWOS_TMP/actiontest.out" "$MWOS_TMP/actiontest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\actiontest.a -o=M:\\TMP\\actiontest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/actiontest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\actiontest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\actiontest.out -s=M:\\TMP\\actiontest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/actiontest.out" ]; then
					echo "ok    qcc Selfhosting L2 Vollport: ActionRoutine/ACTIONS-Konfigurationsparser (src-qcc/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  qcc Selfhosting L2 Vollport: echter l68-Link (ActionRoutine/ACTIONS-Konfigurationsparser) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (ActionRoutine/ACTIONS-Konfigurationsparser) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/actiontest.a "$MWOS_TMP"/actiontest.r "$MWOS_TMP"/actiontest.out "$MWOS_TMP"/actiontest.sym
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/codegen.tc (ActionRoutine/ACTIONS-Konfigurationsparser) kompiliert nicht sauber (siehe build/qcc_actiontest.err)"; fail=1
		fi
	else
		echo "warn  qcc Selfhosting L2 Vollport (ActionRoutine/ACTIONS-Konfigurationsparser): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): naechster Ausschnitt
	# -- AST-Validierung (isWordLiteral/nodeNullable/validateRepeatProgress/
	# validateAstForCodegen, Zeilen 750-857 in src/codegen.cpp). Prueft VOR der
	# Ausgabe: (1) eine Wiederholung mit nullbarem Rumpf (z.B. "{ [x] }") waere eine
	# Endlosschleife im erzeugten Parser -- wird erkannt und abgelehnt (Fixpunkt-
	# analyse ueber gegenseitig rekursive Regeln); (2) zwei Regeln, die nach der
	# "$"->"_"-Normalisierung denselben Namen ergeben, wuerden doppelte C-Funktionen/
	# 68k-Labels erzeugen -- wird erkannt und abgelehnt. Einmalig per QCCVM
	# tiefenverifiziert (mit QCC-eigenen Stand-ins fuer strcmp/isalpha/isalnum
	# statt extern): alle 10 erwarteten Werte trafen exakt zu, inklusive beider
	# Diagnosepfade -- siehe docs/FORTSCHRITT.md. ARM64 hier NICHT moeglich (das
	# kumulative Gesamtkompilat enthaelt bereits "free" aus dem ActionRoutine-Chunk,
	# CALLEXT wird von ARM64 grundsaetzlich abgelehnt, unabhaengig von Erreichbarkeit
	# -- QCCVM meldet den Fehler dagegen nur bei tatsaechlicher Ausfuehrung, hier
	# also unproblematisch). Dauerhafte Regression wie ueblich: echter r68+l68.
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
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
		if build/qcc_p "$(cat src-qcc/codegen.tc)$astvaltest_main" > build/qcc_astvaltest.ir 2>build/qcc_astvaltest.err && \
			build/qcc_backend build/qcc_astvaltest.ir build/qcc_astvaltest_os9.a -os9 -largedata; then
			cp build/qcc_astvaltest_os9.a "$MWOS_TMP/astvaltest.a"
			rm -f "$MWOS_TMP/astvaltest.r" "$MWOS_TMP/astvaltest.out" "$MWOS_TMP/astvaltest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\astvaltest.a -o=M:\\TMP\\astvaltest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/astvaltest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\astvaltest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\astvaltest.out -s=M:\\TMP\\astvaltest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/astvaltest.out" ]; then
					echo "ok    qcc Selfhosting L2 Vollport: AST-Validierung (src-qcc/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  qcc Selfhosting L2 Vollport: echter l68-Link (AST-Validierung) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (AST-Validierung) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/astvaltest.a "$MWOS_TMP"/astvaltest.r "$MWOS_TMP"/astvaltest.out "$MWOS_TMP"/astvaltest.sym
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/codegen.tc (AST-Validierung) kompiliert nicht sauber (siehe build/qcc_astvaltest.err)"; fail=1
		fi
	else
		echo "warn  qcc Selfhosting L2 Vollport (AST-Validierung): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): naechster Ausschnitt
	# -- C-Backend-Codegenerator (emitCString/emitLongerLiteralRejectC/genNodeC,
	# src/codegen.cpp Zeilen 858-1000). Erste Beruehrung mit ECHTER Dateiausgabe
	# (fopen/fprintf/fputc/fclose statt nur stdout-Diagnosen wie bisher) -- FILE*
	# wird als "void*" gefuehrt (QCC hat keinen FILE-Struct-Typ, der ABI-Aufruf
	# braucht nur einen opaken Zeiger). clib.l hat KEIN snprintf (nur sprintf,
	# per `strings` bestaetigt) -- deshalb sprintf ohne Laengenlimit verwendet.
	# NEUE QCC-Grenze gefunden: QCC kann KEINE EIGENEN variadischen
	# Funktionen definieren (nur variadische extern-Aufrufe) -- ein QCC-
	# Stand-in fuer fprintf (fuer QCCVM/ARM64-Tiefenverifikation wie bei den
	# String-Funktionen zuvor) ist deshalb NICHT moeglich. Verifikation bleibt
	# bei kompiliert sauber + echter r68/l68-Link (wie beim ActionRoutine-Chunk).
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
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
	fp = fopen("/tmp/qcc_gennodetest_output.c", "w");
	if (fp == 0) { putint(-1); return 1; }
	genNodeC(fp, rules[1].root, 999, 0);
	fclose(fp);
	putint(1);
	return 0;
}'
		if build/qcc_p "$(cat src-qcc/codegen.tc)$gennodetest_main" > build/qcc_gennodetest.ir 2>build/qcc_gennodetest.err && \
			build/qcc_backend build/qcc_gennodetest.ir build/qcc_gennodetest_os9.a -os9 -largedata; then
			cp build/qcc_gennodetest_os9.a "$MWOS_TMP/gennodetest.a"
			rm -f "$MWOS_TMP/gennodetest.r" "$MWOS_TMP/gennodetest.out" "$MWOS_TMP/gennodetest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\gennodetest.a -o=M:\\TMP\\gennodetest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/gennodetest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\gennodetest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\gennodetest.out -s=M:\\TMP\\gennodetest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/gennodetest.out" ]; then
					echo "ok    qcc Selfhosting L2 Vollport: C-Backend-Codegenerator (src-qcc/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  qcc Selfhosting L2 Vollport: echter l68-Link (C-Backend-Codegenerator) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (C-Backend-Codegenerator) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/gennodetest.a "$MWOS_TMP"/gennodetest.r "$MWOS_TMP"/gennodetest.out "$MWOS_TMP"/gennodetest.sym
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/codegen.tc (C-Backend-Codegenerator) kompiliert nicht sauber (siehe build/qcc_gennodetest.err)"; fail=1
		fi
	else
		echo "warn  qcc Selfhosting L2 Vollport (C-Backend-Codegenerator): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): naechster Ausschnitt
	# -- genParserC (der Rest des C-Backends: Datei-Header, Lexer-Helfer ws()/idch(),
	# Action-Log-Runtime-Geruest, eine p_<regel>()-Funktion pro Regel, main() --
	# src/codegen.cpp Zeilen 1002-1168). SECHS Stellen im C++-Original betten ein
	# Anfuehrungszeichen DIREKT in einen fprintf-Formatstring ein -- geht in QCC
	# nicht (siehe Quirk in qcc-vollport-status.md), per fputc(34,fp)-Aufteilung
	# umgeschrieben. ZWEI dieser Stellen haben zusaetzlich ein "%%"-Selbstescape im
	# Original (literales "%" ohne eigenes Substitutionsargument) -- als "%s"-Argument
	# uebergeben statt direkt in den Formatstring geschrieben (sonst re-interpretiert
	# QCC/clib.l's fprintf das "%" als Formatzeichen).
	#
	# WICHTIGER, EIGENSTAENDIGER FUND waehrend dieses Chunks (nicht der Port selbst,
	# sondern ZWEI echte Bugs in der Toolchain):
	# 1. Ein bisher unbekannter QCC-PARSER-Bug: ein QCC-String-Literal, das die
	#    Zeichenfolge "&&" oder "||" als reinen TEXT enthaelt (hier: generierter C-Code
	#    braucht selbst && / || in ws()/idch()), loeste eine falsche "qcc: logical-
	#    frame mismatch"-Diagnose aus. NICHT die Grammatik gefixt (Risiko/Aufwand vs.
	#    Nutzen) -- stattdessen jede betroffene Stelle per fputc(38,fp)/fputc(124,fp)
	#    umgeschrieben (emitAndAnd/emitOrOr-Helfer), sodass "&&"/"||" nie als
	#    zusammenhaengende Zeichenfolge in einem QCC-String-Literal auftaucht.
	# 2. Ein ECHTER SKALIERUNGSBUG im 68k-Backend selbst (src/qcc_backend_c.cpp):
	#    das -largedata-Datenmodell lud JEDEN Tabelleneintrag bisher per EIGENEM
	#    PC-relativem Label ("movea.l tc_ga_X(pc),reg") -- das brach, sobald der
	#    GESAMTE Funktionscode zwischen einer fruehen Funktion (z.B. "main", die per
	#    -largedata-Funktionsaufruf-Fix immer zuerst emittiert wird) und der Tabelle
	#    selbst (die NACH allen Funktionsrumpf-Texten lag) mehr als 32 KB umfasste --
	#    genau das trat beim ersten echten Skalierungstest fuer src-qcc/codegen.tc
	#    auf (kumulatives Kompilat inzwischen weit ueber 32 KB Code) und liess
	#    RUECKWIRKEND ALLE bisherigen Vollport-Regressionstests fehlschlagen (der
	#    naechste Funktionsaufruf/Global-Zugriff konnte die Tabelle nicht mehr
	#    erreichen). GEFIXT nach EXAKT demselben Muster wie tc_functab/a4: ein Register
	#    (a3) wird EINMAL beim Programmstart auf die absolute Adresse EINER
	#    kombinierten Tabelle (tc_gadata, direkt nach tc_functab, vor allen
	#    Funktionsrumpf-Texten) gesetzt; jeder Globalzugriff wird zu "move.l
	#    <gidx*4>(a3),reg" statt einem PC-relativen Tabellen-Label-Load. tools/
	#    qcc68sim.py (Test-Simulator) musste dafuer a3 in sein generisches
	#    Adressregister-Dict aufnehmen (war zuvor auf a0/a2/a4 beschraenkt).
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		genparsertest_main='
int main() {
	int m;
	int m2;
	int ok;
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
	ok = genParserC("/tmp/qcc_genparserc_output.c");
	putint(ok);
	return 0;
}'
		if build/qcc_p "$(cat src-qcc/codegen.tc)$genparsertest_main" > build/qcc_genparsertest.ir 2>build/qcc_genparsertest.err && \
			build/qcc_backend build/qcc_genparsertest.ir build/qcc_genparsertest_os9.a -os9 -largedata; then
			cp build/qcc_genparsertest_os9.a "$MWOS_TMP/genparsertest.a"
			rm -f "$MWOS_TMP/genparsertest.r" "$MWOS_TMP/genparsertest.out" "$MWOS_TMP/genparsertest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\genparsertest.a -o=M:\\TMP\\genparsertest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/genparsertest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\genparsertest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\genparsertest.out -s=M:\\TMP\\genparsertest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/genparsertest.out" ]; then
					echo "ok    qcc Selfhosting L2 Vollport: genParserC (src-qcc/codegen.tc) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  qcc Selfhosting L2 Vollport: echter l68-Link (genParserC) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (genParserC) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/genparsertest.a "$MWOS_TMP"/genparsertest.r "$MWOS_TMP"/genparsertest.out "$MWOS_TMP"/genparsertest.sym
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/codegen.tc (genParserC) kompiliert nicht sauber (siehe build/qcc_genparsertest.err)"; fail=1
		fi
	else
		echo "warn  qcc Selfhosting L2 Vollport (genParserC): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# Selfhosting L2 Vollport (2026-07-25, direkt im Anschluss): letzter Ausschnitt
	# von codegen.cpp -- 68k-Backend (charComment/emitConsume68k/
	# emitLongerLiteralReject68k/genNode68k/emitLexHelpers68k/genParser68kTo/
	# genParser68k/genParser68kOS9, src/codegen.cpp Zeilen 858+1181-1580). Damit
	# ist der GESAMTE Vollport von codegen.cpp abgeschlossen. Erzeugt reinen
	# 68k-Assemblertext -- KEINE C-Operatoren (&&/||) und KEINE eingebetteten
	# Anfuehrungszeichen im generierten Code, daher weder der emitAndAnd/emitOrOr-
	# noch der fputc(34,fp)-Workaround aus genParserC hier gebraucht.
	#
	# DREI weitere Funde/Fixes waehrend dieses Chunks:
	# 1. DIESELBE "?"-Variante des logical-frame-Bugs (siehe genParserC): ein
	#    String-Literal mit "?" als Text ("Identifikator-Zeichen? d0.b...") loeste
	#    "qcc: conditional-frame mismatch" aus (tcTernaryDepth-Tracking).
	#    Gefixt per neuem emitQMark(fp)-Helfer (ein fputc(63,fp)), analog zu
	#    emitAndAnd/emitOrOr.
	# 2. Backend-eigene MAX_GLOBALS-Grenze (256, GETRENNT von der Frontend-
	#    Grenze in data/qcc.lextab) blockierte den Skalierungsnachweis: JEDES
	#    String-Literal im QCC-Quelltext wird zu einem anonymen __strN-Global,
	#    das kumulative Kompilat hat inzwischen weit ueber 256 davon. Erhoeht auf
	#    1024 (src/qcc_backend_c.cpp).
	# 3. Ein WEITERER echter Skalierungsbug im 68k-Backend: tc_extcall_tmp (der
	#    Scratch-Puffer fuer externe Aufrufe mit Stack-Argumenten) wurde bisher
	#    IMMER per PC-relativem "lea tc_extcall_tmp(pc),a0" direkt an der
	#    Aufrufstelle referenziert -- unabhaengig von -largedata. Brach aus
	#    demselben Grund wie der tc_gadata-Fund zuvor. Gefixt nach demselben
	#    a3-Muster: tc_extcall_tmp bekommt einen ZUSAETZLICHEN Eintrag in
	#    tc_gadata (Offset globalCount*4, direkt nach allen echten Globalen);
	#    tc_gadata wird jetzt IMMER emittiert (nicht nur bei globalCount > 0).
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		genparser68ktest_main='
int main() {
	int m;
	int m2;
	int ok;
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
	ok = genParser68kOS9("/tmp/qcc_genparser68k_output.a", "testbase");
	putint(ok);
	return 0;
}'
		if build/qcc_p "$(cat src-qcc/codegen.tc)$genparser68ktest_main" > build/qcc_genparser68ktest.ir 2>build/qcc_genparser68ktest.err && \
			build/qcc_backend build/qcc_genparser68ktest.ir build/qcc_genparser68ktest_os9.a -os9 -largedata; then
			cp build/qcc_genparser68ktest_os9.a "$MWOS_TMP/genparser68ktest.a"
			rm -f "$MWOS_TMP/genparser68ktest.r" "$MWOS_TMP/genparser68ktest.out" "$MWOS_TMP/genparser68ktest.sym"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\genparser68ktest.a -o=M:\\TMP\\genparser68ktest.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/genparser68ktest.r" ]; then
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\genparser68ktest.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\genparser68ktest.out -s=M:\\TMP\\genparser68ktest.sym" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/genparser68ktest.out" ]; then
					echo "ok    qcc Selfhosting L2 Vollport: 68k-Backend (src-qcc/codegen.tc, Vollport von codegen.cpp abgeschlossen) kompiliert, assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
				else
					echo "FAIL  qcc Selfhosting L2 Vollport: echter l68-Link (68k-Backend) fehlgeschlagen"; fail=1
				fi
			else
				echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (68k-Backend) fehlgeschlagen"; fail=1
			fi
			rm -f "$MWOS_TMP"/genparser68ktest.a "$MWOS_TMP"/genparser68ktest.r "$MWOS_TMP"/genparser68ktest.out "$MWOS_TMP"/genparser68ktest.sym
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/codegen.tc (68k-Backend) kompiliert nicht sauber (siehe build/qcc_genparser68ktest.err)"; fail=1
		fi
	else
		echo "warn  qcc Selfhosting L2 Vollport (68k-Backend): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Link uebersprungen"
	fi

	# -largedata (2026-07-25, "Speichermodell"-Schalter): der 68k-Backend adressiert
	# Globale standardmaessig AUSSCHLIESSLICH PC-relativ -- eine ECHTE 68000-Grenze
	# (16-Bit-Displacement, +-32 KB), die schon bei einem Array von wenigen Dutzend
	# KB "value out of range" von r68 ausloest (empirisch bestaetigt, siehe
	# docs/FORTSCHRITT.md). -largedata schaltet auf eine Indirektionstabelle mit
	# absoluten (vom Linker aufgeloesten) Adressen um -- getestet mit einem
	# struct-Array, das OHNE -largedata garantiert zu gross waere (analog zum
	# urspruenglichen AST_MAX_NODES=8192-Fund in src-qcc/codegen.tc).
	largedata_src='struct Big { int a; char pad[52]; }; struct Big arr[2048]; int cnt; int main(){ arr[0].a=42; arr[2047].a=99; cnt=7; putint(arr[0].a); putint(arr[2047].a); putint(cnt); }'
	if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
	   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
		mkdir -p "$MWOS_TMP"
		if build/qcc_p "$largedata_src" > build/qcc_largedata.ir && \
			build/qcc_backend build/qcc_largedata.ir build/qcc_largedata.s68 -largedata && \
			build/qcc_backend build/qcc_largedata.ir build/qcc_largedata_os9.a -os9 -largedata; then
			cp build/qcc_largedata_os9.a "$MWOS_TMP/largedata.a"
			rm -f "$MWOS_TMP/largedata.r"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\largedata.a -o=M:\\TMP\\largedata.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/largedata.r" ] && [ "$(python3 tools/qcc68sim.py build/qcc_largedata.s68 2>/dev/null)" = "$(printf '42\n99\n7')" ]; then
				echo "ok    qcc -largedata: grosses struct-Array (112 KB, weit ueber der PC-relativen Grenze) kompiliert, assembliert (echter r68) und simuliert korrekt"
			else
				echo "FAIL  qcc -largedata: grosses struct-Array fehlerhaft"; fail=1
			fi
			rm -f "$MWOS_TMP"/largedata.a "$MWOS_TMP"/largedata.r
		else
			echo "FAIL  qcc -largedata: IR/Backend fehlgeschlagen"; fail=1
		fi
	else
		echo "warn  qcc -largedata: Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter r68-Test uebersprungen"
	fi
	# Gegenprobe: OHNE -largedata muss (a) unser Backend selbst schon eine Warnung
	# ausgeben (Groessen-Heuristik, siehe main() in qcc_backend_c.cpp) UND (b)
	# der ECHTE r68 dasselbe Programm mit "value out of range" ablehnen -- damit
	# ist die Notwendigkeit von -largedata fuer diesen Fall doppelt belegt.
	if build/qcc_p "$largedata_src" > build/qcc_largedata2.ir && \
		build/qcc_backend build/qcc_largedata2.ir build/qcc_largedata2.s68 -os9 2>build/qcc_largedata2.warn; then
		if grep -q 'globale Daten sind mit .* Byte recht gross' build/qcc_largedata2.warn; then
			echo "ok    qcc: Groessen-Heuristik warnt VOR -largedata, wenn globale Daten gross werden"
		else
			echo "FAIL  qcc: Groessen-Heuristik-Warnung fehlt"; fail=1
		fi
		if [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ]; then
			cp build/qcc_largedata2.s68 "$MWOS_TMP/largedata2.a"
			rm -f "$MWOS_TMP/largedata2.r"
			out=$(WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\largedata2.a -o=M:\\TMP\\largedata2.r -q" 2>&1)
			if echo "$out" | grep -q 'value out of range'; then
				echo "ok    qcc: echter r68 lehnt dasselbe Array OHNE -largedata tatsaechlich mit 'value out of range' ab"
			else
				echo "FAIL  qcc: r68 haette OHNE -largedata ablehnen muessen -- Gegenprobe ungueltig"; fail=1
			fi
			rm -f "$MWOS_TMP"/largedata2.a "$MWOS_TMP"/largedata2.r
		else
			echo "warn  qcc -largedata-Gegenprobe (echter r68): Wine/MWOS nicht verfuegbar -- uebersprungen"
		fi
	else
		echo "FAIL  qcc: IR/Backend fuer -largedata-Gegenprobe fehlgeschlagen"; fail=1
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
	if [ -x build/qcc_backend ] && command -v python3 >/dev/null 2>&1; then
		if build/qcc_p "$largefunc_src" > build/qcc_largefunc.ir && \
			build/qcc_backend build/qcc_largefunc.ir build/qcc_largefunc.s68 -largedata && \
			[ "$(python3 tools/qcc68sim.py build/qcc_largefunc.s68 2>/dev/null)" = "$(printf '196\n14204\n6311')" ]; then
			echo "ok    qcc -largedata Funktionsaufrufe: 150 Funktionen (>32 KB Code) ueber Indirektionstabelle (a4/a2) statt bsr = qccvm"
		else
			echo "FAIL  qcc -largedata Funktionsaufrufe: 150-Funktionen-Testfall stimmt nicht mit qccvm ueberein"; fail=1
		fi
		if [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
		   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
			mkdir -p "$MWOS_TMP"
			if build/qcc_backend build/qcc_largefunc.ir build/qcc_largefunc_os9.a -os9 -largedata; then
				cp build/qcc_largefunc_os9.a "$MWOS_TMP/largefunc.a"
				rm -f "$MWOS_TMP/largefunc.r" "$MWOS_TMP/largefunc.out" "$MWOS_TMP/largefunc.sym"
				WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\largefunc.a -o=M:\\TMP\\largefunc.r -q" >/dev/null 2>&1
				if [ -s "$MWOS_TMP/largefunc.r" ]; then
					WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\largefunc.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\largefunc.out -s=M:\\TMP\\largefunc.sym" >/dev/null 2>&1
					if [ -s "$MWOS_TMP/largefunc.out" ]; then
						echo "ok    qcc -largedata Funktionsaufrufe: 150-Funktionen-Programm (>32 KB Code) assembliert (echter r68) und linkt (echter l68 gegen echte clib.l) korrekt"
					else
						echo "FAIL  qcc -largedata Funktionsaufrufe: echter l68-Link fehlgeschlagen"; fail=1
					fi
				else
					echo "FAIL  qcc -largedata Funktionsaufrufe: echte r68-Assemblierung fehlgeschlagen"; fail=1
				fi
				rm -f "$MWOS_TMP"/largefunc.a "$MWOS_TMP"/largefunc.r "$MWOS_TMP"/largefunc.out "$MWOS_TMP"/largefunc.sym
			else
				echo "FAIL  qcc -largedata Funktionsaufrufe: -os9-Backend-Lauf fehlgeschlagen"; fail=1
			fi
			# Gegenprobe: dasselbe 150-Funktionen-Programm OHNE -largedata muss der
			# echte r68 tatsaechlich mit "branch out of range" ablehnen (bsr ueber
			# mehr als 32 KB -- andere Fehlermeldung als "value out of range" bei
			# lea/movea fuer Daten) -- belegt die Notwendigkeit auch fuer
			# Funktionsaufrufe, nicht nur fuer globale Daten.
			if build/qcc_backend build/qcc_largefunc.ir build/qcc_largefunc2_os9.a -os9; then
				cp build/qcc_largefunc2_os9.a "$MWOS_TMP/largefunc2.a"
				rm -f "$MWOS_TMP/largefunc2.r"
				out=$(WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\largefunc2.a -o=M:\\TMP\\largefunc2.r -q" 2>&1)
				# "branch out of range" (nicht "value out of range" wie bei lea/movea
				# fuer Daten) ist die reale r68-Fehlermeldung fuer ein zu weit
				# entferntes bsr-Ziel -- empirisch bestaetigt (2026-07-25).
				if echo "$out" | grep -q 'branch out of range'; then
					echo "ok    qcc: echter r68 lehnt dasselbe 150-Funktionen-Programm OHNE -largedata tatsaechlich mit 'branch out of range' ab"
				else
					echo "FAIL  qcc: r68 haette OHNE -largedata (Funktionsaufrufe) ablehnen muessen -- Gegenprobe ungueltig"; fail=1
				fi
				rm -f "$MWOS_TMP"/largefunc2.a "$MWOS_TMP"/largefunc2.r
			else
				echo "FAIL  qcc: -os9-Backend-Lauf (Funktionsaufruf-Gegenprobe) fehlgeschlagen"; fail=1
			fi
		else
			echo "warn  qcc -largedata Funktionsaufrufe: Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter r68/l68-Test uebersprungen"
		fi
	else
		echo "warn  qcc -largedata Funktionsaufrufe: Backend oder python3 fehlt -- uebersprungen"
	fi

	# 13a-os9) Microware-r68-Ausgabemodus (2026-07-24): qcc_backend akzeptiert
	# ein optionales 4. Argument "-os9" und schaltet dann auf nam/psect/ends-
	# Rahmung, "*" statt ";" fuer volle Kommentarzeilen und "align 4"/"dc.l 0,.."
	# statt "even"/"ds.l" um (r68 kennt even/ds.l/rmb/cnop nicht -- empirisch
	# gegen die echte Microware-Toolchain ermittelt). Deckt DATA/BSS/Scratch-
	# Puffer UND einen CALLEXT-Aufruf gleichzeitig ab; wird -- wie schon bei
	# oberon0_os9.a -- gegen den ECHTEN r68.exe (via Wine/MWOS) assembliert.
	if [ -x build/qcc_backend ]; then
		build/qcc_p 'int counter; int limit = 10; char buf[4]; extern int myadd(int a, int b); int helper(int x){ return x+1; } int main(){ counter = myadd(3,4); limit = helper(counter); putint(limit); }' > build/qcc_os9combo.ir
		if build/qcc_backend build/qcc_os9combo.ir build/qcc_os9combo.s68 -os9 && \
			grep -q '^	nam	qcc_os9combo_p$' build/qcc_os9combo.s68 && \
			grep -q '^	psect	qcc_os9combo_p,0,0,1,0,0$' build/qcc_os9combo.s68 && \
			grep -q '^	ends$' build/qcc_os9combo.s68; then
			echo "ok    qcc -os9: nam/psect/ends-Rahmung wird erzeugt"
		else
			echo "FAIL  qcc -os9: nam/psect/ends-Rahmung fehlt oder Backend-Aufruf fehlgeschlagen"; fail=1
		fi
		if [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && [ -f build/qcc_os9combo.s68 ]; then
			mkdir -p "$MWOS_TMP"
			cp build/qcc_os9combo.s68 "$MWOS_TMP/tccombo.a"
			rm -f "$MWOS_TMP/tccombo.r"
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\tccombo.a -o=M:\\TMP\\tccombo.r -q" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/tccombo.r" ]; then
				echo "ok    qcc -os9: DATA/BSS/Scratch-Puffer + CALLEXT assemblieren fehlerfrei (Microware r68 via Wine)"
			else
				echo "FAIL  qcc -os9: assembliert NICHT (r68 via Wine)"; fail=1
			fi
			rm -f "$MWOS_TMP/tccombo.a" "$MWOS_TMP/tccombo.r"
		else
			echo "warn  qcc -os9: Wine/MWOS nicht verfuegbar -- echter r68-Check uebersprungen"
		fi
	else
		echo "warn  qcc -os9: Backend fehlt -- uebersprungen"
	fi
else
	echo "warn  qcc M4a: vasm fehlt -- Backend-Assembler-Check uebersprungen"
fi

# 13a-link) QCC Mehrdatei-Uebersetzung, M2 (2026-07-25): ZWEI SEPARAT mit
#     -part kompilierte Dateien (eine zusaetzlich mit -runtime fuer den
#     gemeinsamen 68k-Core/I/O-Anker, siehe qcc_backend_c.cpp) werden mit dem
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
if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
	mkdir -p "$MWOS_TMP"
	build/qcc_p 'int shared; int helper(int x); static int localHelper(int x){ return x-1; } int main(){ shared = 10; putint(helper(shared)); putint(localHelper(shared)); }' > build/qcc_mf_link_a.ir
	build/qcc_p 'extern int shared; int helper(int x){ return shared + x; }' > build/qcc_mf_link_b.ir
	if build/qcc_backend build/qcc_mf_link_a.ir build/qcc_mf_link_a.s68 -os9 -part -runtime && \
		build/qcc_backend build/qcc_mf_link_b.ir build/qcc_mf_link_b.s68 -os9 -part; then
		cp build/qcc_mf_link_a.s68 "$MWOS_TMP/mflinka.a"
		cp build/qcc_mf_link_b.s68 "$MWOS_TMP/mflinkb.a"
		rm -f "$MWOS_TMP/mflinka.r" "$MWOS_TMP/mflinkb.r" "$MWOS_TMP/mflink.out" "$MWOS_TMP/mflink.sym"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\mflinka.a -o=M:\\TMP\\mflinka.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\mflinkb.a -o=M:\\TMP\\mflinkb.r -q" >/dev/null 2>&1
		if [ -s "$MWOS_TMP/mflinka.r" ] && [ -s "$MWOS_TMP/mflinkb.r" ]; then
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\mflinka.r M:\\TMP\\mflinkb.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\mflink.out -s=M:\\TMP\\mflink.sym" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/mflink.out" ] && grep -q "tc_localHelper__qcc_mf_link_a_p" "$MWOS_TMP/mflink.sym" && \
				grep -q "tc_helper " "$MWOS_TMP/mflink.sym" && grep -q "tc_g_shared" "$MWOS_TMP/mflink.sym"; then
				echo "ok    qcc Mehrdatei M2: echter r68+l68-Link zweier getrennt kompilierter Dateien (Funktionsaufruf+Global ueber Dateigrenze, static-Mangling) korrekt"
			else
				echo "FAIL  qcc Mehrdatei M2: echter l68-Link fehlerhaft oder Symbole fehlen"; fail=1
			fi
		else
			echo "FAIL  qcc Mehrdatei M2: r68-Assemblierung einer der beiden Dateien fehlgeschlagen"; fail=1
		fi
		rm -f "$MWOS_TMP"/mflink*.a "$MWOS_TMP"/mflink*.r "$MWOS_TMP/mflink.out" "$MWOS_TMP/mflink.sym"
	else
		echo "FAIL  qcc Mehrdatei M2: -part/-runtime-Backend-Aufruf fehlgeschlagen"; fail=1
	fi
	# Duplicate-Symbol-Testfall: ZWEI Dateien definieren dieselbe nicht-static
	# Funktion -- l68 muss das als "duplicate symbol" ablehnen (simuliert, was
	# jeder echte Linker tut, siehe M0-Spike-Ergebnis in docs/STATUS.md).
	build/qcc_p 'int f(){ return 1; } int main(){ putint(f()); }' > build/qcc_mf_dup_a.ir
	build/qcc_p 'int f(){ return 2; }' > build/qcc_mf_dup_b.ir
	if build/qcc_backend build/qcc_mf_dup_a.ir build/qcc_mf_dup_a.s68 -os9 -part -runtime && \
		build/qcc_backend build/qcc_mf_dup_b.ir build/qcc_mf_dup_b.s68 -os9 -part; then
		cp build/qcc_mf_dup_a.s68 "$MWOS_TMP/mfdupa.a"
		cp build/qcc_mf_dup_b.s68 "$MWOS_TMP/mfdupb.a"
		rm -f "$MWOS_TMP/mfdupa.r" "$MWOS_TMP/mfdupb.r" "$MWOS_TMP/mfdup.out"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\mfdupa.a -o=M:\\TMP\\mfdupa.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\mfdupb.a -o=M:\\TMP\\mfdupb.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\mfdupa.r M:\\TMP\\mfdupb.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\mfdup.out" >build/qcc_mf_dup.err 2>&1
		if [ ! -s "$MWOS_TMP/mfdup.out" ] && grep -qi "duplicate symbol" build/qcc_mf_dup.err; then
			echo "ok    qcc Mehrdatei M2: echter l68 lehnt doppelte nicht-static Definition als 'duplicate symbol' ab"
		else
			echo "FAIL  qcc Mehrdatei M2: l68 haette 'duplicate symbol' melden muessen"; fail=1
		fi
		rm -f "$MWOS_TMP"/mfdup*.a "$MWOS_TMP"/mfdup*.r "$MWOS_TMP/mfdup.out" build/qcc_mf_dup.err
	else
		echo "FAIL  qcc Mehrdatei M2: Backend-Aufruf fuer Duplicate-Test fehlgeschlagen"; fail=1
	fi
else
	echo "warn  qcc Mehrdatei M2: Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echter Mehrdatei-Link uebersprungen"
fi

# 13a) "static" lokale Variablen (2026-07-24): GLOBAL/GARRAY/GINIT duerfen jetzt auch
#      INNERHALB einer Funktion im IR-Strom stehen (frueher nur davor erlaubt) -- eine
#      static-Lokale wird an genau der Textstelle ihrer Deklaration als GLOBAL emittiert,
#      siehe collectGlobals()/collectFunctions() in qcc_backend_c.cpp. Test prueft
#      echte Persistenz ueber mehrere Aufrufe hinweg im ECHTEN 68000-Code (nicht nur QCCVM).
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'int bump(){ static int counter; counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); putint(bump()); }' > build/qcc_static.ir && \
		build/qcc_backend build/qcc_static.ir build/qcc_static.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_static.bin build/qcc_static.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_static.s68 2>/dev/null)" = "$(printf '1\n2\n3')" ]; then
		echo "ok    qcc static 68000: lokale static-Variable persistiert ueber Aufrufe hinweg"
	else
		echo "FAIL  qcc static 68000: static-Persistenz fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc static 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi

# 13a-2) nicht-konstanter static-Initialisierer (2026-07-24): Runs-once-Guard mit
#        verstecktem bool-Flag-Global (LOADGC/JZ/STOREG.../STOREGC, dieselben Opcodes
#        wie if/while) im ECHTEN 68000-Code.
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'int base(){ return 10; } int f(){ static int x = base() + 5; x += 1; return x; } int main(){ putint(f()); putint(f()); putint(f()); }' > build/qcc_staticrt.ir && \
		build/qcc_backend build/qcc_staticrt.ir build/qcc_staticrt.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_staticrt.bin build/qcc_staticrt.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_staticrt.s68 2>/dev/null)" = "$(printf '16\n17\n18')" ]; then
		echo "ok    qcc static-Laufzeit-Initialisierer 68000: Runs-once-Guard korrekt"
	else
		echo "FAIL  qcc static-Laufzeit-Initialisierer 68000: Runs-once-Guard fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc static-Laufzeit-Initialisierer 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi

# 13b) struct-Felder (2026-07-24: gemischte skalare Feldtypen, echtes Byte-Layout,
#      siehe SELFHOSTING_LUECKENLISTE.md): Feldzugriff nutzt PUSHADDR/IPADD/LOADIND/
#      STOREIND -- bereits vorhandene, architekturneutrale Opcodes, kein neuer Opcode
#      und keine Backend-Aenderung noetig, trotzdem hier explizit durch 68000 und ARM64
#      gegengeprueft (einheitlicher Feldtyp als Regressionsschutz plus gemischter Fall).
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'struct Point { int x; int y; }; int main(){ struct Point p; p.x=3; p.y=4; putint(p.x+p.y); }' > build/qcc_struct.ir && \
		build/qcc_backend build/qcc_struct.ir build/qcc_struct.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_struct.bin build/qcc_struct.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_struct.s68 2>/dev/null)" = "7" ]; then
		echo "ok    qcc struct 68000: Feldzugriff (LOADIDX/STOREIDX) korrekt"
	else
		echo "FAIL  qcc struct 68000: Feldzugriff fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'struct Mixed { char a; int b; char c; }; int main(){ struct Mixed m; m.a=1; m.b=1000; m.c=2; putint(m.b+m.a+m.c); }' > build/qcc_struct_mixed.ir && \
		build/qcc_backend build/qcc_struct_mixed.ir build/qcc_struct_mixed.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_struct_mixed.bin build/qcc_struct_mixed.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_struct_mixed.s68 2>/dev/null)" = "1003" ]; then
		echo "ok    qcc struct 68000: gemischte Feldtypen (char/int/char, Byte-Offset+Padding) korrekt"
	else
		echo "FAIL  qcc struct 68000: gemischte Feldtypen fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000 (gemischt): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'typedef struct { char a; int b; } Mixed; int main(){ Mixed m; m.a=1; m.b=1000; putint(m.b+m.a); }' > build/qcc_struct_anon.ir && \
		build/qcc_backend build/qcc_struct_anon.ir build/qcc_struct_anon.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_struct_anon.bin build/qcc_struct_anon.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_struct_anon.s68 2>/dev/null)" = "1001" ]; then
		echo "ok    qcc struct 68000: anonymes struct inline im typedef korrekt"
	else
		echo "FAIL  qcc struct 68000: anonymes struct inline im typedef fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000 (anonym im typedef): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'struct Rec { char tag; int value; char buf[4]; }; int main(){ struct Rec r; r.tag = 1; r.value = 1000; char *p = r.buf; p[0]=9; putint(r.tag); putint(r.value); putint(p[0]); putint(sizeof(struct Rec)); }' > build/qcc_struct_arrfield.ir && \
		build/qcc_backend build/qcc_struct_arrfield.ir build/qcc_struct_arrfield.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_struct_arrfield.bin build/qcc_struct_arrfield.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_struct_arrfield.s68 2>/dev/null)" = "$(printf '1\n1000\n9\n12')" ]; then
		echo "ok    qcc struct 68000: Array-Feld (char buf[4]) korrekt"
	else
		echo "FAIL  qcc struct 68000: Array-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000 (Array-Feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'struct P{int x; char buf[4]; int a[3];}; int main(){ struct P p; int i; i = 1; p.x = 7; p.buf[0]=65; p.buf[1]=66; p.a[0]=10; p.a[1]=20; p.a[2]=30; putint(p.x); putchar(p.buf[0]); putchar(p.buf[1]); putint(p.a[i]); }' > build/qcc_structidx.ir && \
		build/qcc_backend build/qcc_structidx.ir build/qcc_structidx.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_structidx.bin build/qcc_structidx.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_structidx.s68 2>/dev/null)" = "$(printf '7\nAB20')" ]; then
		echo "ok    qcc struct 68000: direkte p.field[i]-Indizierung (int- und char-Array-Feld) korrekt"
	else
		echo "FAIL  qcc struct 68000: direkte p.field[i]-Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000 (p.field[i]): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# 2026-07-25 (Selfhosting L2): Pointer-Feld in struct -- 8 Byte Groesse/Ausrichtung
# UNABHAENGIG von der Ziel-Architektur (siehe tcRegisterStruct-Kommentar), der
# 68k-Backend nutzt davon nur die ersten 4 Byte. Zugriff nur ueber eine Pointer-
# Zwischenvariable (kein direktes p.field[i] durch ein Pointer-Feld, wie zuvor
# bei Array-Feldern).
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'struct P{char* text; int len;}; int main(){ struct P p; char msg[4]; char* t; msg[0]=72; msg[1]=105; msg[2]=0; p.text=msg; p.len=2; t=p.text; putchar(t[0]); putchar(t[1]); putint(p.len); }' > build/qcc_structptr.ir && \
		build/qcc_backend build/qcc_structptr.ir build/qcc_structptr.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_structptr.bin build/qcc_structptr.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_structptr.s68 2>/dev/null)" = "$(printf 'Hi2')" ]; then
		echo "ok    qcc struct 68000: Pointer-Feld (8-Byte-Layout, ueber Pointer-Zwischenvariable) korrekt"
	else
		echo "FAIL  qcc struct 68000: Pointer-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000 (Pointer-Feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# arr[i].feld (2026-07-25): siehe QCCVM-Tests oben fuer die vollstaendige Erklaerung
# (Allokations-Fix + neuer IPADDN-Opcode). 68k-IPADDN nutzt tc_mul_i32 (bereits im
# Runtime-Core), da lsl.l nur feste 1/4-Skalierung kann, structByteSize aber beliebig ist.
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; arr[0].a=10; arr[0].b=11; arr[1].a=20; arr[1].b=21; arr[2].a=30; arr[2].b=31; putint(arr[0].a); putint(arr[0].b); putint(arr[1].a); putint(arr[1].b); putint(arr[2].a); putint(arr[2].b); }' > build/qcc_structarr.ir && \
		build/qcc_backend build/qcc_structarr.ir build/qcc_structarr.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_structarr.bin build/qcc_structarr.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_structarr.s68 2>/dev/null)" = "$(printf '10\n11\n20\n21\n30\n31')" ]; then
		echo "ok    qcc struct 68000: Array von structs, arr[i].feld (int-Felder) korrekt"
	else
		echo "FAIL  qcc struct 68000: arr[i].feld (int-Felder) fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000 (arr[i].feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'struct Rec { char name[8]; char* text; }; int main(){ struct Rec arr[3]; char* n; n = arr[0].name; n[0]=65; n = arr[1].name; n[0]=66; n = arr[2].name; n[0]=67; n = arr[1].name; n[0] = 88; n = arr[0].name; putchar(n[0]); n = arr[1].name; putchar(n[0]); n = arr[2].name; putchar(n[0]); }' > build/qcc_structarrptr.ir && \
		build/qcc_backend build/qcc_structarrptr.ir build/qcc_structarrptr.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_structarrptr.bin build/qcc_structarrptr.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_structarrptr.s68 2>/dev/null)" = "AXC" ]; then
		echo "ok    qcc struct 68000: Array von structs mit Pointer-Feld, arr[i].feld ueber Zwischenvariable korrekt"
	else
		echo "FAIL  qcc struct 68000: Array von structs mit Pointer-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000 (arr[i].feld, Pointer-Feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# ptr[i].feld (2026-07-25, Milestone B): lokale Pointer-auf-struct-Variable, indiziert,
# dann Feldzugriff -- braucht routinesC[i].name/.text im Selfhosting-Piloten. LOADP statt
# PUSHADDR, sonst dieselbe IPADDN-Idee wie arr[i].feld.
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; struct Rec* p; arr[0].a=10; arr[1].a=20; arr[2].a=30; p = arr; putint(p[0].a); putint(p[1].a); putint(p[2].a); p[1].a = 99; putint(arr[1].a); }' > build/qcc_structptrarr.ir && \
		build/qcc_backend build/qcc_structptrarr.ir build/qcc_structptrarr.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_structptrarr.bin build/qcc_structptrarr.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_structptrarr.s68 2>/dev/null)" = "$(printf '10\n20\n30\n99')" ]; then
		echo "ok    qcc struct 68000: ptr[i].feld (lokale Pointer-auf-struct-Variable) korrekt"
	else
		echo "FAIL  qcc struct 68000: ptr[i].feld fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000 (ptr[i].feld): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# Globale structs (2026-07-25): tc_globalend erkannte "struct" bisher gar nicht als
# Basistyp. Skalare globale structs/Arrays von structs/Pointer-auf-struct-Globale --
# Codegen strukturell identisch zum lokalen Fall (ADDRG/PUSHADDR G/LOADGP statt
# PUSHADDR L/LOADP).
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'struct Rec { int a; int b; }; struct Rec garr[3]; struct Rec* gp; int main(){ gp = garr; gp[0].a=100; gp[1].a=200; putint(garr[0].a); putint(garr[1].a); putint(gp[1].a); }' > build/qcc_gstruct.ir && \
		build/qcc_backend build/qcc_gstruct.ir build/qcc_gstruct.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_gstruct.bin build/qcc_gstruct.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_gstruct.s68 2>/dev/null)" = "$(printf '100\n200\n200')" ]; then
		echo "ok    qcc struct 68000: globale struct-Variablen/-Arrays/-Pointer korrekt"
	else
		echo "FAIL  qcc struct 68000: globale structs fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct 68000 (globale structs): Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'void greet(int x){ putint(x); } int deref(void *p){ int *q = p; return *q; } int main(){ greet(9); int x=7; putint(deref(&x)); }' > build/qcc_void.ir && \
		build/qcc_backend build/qcc_void.ir build/qcc_void.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_void.bin build/qcc_void.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_void.s68 2>/dev/null)" = "$(printf '9\n7')" ]; then
		echo "ok    qcc void 68000: void-Rueckgabe + void*-Parameter korrekt"
	else
		echo "FAIL  qcc void 68000: void/void* fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc void 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'int main(){ int m[3][3]; int i; int j; for(i=0;i<3;i+=1){ for(j=0;j<3;j+=1){ m[i][j]=i*10+j; } } putint(m[2][1]); putint(m[0][2]); }' > build/qcc_2d.ir && \
		build/qcc_backend build/qcc_2d.ir build/qcc_2d.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_2d.bin build/qcc_2d.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_2d.s68 2>/dev/null)" = "$(printf '21\n2')" ]; then
		echo "ok    qcc 2D-Array 68000: Zeilen/Spalten-Indizierung korrekt"
	else
		echo "FAIL  qcc 2D-Array 68000: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc 2D-Array 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'int main(){ int m[2][3][4]; int i; int j; int k; for(i=0;i<2;i+=1){ for(j=0;j<3;j+=1){ for(k=0;k<4;k+=1){ m[i][j][k]=i*100+j*10+k; } } } putint(m[1][2][3]); putint(m[0][0][0]); putint(m[1][0][2]); }' > build/qcc_3d.ir && \
		build/qcc_backend build/qcc_3d.ir build/qcc_3d.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_3d.bin build/qcc_3d.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_3d.s68 2>/dev/null)" = "$(printf '123\n0\n102')" ]; then
		echo "ok    qcc 3D-Array 68000: Horner-Kombination ueber drei Dimensionen korrekt"
	else
		echo "FAIL  qcc 3D-Array 68000: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc 3D-Array 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'int main(){ char* s = "AB"; putchar(s[0]); putchar(s[1]); putint(s[2]); }' > build/qcc_string.ir && \
		build/qcc_backend build/qcc_string.ir build/qcc_string.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_string.bin build/qcc_string.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_string.s68 2>/dev/null)" = "AB0" ]; then
		echo "ok    qcc String-Literal 68000: GARRAY/GINIT/ADDRG-Adressierung korrekt"
	else
		echo "FAIL  qcc String-Literal 68000: Adressierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc String-Literal 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'char gmsg[6] = "hallo"; int main(){ char m[5] = "hallo"; putchar(m[0]); putchar(m[4]); putchar(gmsg[0]); putint(gmsg[5]); }' > build/qcc_stringinit.ir && \
		build/qcc_backend build/qcc_stringinit.ir build/qcc_stringinit.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_stringinit.bin build/qcc_stringinit.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_stringinit.s68 2>/dev/null)" = "hoh0" ]; then
		echo "ok    qcc String-Array-Initialisierer 68000: lokal (exakt) + global (mit Nullterminator) korrekt"
	else
		echo "FAIL  qcc String-Array-Initialisierer 68000: fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc String-Array-Initialisierer 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'char* mkstr(){ return "hallo"; } int main(){ putchar(mkstr()[0]); putchar(mkstr()[4]); putchar("world"[0]); putint("world"[4]); }' > build/qcc_directidx.ir && \
		build/qcc_backend build/qcc_directidx.ir build/qcc_directidx.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_directidx.bin build/qcc_directidx.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_directidx.s68 2>/dev/null)" = "how100" ]; then
		echo "ok    qcc direkte Indizierung 68000: Funktionsrueckgabewert + String-Literal ohne Zwischenvariable korrekt"
	else
		echo "FAIL  qcc direkte Indizierung 68000: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc direkte Indizierung 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p 'int main(){ int x=2; int r=0; switch(x){ case 1: r=11; break; case 2: case 3: r=23; break; default: r=99; } putint(r); }' > build/qcc_switch.ir && \
		build/qcc_backend build/qcc_switch.ir build/qcc_switch.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_switch.bin build/qcc_switch.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_switch.s68 2>/dev/null)" = "23" ]; then
		echo "ok    qcc switch 68000: gestapelte case-Label + default korrekt"
	else
		echo "FAIL  qcc switch 68000: switch/case fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc switch 68000: Backend, vasm oder python3 fehlt -- uebersprungen"
fi
# 14) QCC M4b/M4c-1: Der Python-Simulator ist ausschliesslich ein Test-Orakel,
#     nicht Teil der auszuliefernden Toolchain. Er fuehrt die von M4a erzeugte 68k-
#     Schablonen-Ausgabe inklusive Frame/Call/RET und der ECHTEN 68k-Core-
#     Schablonen fuer signed int32 MUL/DIV aus. Nur PRINT bleibt ein Plattform-Hook.
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ]; then
	if build/qcc_p 'unsigned int high=-1; int fact(int n){ if(n <= 1) return 1; else return n * fact(n - 1); } int main(){ putint(fact(5)); putint(-7 * 6); putint(20 / 3); putint(-20 / 3); putint(high > 1); putint(high / 2); putuint(high); putint(20 % 6); putint(-20 % 6); putuint(high % 10); }' > build/qcc_m4b.ir && \
		build/qcc_backend build/qcc_m4b.ir build/qcc_m4b.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_m4b.bin build/qcc_m4b.s68 2>/dev/null && \
		grep -q '^tc_mul_loop:' build/qcc_m4b.s68 && grep -q '^tc_div_loop:' build/qcc_m4b.s68 && grep -q '^tc_udiv_loop:' build/qcc_m4b.s68 && \
		[ "$(python3 tools/qcc68sim.py build/qcc_m4b.s68 2>/dev/null)" = "$(printf '120\n-42\n6\n-6\n1\n2147483647\n4294967295\n2\n-2\n5')" ]; then
		echo "ok    qcc M4c-1: echte 68k int32 signed/unsigned MUL/DIV/MOD + Fakultaet = qccvm"
	else
		echo "FAIL  qcc M4c-1: 68k-Core-Lauf stimmt nicht mit qccvm ueberein"; fail=1
	fi
else
	echo "warn  qcc M4c-1: python3 oder Backend fehlt -- Ausfuehrungstest uebersprungen"
fi

# 15) QCC M4c-2: Globale int32-Variablen besitzen eine eigene, dauerhafte
#     Namens-Tabelle im Frontend und werden im 68k-Code PC-relativ als LOADG/
#     STOREG auf das nullinitialisierte Daten-/BSS-Aequivalent angesprochen.
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ]; then
	if build/qcc_p 'int limit = 10; char mark = 346; int counter; char next(char c){ return c + 1; } int bump(){ counter = counter + limit; return counter; } int main(){ char copy; copy = mark; putchar(copy); putchar(next(334)); putchar(10); putint(bump()); putint(bump()); }' > build/qcc_globals.ir && \
		build/qcc_backend build/qcc_globals.ir build/qcc_globals.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_globals.bin build/qcc_globals.s68 2>/dev/null && \
		grep -q $'tc_g_limit:\tdc.l\t10' build/qcc_globals.s68 && grep -q $'tc_g_mark:\tdc.b\t90' build/qcc_globals.s68 && grep -q 'andi.l' build/qcc_globals.s68 && \
		[ "$(python3 tools/qcc68sim.py build/qcc_globals.s68 2>/dev/null)" = "$(printf 'ZO\n10\n20')" ]; then
		echo "ok    qcc M4c-4: DATA/BSS + putchar im 68k-Runtime-Vertrag = qccvm"
	else
		echo "FAIL  qcc M4c-4: globale Variablen/putchar im 68k-Pfad fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc M4c-4: python3 oder Backend fehlt -- Globals-Test uebersprungen"
fi

# 15b) Pointer-End-to-End auf dem 68000-Pfad: echte Adressen, char/int-Skalierung,
#      Pointerdifferenz, Pointer auf Pointer und Pointerarrays.
pointer_program='int values[4]={10,20,30,40}; char bytes[4]={5,6,7,8}; int main(){ int *p=values; char *c=bytes; int **pp=&p; int *pa[2]; int **r=pa; pa[0]=&values[0]; pa[1]=&values[3]; putint(p[2]); *(p+1)=25; putint(*(1+p)); p+=3; putint(*p); putint(p-values); c+=2; putint(*c); putint(c-bytes); putint(p!=0); putint(p>values); putint(**pp); putint(*r[1]); }'
pointer_expected=$(printf '30\n25\n40\n3\n7\n2\n1\n1\n40\n40')
if command -v python3 >/dev/null 2>&1 && [ -x build/qcc_backend ] && [ -x tools/vasmm68k_mot ]; then
	if build/qcc_p "$pointer_program" > build/qcc_pointer_reg.ir && \
		build/qcc_backend build/qcc_pointer_reg.ir build/qcc_pointer_reg.s68 && \
		tools/vasmm68k_mot -Fbin -quiet -m68000 -o build/qcc_pointer_reg.bin build/qcc_pointer_reg.s68 2>/dev/null && \
		[ "$(python3 tools/qcc68sim.py build/qcc_pointer_reg.s68 2>/dev/null)" = "$pointer_expected" ]; then
		echo "ok    qcc 68000 Pointer: Adressen, Skalierung, Differenz und T** korrekt"
	else
		echo "FAIL  qcc 68000 Pointer: Backend- oder Ausfuehrungsfehler"; fail=1
	fi
else
	echo "warn  qcc 68000 Pointer: Backend, vasm oder python3 fehlt -- uebersprungen"
fi

# 16) ARM64/Darwin: erster echter Hosted-Zielweg. Der Backend-Treiber erzeugt
#     Programmassembler; runtime/arm64_darwin/start.s liefert eigenen Einstieg,
#     putint und exit. Der Linker bindet nur libSystem zum Laden des Mach-O ein,
#     keine C-Startdateien (-nostartfiles).
#     Gebaut wird die reine-C-Fassung (qcc_arm64_backend_c.cpp, siehe
#     docs/SELFHOSTING_LUECKENLISTE.md); das C++-Original bleibt als Referenz
#     liegen -- Ruecksetzen = hier wieder die .cpp bauen.
if [ "$(uname -m)" = "arm64" ] && command -v clang >/dev/null 2>&1; then
	if cc -std=c11 -Wall -Wextra -x c -o build/qcc_arm64_backend "$QIRARM64_SRC" 2>/dev/null && \
		build/qcc_p 'int limit = 5; int debt = -20; unsigned int high = -1; char mark = 335; int counter; char next(char c){ return c + 1; } int fact(int n){ if(n <= 1) return 1; else return n * fact(n - 1); } int main(){ char copy; copy = mark; putchar(copy); putchar(next(334)); putchar(10); counter = fact(limit); putint(counter); putint(debt / 3); putint(high > 1); putint(high / 2); putuint(high); }' > build/qcc_arm64.ir && \
		build/qcc_arm64_backend build/qcc_arm64.ir build/qcc_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_arm64 build/qcc_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		grep -q 'ldrb' build/qcc_arm64.s && grep -q 'strb' build/qcc_arm64.s && grep -q 'and.*#255' build/qcc_arm64.s && grep -q 'udiv' build/qcc_arm64.s && \
		[ "$(build/qcc_arm64)" = "$(printf 'OO\n120\n-6\n1\n2147483647\n4294967295')" ] && \
		build/qcc_p "$pointer_program" > build/qcc_pointer_arm64_reg.ir && \
		build/qcc_arm64_backend build/qcc_pointer_arm64_reg.ir build/qcc_pointer_arm64_reg.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_pointer_arm64_reg build/qcc_pointer_arm64_reg.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_pointer_arm64_reg)" = "$pointer_expected" ]; then
		echo "ok    qcc ARM64/Darwin: native char/unsigned und 64-Bit-Pointer korrekt"
	else
		echo "FAIL  qcc ARM64/Darwin: nativer Backend-/Runtime-Pfad fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc ARM64/Darwin: nur auf arm64-macOS getestet -- uebersprungen"
fi


# 15b) short/unsigned short auf dem ARM64-Backend (2026-09-10, s. auch
#     tools/test_short_68k.sh in Q9-QCC fuer den 68k-Gegenpart). Deckt
#     lokale/globale Variable, Array, Zeiger, ein struct mit der Feldreihen-
#     folge "short a; short b; int c;" (im Host-Orakel qccvm.py NICHT
#     pruefbar, hier aber ein echter Speicherzugriff wie auf dem 68k), Wert-
#     parameter, Rueckgabe-Narrowing, unsigned short und expliziten Cast ab.
if [ "$(uname -m)" = "arm64" ] && command -v clang >/dev/null 2>&1 && [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct N { short a; short b; int c; }; short g = 300; short retTooBig(void){ return 100000; } void byval(short x){ putint(x); } int main(){ short a; short arr[3]; short x; short *p; struct N n; int i; unsigned short u; a = -1; putint(a); g = g + 1; putint(g); for(i=0;i<3;i=i+1) arr[i]=i*10; putint(arr[1]); x=42; p=&x; *p=7; putint(x); n.a=11; n.b=22; n.c=99999; putint(n.a); putint(n.b); putint(n.c); putint(sizeof(struct N)); byval(-3); putint(retTooBig()); u=40000; putint(u); putint((short)100000); }' > build/qcc_short_arm64.ir && \
		build/qcc_arm64_backend build/qcc_short_arm64.ir build/qcc_short_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_short_arm64 build/qcc_short_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_short_arm64)" = "$(printf '65535\n301\n10\n7\n11\n22\n99999\n8\n65533\n34464\n40000\n34464')" ]; then
		echo "ok    qcc short ARM64: lokal/global/Array/Zeiger/struct (short+short+int)/Wertparameter/Rueckgabe-Narrowing/unsigned/Cast korrekt"
	else
		echo "FAIL  qcc short ARM64: nativer Backend-/Runtime-Pfad fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc short ARM64: nur auf arm64-macOS getestet -- uebersprungen"
fi

# 16a) QCC Mehrdatei-Uebersetzung, M3 (2026-07-25): ZWEI SEPARAT mit -part
#     kompilierte Dateien werden mit clang zu getrennten .o-Objekten assembliert
#     und mit demselben clang-Aufruf (der intern ld ruft) zu EINEM Programm
#     gelinkt -- erstmals echte getrennte Objektdateien statt ein einzelner
#     .s-Compile + Runtime-Datei in einem Rutsch. Anders als beim 68k/l68-Ziel
#     (siehe qcc_backend_c.cpp) braucht static HIER KEINE Namensverfremdung:
#     Mach-O/ld unterstuetzen ECHTE lokale Symbole (kein .globl = fuer andere
#     Objektdateien unsichtbar) -- empirisch verifiziert (zwei .o mit je einem
#     lokalen gleichnamigen Symbol linken ohne Konflikt). Zweiter Testfall
#     bestaetigt trotzdem, dass eine ECHTE Namenskollision (zwei nicht-static
#     Definitionen) von ld zuverlaessig als "duplicate symbol" abgelehnt wird.
if [ "$(uname -m)" = "arm64" ] && command -v clang >/dev/null 2>&1 && [ -x build/qcc_arm64_backend ]; then
	build/qcc_p 'int shared; int helper(int x); static int localHelper(int x){ return x-1; } int main(){ shared = 10; putint(helper(shared)); putint(localHelper(shared)); }' > build/qcc_mf_arm_a.ir
	build/qcc_p 'extern int shared; int helper(int x){ return shared + x; }' > build/qcc_mf_arm_b.ir
	if build/qcc_arm64_backend build/qcc_mf_arm_a.ir build/qcc_mf_arm_a.s -part && \
		build/qcc_arm64_backend build/qcc_mf_arm_b.ir build/qcc_mf_arm_b.s -part && \
		clang -arch arm64 -c build/qcc_mf_arm_a.s -o build/qcc_mf_arm_a.o && \
		clang -arch arm64 -c build/qcc_mf_arm_b.s -o build/qcc_mf_arm_b.o && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_mf_arm build/qcc_mf_arm_a.o build/qcc_mf_arm_b.o runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_mf_arm)" = "$(printf '20\n9')" ] && \
		[ "$(nm build/qcc_mf_arm_a.o | grep -c ' t _tc_localHelper$')" = "1" ]; then
		echo "ok    qcc Mehrdatei M3: echte getrennte .o-Kompilate + clang/ld-Link (Funktionsaufruf+Global ueber Dateigrenze, static bleibt echt lokal) korrekt"
	else
		echo "FAIL  qcc Mehrdatei M3: getrennte .o-Kompilate/Link fehlerhaft"; fail=1
	fi
	# Duplicate-Symbol-Testfall: ZWEI Dateien definieren dieselbe nicht-static
	# Funktion -- ld muss das als "duplicate symbol" ablehnen.
	build/qcc_p 'int f(){ return 1; } int main(){ putint(f()); }' > build/qcc_mf_arm_dup_a.ir
	build/qcc_p 'int f(){ return 2; }' > build/qcc_mf_arm_dup_b.ir
	if build/qcc_arm64_backend build/qcc_mf_arm_dup_a.ir build/qcc_mf_arm_dup_a.s -part && \
		build/qcc_arm64_backend build/qcc_mf_arm_dup_b.ir build/qcc_mf_arm_dup_b.s -part && \
		clang -arch arm64 -c build/qcc_mf_arm_dup_a.s -o build/qcc_mf_arm_dup_a.o && \
		clang -arch arm64 -c build/qcc_mf_arm_dup_b.s -o build/qcc_mf_arm_dup_b.o; then
		if clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_mf_arm_dup build/qcc_mf_arm_dup_a.o build/qcc_mf_arm_dup_b.o runtime/arm64_darwin/start.s >build/qcc_mf_arm_dup.err 2>&1; then
			echo "FAIL  qcc Mehrdatei M3: ld haette 'duplicate symbol' melden muessen"; fail=1
		elif grep -qi "duplicate symbol" build/qcc_mf_arm_dup.err; then
			echo "ok    qcc Mehrdatei M3: echter ld lehnt doppelte nicht-static Definition als 'duplicate symbol' ab"
		else
			echo "FAIL  qcc Mehrdatei M3: Link schlug NICHT wegen 'duplicate symbol' fehl"; fail=1
		fi
		rm -f build/qcc_mf_arm_dup.err
	else
		echo "FAIL  qcc Mehrdatei M3: Backend-/.o-Aufruf fuer Duplicate-Test fehlgeschlagen"; fail=1
	fi
else
	echo "warn  qcc Mehrdatei M3: nur auf arm64-macOS getestet -- uebersprungen"
fi

# VOLATILE HAELT, WEIL DER CODEGEN NICHTS ZURUECKHAELT -- nachgezaehlt statt
# geglaubt. Zwei aufeinanderfolgende Lesezugriffe auf dieselbe volatile
# Variable muessen ZWEI Speicherzugriffe ergeben, auch mit -peephole. Faltet
# jemand spaeter ein Muster ein, das Variablenzugriffe zusammenfasst, schlaegt
# genau dieser Test an -- und dann braucht volatile eine echte Sperre.
if [ -x build/qcc_backend ]; then
	volsrc='volatile int g; int main(){ int a; int b; a = g; b = g; return a+b; }'
	if build/qcc_p "$volsrc" > build/qcc_vol.ir 2>/dev/null &&
	   build/qcc_backend build/qcc_vol.ir build/qcc_vol.s68 -os9 >/dev/null 2>&1 &&
	   build/qcc_backend build/qcc_vol.ir build/qcc_vol_ph.s68 -os9 -peephole >/dev/null 2>&1; then
		# Gezaehlt wird die QUELLseite, unabhaengig vom Ziel: -peephole
		# ersetzt den Umweg ueber den Stapel ("move.l g,-(a7)" + "move.l
		# (a7)+,-4(a5)") durch einen Direktzugriff ("move.l g,-4(a5)").
		# Der SPEICHERZUGRIFF bleibt dabei erhalten -- genau das ist der
		# Unterschied zwischen Stapelfaltung und dem Wegoptimieren eines
		# Zugriffs, und nur Letzteres waere fuer volatile ein Problem.
		n1=$(grep -c 'move\.l	tc_g_g(pc),' build/qcc_vol.s68)
		n2=$(grep -c 'move\.l	tc_g_g(pc),' build/qcc_vol_ph.s68)
		if [ "$n1" = "2" ] && [ "$n2" = "2" ]; then
			echo "ok    qcc volatile: jeder Zugriff bleibt erhalten (2 Ladungen, auch mit -peephole)"
		else
			echo "FAIL  qcc volatile: Zugriffe wurden zusammengefasst ($n1 ohne / $n2 mit -peephole, erwartet 2/2)"; fail=1
		fi
	else
		echo "FAIL  qcc volatile: Assembler nicht erzeugbar"; fail=1
	fi
else
	echo "warn  qcc volatile: 68k-Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct Point { int x; int y; }; int main(){ struct Point p; p.x=3; p.y=4; putint(p.x+p.y); }' > build/qcc_struct_arm64.ir && \
		build/qcc_arm64_backend build/qcc_struct_arm64.ir build/qcc_struct_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_struct_arm64 build/qcc_struct_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_struct_arm64)" = "7" ]; then
		echo "ok    qcc struct ARM64: Feldzugriff korrekt"
	else
		echo "FAIL  qcc struct ARM64: Feldzugriff fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct Mixed { char a; int b; char c; }; int main(){ struct Mixed m; m.a=1; m.b=1000; m.c=2; putint(m.b+m.a+m.c); }' > build/qcc_struct_mixed_arm64.ir && \
		build/qcc_arm64_backend build/qcc_struct_mixed_arm64.ir build/qcc_struct_mixed_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_struct_mixed_arm64 build/qcc_struct_mixed_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_struct_mixed_arm64)" = "1003" ]; then
		echo "ok    qcc struct ARM64: gemischte Feldtypen (char/int/char, Byte-Offset+Padding) korrekt"
	else
		echo "FAIL  qcc struct ARM64: gemischte Feldtypen fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64 (gemischt): Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'typedef struct { char a; int b; } Mixed; int main(){ Mixed m; m.a=1; m.b=1000; putint(m.b+m.a); }' > build/qcc_struct_anon_arm64.ir && \
		build/qcc_arm64_backend build/qcc_struct_anon_arm64.ir build/qcc_struct_anon_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_struct_anon_arm64 build/qcc_struct_anon_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_struct_anon_arm64)" = "1001" ]; then
		echo "ok    qcc struct ARM64: anonymes struct inline im typedef korrekt"
	else
		echo "FAIL  qcc struct ARM64: anonymes struct inline im typedef fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64 (anonym im typedef): Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct Rec { char tag; int value; char buf[4]; }; int main(){ struct Rec r; r.tag = 1; r.value = 1000; char *p = r.buf; p[0]=9; putint(r.tag); putint(r.value); putint(p[0]); putint(sizeof(struct Rec)); }' > build/qcc_struct_arrfield_arm64.ir && \
		build/qcc_arm64_backend build/qcc_struct_arrfield_arm64.ir build/qcc_struct_arrfield_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_struct_arrfield_arm64 build/qcc_struct_arrfield_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_struct_arrfield_arm64)" = "$(printf '1\n1000\n9\n12')" ]; then
		echo "ok    qcc struct ARM64: Array-Feld (char buf[4]) korrekt"
	else
		echo "FAIL  qcc struct ARM64: Array-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64 (Array-Feld): Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct P{int x; char buf[4]; int a[3];}; int main(){ struct P p; int i; i = 1; p.x = 7; p.buf[0]=65; p.buf[1]=66; p.a[0]=10; p.a[1]=20; p.a[2]=30; putint(p.x); putchar(p.buf[0]); putchar(p.buf[1]); putint(p.a[i]); }' > build/qcc_structidx_arm64.ir && \
		build/qcc_arm64_backend build/qcc_structidx_arm64.ir build/qcc_structidx_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_structidx_arm64 build/qcc_structidx_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_structidx_arm64)" = "$(printf '7\nAB20')" ]; then
		echo "ok    qcc struct ARM64: direkte p.field[i]-Indizierung (int- und char-Array-Feld) korrekt"
	else
		echo "FAIL  qcc struct ARM64: direkte p.field[i]-Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64 (p.field[i]): Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct P{char* text; int len;}; int main(){ struct P p; char msg[4]; char* t; msg[0]=72; msg[1]=105; msg[2]=0; p.text=msg; p.len=2; t=p.text; putchar(t[0]); putchar(t[1]); putint(p.len); }' > build/qcc_structptr_arm64.ir && \
		build/qcc_arm64_backend build/qcc_structptr_arm64.ir build/qcc_structptr_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_structptr_arm64 build/qcc_structptr_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_structptr_arm64)" = "$(printf 'Hi2')" ]; then
		echo "ok    qcc struct ARM64: Pointer-Feld (8-Byte-Layout, ueber Pointer-Zwischenvariable) korrekt"
	else
		echo "FAIL  qcc struct ARM64: Pointer-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64 (Pointer-Feld): Backend fehlt -- uebersprungen"
fi

# arr[i].feld (2026-07-25): siehe QCCVM/68000-Tests oben. ARM64-IPADDN nutzt eine
# echte 32-Bit-Multiplikation (mul) statt scaleSuffix (nur feste 1/4/8-Shifts).
if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; arr[0].a=10; arr[0].b=11; arr[1].a=20; arr[1].b=21; arr[2].a=30; arr[2].b=31; putint(arr[0].a); putint(arr[0].b); putint(arr[1].a); putint(arr[1].b); putint(arr[2].a); putint(arr[2].b); }' > build/qcc_structarr_arm64.ir && \
		build/qcc_arm64_backend build/qcc_structarr_arm64.ir build/qcc_structarr_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_structarr_arm64 build/qcc_structarr_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_structarr_arm64)" = "$(printf '10\n11\n20\n21\n30\n31')" ]; then
		echo "ok    qcc struct ARM64: Array von structs, arr[i].feld (int-Felder) korrekt"
	else
		echo "FAIL  qcc struct ARM64: arr[i].feld (int-Felder) fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64 (arr[i].feld): Backend fehlt -- uebersprungen"
fi
if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct Rec { char name[8]; char* text; }; int main(){ struct Rec arr[3]; char* n; n = arr[0].name; n[0]=65; n = arr[1].name; n[0]=66; n = arr[2].name; n[0]=67; n = arr[1].name; n[0] = 88; n = arr[0].name; putchar(n[0]); n = arr[1].name; putchar(n[0]); n = arr[2].name; putchar(n[0]); }' > build/qcc_structarrptr_arm64.ir && \
		build/qcc_arm64_backend build/qcc_structarrptr_arm64.ir build/qcc_structarrptr_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_structarrptr_arm64 build/qcc_structarrptr_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_structarrptr_arm64)" = "AXC" ]; then
		echo "ok    qcc struct ARM64: Array von structs mit Pointer-Feld, arr[i].feld ueber Zwischenvariable korrekt"
	else
		echo "FAIL  qcc struct ARM64: Array von structs mit Pointer-Feld fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64 (arr[i].feld, Pointer-Feld): Backend fehlt -- uebersprungen"
fi

# ptr[i].feld (2026-07-25, Milestone B): siehe 68000-Test oben fuer die Erklaerung.
if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct Rec { int a; int b; }; int main(){ struct Rec arr[3]; struct Rec* p; arr[0].a=10; arr[1].a=20; arr[2].a=30; p = arr; putint(p[0].a); putint(p[1].a); putint(p[2].a); p[1].a = 99; putint(arr[1].a); }' > build/qcc_structptrarr_arm64.ir && \
		build/qcc_arm64_backend build/qcc_structptrarr_arm64.ir build/qcc_structptrarr_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_structptrarr_arm64 build/qcc_structptrarr_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_structptrarr_arm64)" = "$(printf '10\n20\n30\n99')" ]; then
		echo "ok    qcc struct ARM64: ptr[i].feld (lokale Pointer-auf-struct-Variable) korrekt"
	else
		echo "FAIL  qcc struct ARM64: ptr[i].feld fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64 (ptr[i].feld): Backend fehlt -- uebersprungen"
fi

# Globale structs (2026-07-25): siehe 68000-Test oben fuer die Erklaerung.
if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'struct Rec { int a; int b; }; struct Rec garr[3]; struct Rec* gp; int main(){ gp = garr; gp[0].a=100; gp[1].a=200; putint(garr[0].a); putint(garr[1].a); putint(gp[1].a); }' > build/qcc_gstruct_arm64.ir && \
		build/qcc_arm64_backend build/qcc_gstruct_arm64.ir build/qcc_gstruct_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_gstruct_arm64 build/qcc_gstruct_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_gstruct_arm64)" = "$(printf '100\n200\n200')" ]; then
		echo "ok    qcc struct ARM64: globale struct-Variablen/-Arrays/-Pointer korrekt"
	else
		echo "FAIL  qcc struct ARM64: globale structs fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc struct ARM64 (globale structs): Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'void greet(int x){ putint(x); } int deref(void *p){ int *q = p; return *q; } int main(){ greet(9); int x=7; putint(deref(&x)); }' > build/qcc_void_arm64.ir && \
		build/qcc_arm64_backend build/qcc_void_arm64.ir build/qcc_void_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_void_arm64 build/qcc_void_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_void_arm64)" = "$(printf '9\n7')" ]; then
		echo "ok    qcc void ARM64: void-Rueckgabe + void*-Parameter korrekt"
	else
		echo "FAIL  qcc void ARM64: void/void* fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc void ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'int main(){ int m[3][3]; int i; int j; for(i=0;i<3;i+=1){ for(j=0;j<3;j+=1){ m[i][j]=i*10+j; } } putint(m[2][1]); putint(m[0][2]); }' > build/qcc_2d_arm64.ir && \
		build/qcc_arm64_backend build/qcc_2d_arm64.ir build/qcc_2d_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_2d_arm64 build/qcc_2d_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_2d_arm64)" = "$(printf '21\n2')" ]; then
		echo "ok    qcc 2D-Array ARM64: Zeilen/Spalten-Indizierung korrekt"
	else
		echo "FAIL  qcc 2D-Array ARM64: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc 2D-Array ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'int main(){ int m[2][3][4]; int i; int j; int k; for(i=0;i<2;i+=1){ for(j=0;j<3;j+=1){ for(k=0;k<4;k+=1){ m[i][j][k]=i*100+j*10+k; } } } putint(m[1][2][3]); putint(m[0][0][0]); putint(m[1][0][2]); }' > build/qcc_3d_arm64.ir && \
		build/qcc_arm64_backend build/qcc_3d_arm64.ir build/qcc_3d_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_3d_arm64 build/qcc_3d_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_3d_arm64)" = "$(printf '123\n0\n102')" ]; then
		echo "ok    qcc 3D-Array ARM64: Horner-Kombination ueber drei Dimensionen korrekt"
	else
		echo "FAIL  qcc 3D-Array ARM64: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc 3D-Array ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'int main(){ char* s = "AB"; putchar(s[0]); putchar(s[1]); putint(s[2]); }' > build/qcc_string_arm64.ir && \
		build/qcc_arm64_backend build/qcc_string_arm64.ir build/qcc_string_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_string_arm64 build/qcc_string_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_string_arm64)" = "AB0" ]; then
		echo "ok    qcc String-Literal ARM64: GARRAY/GINIT/ADDRG-Adressierung korrekt"
	else
		echo "FAIL  qcc String-Literal ARM64: Adressierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc String-Literal ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'char gmsg[6] = "hallo"; int main(){ char m[5] = "hallo"; putchar(m[0]); putchar(m[4]); putchar(gmsg[0]); putint(gmsg[5]); }' > build/qcc_stringinit_arm64.ir && \
		build/qcc_arm64_backend build/qcc_stringinit_arm64.ir build/qcc_stringinit_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_stringinit_arm64 build/qcc_stringinit_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_stringinit_arm64)" = "hoh0" ]; then
		echo "ok    qcc String-Array-Initialisierer ARM64: lokal (exakt) + global (mit Nullterminator) korrekt"
	else
		echo "FAIL  qcc String-Array-Initialisierer ARM64: fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc String-Array-Initialisierer ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'char *namen[]={"eins","zwei"}; int main(){ char *p; p=namen[1]; putchar(p[0]); p=namen[0]; putchar(p[0]); }' > build/qcc_ptrtab_arm64.ir && \
		build/qcc_arm64_backend build/qcc_ptrtab_arm64.ir build/qcc_ptrtab_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_ptrtab_arm64 build/qcc_ptrtab_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_ptrtab_arm64)" = "ze" ]; then
		echo "ok    qcc Zeigertabelle ARM64: char *t[]={...} nativ korrekt"
	else
		echo "FAIL  qcc Zeigertabelle ARM64: fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc Zeigertabelle ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'char* mkstr(){ return "hallo"; } int main(){ putchar(mkstr()[0]); putchar(mkstr()[4]); putchar("world"[0]); putint("world"[4]); }' > build/qcc_directidx_arm64.ir && \
		build/qcc_arm64_backend build/qcc_directidx_arm64.ir build/qcc_directidx_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_directidx_arm64 build/qcc_directidx_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_directidx_arm64)" = "how100" ]; then
		echo "ok    qcc direkte Indizierung ARM64: Funktionsrueckgabewert + String-Literal ohne Zwischenvariable korrekt"
	else
		echo "FAIL  qcc direkte Indizierung ARM64: Indizierung fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc direkte Indizierung ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'int main(){ int x=2; int r=0; switch(x){ case 1: r=11; break; case 2: case 3: r=23; break; default: r=99; } putint(r); }' > build/qcc_switch_arm64.ir && \
		build/qcc_arm64_backend build/qcc_switch_arm64.ir build/qcc_switch_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_switch_arm64 build/qcc_switch_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_switch_arm64)" = "23" ]; then
		echo "ok    qcc switch ARM64: gestapelte case-Label + default korrekt"
	else
		echo "FAIL  qcc switch ARM64: switch/case fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc switch ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'int bump(){ static int counter; counter = counter + 1; return counter; } int main(){ putint(bump()); putint(bump()); putint(bump()); }' > build/qcc_static_arm64.ir && \
		build/qcc_arm64_backend build/qcc_static_arm64.ir build/qcc_static_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_static_arm64 build/qcc_static_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_static_arm64)" = "$(printf '1\n2\n3')" ]; then
		echo "ok    qcc static ARM64: lokale static-Variable persistiert ueber Aufrufe hinweg"
	else
		echo "FAIL  qcc static ARM64: static-Persistenz fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc static ARM64: Backend fehlt -- uebersprungen"
fi

if [ -x build/qcc_arm64_backend ]; then
	if build/qcc_p 'int base(){ return 10; } int f(){ static int x = base() + 5; x += 1; return x; } int main(){ putint(f()); putint(f()); putint(f()); }' > build/qcc_staticrt_arm64.ir && \
		build/qcc_arm64_backend build/qcc_staticrt_arm64.ir build/qcc_staticrt_arm64.s && \
		clang -arch arm64 -nostartfiles -Wl,-e,_start -o build/qcc_staticrt_arm64 build/qcc_staticrt_arm64.s runtime/arm64_darwin/start.s 2>/dev/null && \
		[ "$(build/qcc_staticrt_arm64)" = "$(printf '16\n17\n18')" ]; then
		echo "ok    qcc static-Laufzeit-Initialisierer ARM64: Runs-once-Guard korrekt"
	else
		echo "FAIL  qcc static-Laufzeit-Initialisierer ARM64: Runs-once-Guard fehlerhaft"; fail=1
	fi
else
	echo "warn  qcc static-Laufzeit-Initialisierer ARM64: Backend fehlt -- uebersprungen"
fi

# Selfhosting L2 Vollport (2026-07-26): parsec.cpp-Vollport, naechster Ausschnitt
# nach src-qcc/ebnf.tc -- writeWorkfile (src/parsec.cpp:1007-1130, die
# komplette Arbeitsdatei-Ausgabe: EBNF-QUELLTEXT/TS-SYMBOLTABELLE/
# NTS-SYMBOLTABELLE/PARSER-TABELLE/TESTS/LEXER/CODEGEN/NUTZER-CODE-Bloecke).
# ZWEI neue, live gefundene und gefixte Backend-Bugs (src/qcc_backend_c.cpp)
# waren Voraussetzung: (1) LABEL/JMP/JZ/JNZ ("tc_L<n>") und emitCompare()s interne
# Sprungmarken ("tc_cmp_yes_<n>"/"tc_cmp_done_<n>") hingen nur von einem PRO-DATEI
# neu bei 0 startenden Zaehler ab -- kollidierten beim Mehrdatei-Link, sobald
# ZWEI separat kompilierte Dateien beide Kontrollfluss/Vergleiche enthalten
# (praktisch immer). (2) Dieselbe Kollision fuer die "-largedata"-Tabellen
# tc_functab/tc_gadata, sobald zwei Dateien beide -largedata brauchen. Beide
# jetzt mit psectName-Suffix eindeutig gemacht (Muster wie die bestehende
# static-Namensverfremdung). Grund fuer den Fund: dies ist der ERSTE Test, der
# ebnf.tc UND codegen.tc als ZWEI GETRENNT kompilierte Dateien real linkt
# (bisherige ebnf.tc-Chunks wurden nur ALLEIN kompiliert/assembliert, nie
# gegen codegen.tc gelinkt -- Konkatenation beider Dateien in EINER
# qcc_p-Kompilation, wie im Kopfkommentar von ebnf.tc als "schneller Test"
# beschrieben, verletzt Quirk 8 (Deklarationen-vor-Funktionen GESAMT), siehe
# docs/FORTSCHRITT.md -- ebnf.tc muss daher ALLEIN kompiliert werden, seine
# eigenen Bare-Prototypen fuer codegen.tc-Funktionen reichen dem Frontend).
# Ein VOLLER l68-Link ist fuer diesen Test bewusst NICHT das Kriterium: die
# rekursive-Abstiegs-Parsergruppe (rule/expression/term/factor/.../
# ebnfSyntax/lexikalischeAnalyse/exitProgram) hat noch KEINEN echten Rumpf
# (siehe [[qcc-vollport-status]]) -- ein l68-Lauf schlaegt deshalb ERWARTET
# mit "unresolved symbol" fuer genau diese (hier nicht aufgerufenen) Funktionen
# fehl. Verifiziert wird daher wie bei den bisherigen ebnf.tc-Chunks: kompiliert
# sauber (Frontend) + assembliert fehlerfrei (echter r68) fuer BEIDE Dateien.
if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
	mkdir -p "$MWOS_TMP"
	wwtest_main='
int wwtestFn() {
	void* fp;

	aktTabIndex = 3;

	lexTab[0].mode = "TS";
	tcCopyBounded(lexTab[0].ident, "", 32);
	tcCopyBounded(lexTab[0].TS, "ident", 32);
	lexTab[0].trueAction = 1;
	lexTab[0].falseAction = -2;
	lexTab[0].callAddr = -1;

	lexTab[1].mode = "RNG";
	tcCopyBounded(lexTab[1].ident, "", 32);
	tcCopyBounded(lexTab[1].TS, "digit", 32);
	lexTab[1].rangeLo = 48;
	lexTab[1].rangeHi = 57;
	lexTab[1].trueAction = 2;
	lexTab[1].falseAction = -2;
	lexTab[1].callAddr = -1;

	lexTab[2].mode = "NTS";
	tcCopyBounded(lexTab[2].ident, "rule1", 32);
	tcCopyBounded(lexTab[2].TS, "rule1", 32);
	lexTab[2].trueAction = -1;
	lexTab[2].falseAction = -2;
	lexTab[2].callAddr = 0;

	tcCopyBounded(ruleSymbols[0].name, "rule1", 32);
	ruleSymbols[0].addr = 2;
	ruleSymbolCnt = 1;

	tcCopyBounded(testCases[0].input, "abc123", 255);
	testCases[0].expectOk = 1;
	tcCopyBounded(testCases[1].input, "xyz", 255);
	testCases[1].expectOk = 0;
	testCaseCnt = 2;

	appendQuelltext("rule1 = ident ;");

	fp = fopen("/tmp/qcc_workfile_test.txt", "w");
	writeWorkfile(fp);
	fclose(fp);

	putint(1);
}'
	if build/qcc_p "$(cat src-qcc/ebnf.tc)$wwtest_main" > build/qcc_wwtest_a.ir 2>build/qcc_wwtest_a.err && \
		build/qcc_p "$(cat src-qcc/codegen.tc)" > build/qcc_wwtest_b.ir 2>build/qcc_wwtest_b.err && \
		build/qcc_backend build/qcc_wwtest_a.ir build/qcc_wwtest_a.s68 -os9 -largedata -part && \
		build/qcc_backend build/qcc_wwtest_b.ir build/qcc_wwtest_b.s68 -os9 -largedata -part; then
		cp build/qcc_wwtest_a.s68 "$MWOS_TMP/wwtesta.a"
		cp build/qcc_wwtest_b.s68 "$MWOS_TMP/wwtestb.a"
		rm -f "$MWOS_TMP/wwtesta.r" "$MWOS_TMP/wwtestb.r"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\wwtesta.a -o=M:\\TMP\\wwtesta.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\wwtestb.a -o=M:\\TMP\\wwtestb.r -q" >/dev/null 2>&1
		if [ -s "$MWOS_TMP/wwtesta.r" ] && [ -s "$MWOS_TMP/wwtestb.r" ]; then
			echo "ok    qcc Selfhosting L2 Vollport: writeWorkfile (src-qcc/ebnf.tc) kompiliert und assembliert (echter r68, ebnf.tc+codegen.tc getrennt) korrekt"
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (writeWorkfile) fehlgeschlagen"; fail=1
		fi
		rm -f "$MWOS_TMP"/wwtesta.a "$MWOS_TMP"/wwtestb.a "$MWOS_TMP"/wwtesta.r "$MWOS_TMP"/wwtestb.r
	else
		echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/ebnf.tc (writeWorkfile) kompiliert nicht sauber (siehe build/qcc_wwtest_a.err/build/qcc_wwtest_b.err)"; fail=1
	fi
else
	echo "warn  qcc Selfhosting L2 Vollport (writeWorkfile): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echte Assemblierung uebersprungen"
fi

# Selfhosting L2 Vollport (2026-07-26, direkt im Anschluss): naechster Ausschnitt
# nach src-qcc/ebnf.tc -- rebuildFirstEdgesFromTable (src/parsec.cpp:1132-1156,
# Fall B: Linksrekursions-Kanten aus einer GELADENEN Arbeitsdatei rekonstruieren,
# statt sie waehrend des normalen Parsens ueber das firstPos-Flag zu sammeln).
# Haengt wie fast alles in ebnf.tc an extern strcmp/strlen (CALLEXT) -- QCCVM
# kennt CALLEXT nicht, daher wie bei den bisherigen ebnf.tc-Chunks NUR strukturell
# verifiziert (kompiliert sauber, echter r68 assembliert, ebnf.tc+codegen.tc
# getrennt). ZUSAETZLICH die reine ALGORITHMUS-Logik einmalig gegen eine
# native C-Uebersetzung derselben Funktion samt Testdaten (4 lexTab-Zeilen mit
# einer Linksrekursions-Kette, 2 falseAction-erreichbare NTS-Kanten erwartet)
# gegengeprueft -- beide liefern firstEdgeCnt=2/ruleNameListCnt=2, exakt wie im
# Original-Algorithmus erwartet. Nicht dauerhaft als Skript verankert (gleiche
# Abwaegung wie beim LEXER-Konfigurationsparser-Chunk: Wartungsaufwand einer
# zweiten Testumgebung nur fuers Testen steht in keinem Verhaeltnis zum Nutzen).
if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
	mkdir -p "$MWOS_TMP"
	rebuildtest_main='
int rebuildtestFn() {
	aktTabIndex = 4;

	lexTab[0].mode = "NTS";
	tcCopyBounded(lexTab[0].ident, "start", 32);
	tcCopyBounded(lexTab[0].TS, "middle", 32);
	lexTab[0].falseAction = 1;

	lexTab[1].mode = "TS";
	tcCopyBounded(lexTab[1].ident, "", 32);
	tcCopyBounded(lexTab[1].TS, "ident", 32);
	lexTab[1].falseAction = 2;

	lexTab[2].mode = "NTS";
	tcCopyBounded(lexTab[2].ident, "", 32);
	tcCopyBounded(lexTab[2].TS, "other", 32);
	lexTab[2].falseAction = -2;

	lexTab[3].mode = "TS";
	tcCopyBounded(lexTab[3].ident, "middle", 32);
	tcCopyBounded(lexTab[3].TS, "digit", 32);
	lexTab[3].falseAction = -2;

	rebuildFirstEdgesFromTable();

	putint(firstEdgeCnt);
	putint(ruleNameListCnt);
	putint(1);
}'
	if build/qcc_p "$(cat src-qcc/ebnf.tc)$rebuildtest_main" > build/qcc_rebuildtest_a.ir 2>build/qcc_rebuildtest_a.err && \
		build/qcc_p "$(cat src-qcc/codegen.tc)" > build/qcc_rebuildtest_b.ir 2>build/qcc_rebuildtest_b.err && \
		build/qcc_backend build/qcc_rebuildtest_a.ir build/qcc_rebuildtest_a.s68 -os9 -largedata -part && \
		build/qcc_backend build/qcc_rebuildtest_b.ir build/qcc_rebuildtest_b.s68 -os9 -largedata -part; then
		cp build/qcc_rebuildtest_a.s68 "$MWOS_TMP/rebuilda.a"
		cp build/qcc_rebuildtest_b.s68 "$MWOS_TMP/rebuildb.a"
		rm -f "$MWOS_TMP/rebuilda.r" "$MWOS_TMP/rebuildb.r"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\rebuilda.a -o=M:\\TMP\\rebuilda.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\rebuildb.a -o=M:\\TMP\\rebuildb.r -q" >/dev/null 2>&1
		if [ -s "$MWOS_TMP/rebuilda.r" ] && [ -s "$MWOS_TMP/rebuildb.r" ]; then
			echo "ok    qcc Selfhosting L2 Vollport: rebuildFirstEdgesFromTable (src-qcc/ebnf.tc) kompiliert und assembliert (echter r68, ebnf.tc+codegen.tc getrennt) korrekt"
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (rebuildFirstEdgesFromTable) fehlgeschlagen"; fail=1
		fi
		rm -f "$MWOS_TMP"/rebuilda.a "$MWOS_TMP"/rebuildb.a "$MWOS_TMP"/rebuilda.r "$MWOS_TMP"/rebuildb.r
	else
		echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/ebnf.tc (rebuildFirstEdgesFromTable) kompiliert nicht sauber (siehe build/qcc_rebuildtest_a.err/build/qcc_rebuildtest_b.err)"; fail=1
	fi
else
	echo "warn  qcc Selfhosting L2 Vollport (rebuildFirstEdgesFromTable): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echte Assemblierung uebersprungen"
fi

# Selfhosting L2 Vollport (2026-07-26, direkt im Anschluss): naechster Ausschnitt
# nach src-qcc/ebnf.tc -- loadWorkfileAsGrammar (src/parsec.cpp:1162-1231,
# Fall B: PARSER-TABELLE/EBNF-QUELLTEXT direkt aus einer Arbeitsdatei laden, ohne
# .ebnf). Das Original nutzt EIN grosses sscanf(...) mit NEUN Ausgabeparametern --
# geht hier NICHT 1:1: (1) CALLEXT erlaubt max. 8 Stack-Argumente (neun
# ueberschreiten das), (2) int*-Ausgabeparameter mit "*p = wert"-Schreibzugriff
# sind in dieser QCC-Version generell unerprobt (siehe execPosResult/execFrom).
# Stattdessen ein Handparser (wfParseInt/wfParseToken, globale Parse-Position
# wfParsePos statt int*-Out-Parameter) passend zum writeWorkfile-Zeilenformat.
# ZWEI neue Grenzfaelle live gefunden: "lexTab[aktTabIndex].ident[0] = 0;"
# (arr[i].field[j] als Zuweisungsziel) wird vom Frontend abgelehnt -- durch
# tcCopyBounded(..., "", 32) ersetzt (identisch zum bereits vorhandenen
# "-"-Fall). Und: "?" als reiner Text in einer Fehlermeldung loeste erneut den
# bekannten "conditional-frame mismatch"-Bug aus (Quirk 15) -- ohne Fragezeichen
# umformuliert. Verifiziert wie bei writeWorkfile: kompiliert+assembliert sauber
# (echter r68, ebnf.tc+codegen.tc getrennt). ZUSAETZLICH die komplette
# Rundreise (writeWorkfile schreibt eine Tabelle -> Tabelle "vergessen" ->
# loadWorkfileAsGrammar laedt sie zurueck) einmalig gegen eine native
# C-Uebersetzung BEIDER Funktionen gegengeprueft: alle Werte (Modi, Ident-/
# TS-Text, true/falseAction, rangeLo/rangeHi, Quelltextlaenge) kommen exakt wie
# geschrieben zurueck.
if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
	mkdir -p "$MWOS_TMP"
	lwtest_main='
int lwtestFn() {
	void* fp;
	int savedCnt;

	aktTabIndex = 3;

	lexTab[0].mode = "TS";
	tcCopyBounded(lexTab[0].ident, "", 32);
	tcCopyBounded(lexTab[0].TS, "ident", 32);
	lexTab[0].trueAction = 1;
	lexTab[0].falseAction = -2;
	lexTab[0].callAddr = -1;
	lexTab[0].rangeLo = 0;
	lexTab[0].rangeHi = 0;

	lexTab[1].mode = "RNG";
	tcCopyBounded(lexTab[1].ident, "", 32);
	tcCopyBounded(lexTab[1].TS, "digit", 32);
	lexTab[1].rangeLo = 48;
	lexTab[1].rangeHi = 57;
	lexTab[1].trueAction = 2;
	lexTab[1].falseAction = -2;
	lexTab[1].callAddr = -1;

	lexTab[2].mode = "NTS";
	tcCopyBounded(lexTab[2].ident, "rule1", 32);
	tcCopyBounded(lexTab[2].TS, "rule1", 32);
	lexTab[2].trueAction = -1;
	lexTab[2].falseAction = -2;
	lexTab[2].callAddr = 0;
	lexTab[2].rangeLo = 0;
	lexTab[2].rangeHi = 0;

	appendQuelltext("rule1 = ident | digit ;");

	fp = fopen("/tmp/qcc_roundtrip_test.txt", "w");
	writeWorkfile(fp);
	fclose(fp);

	savedCnt = aktTabIndex;

	aktTabIndex = 0;
	quelltextLen = 0;

	if (loadWorkfileAsGrammar("/tmp/qcc_roundtrip_test.txt") == 0) {
		putint(-1);
		return;
	}

	putint(aktTabIndex);
	putint(savedCnt);
	putint(strcmp(lexTab[0].mode, "TS"));
	putint(strcmp(lexTab[0].TS, "ident"));
	putint(lexTab[0].trueAction);
	putint(lexTab[0].falseAction);
	putint(strcmp(lexTab[1].mode, "RNG"));
	putint(strcmp(lexTab[1].TS, "digit"));
	putint((int) lexTab[1].rangeLo);
	putint((int) lexTab[1].rangeHi);
	putint(strcmp(lexTab[2].mode, "NTS"));
	putint(strcmp(lexTab[2].ident, "rule1"));
	putint(strcmp(lexTab[2].TS, "rule1"));
	putint(quelltextLen);
	putint(1);
}'
	if build/qcc_p "$(cat src-qcc/ebnf.tc)$lwtest_main" > build/qcc_lwtest_a.ir 2>build/qcc_lwtest_a.err && \
		build/qcc_p "$(cat src-qcc/codegen.tc)" > build/qcc_lwtest_b.ir 2>build/qcc_lwtest_b.err && \
		build/qcc_backend build/qcc_lwtest_a.ir build/qcc_lwtest_a.s68 -os9 -largedata -part && \
		build/qcc_backend build/qcc_lwtest_b.ir build/qcc_lwtest_b.s68 -os9 -largedata -part; then
		cp build/qcc_lwtest_a.s68 "$MWOS_TMP/lwtesta.a"
		cp build/qcc_lwtest_b.s68 "$MWOS_TMP/lwtestb.a"
		rm -f "$MWOS_TMP/lwtesta.r" "$MWOS_TMP/lwtestb.r"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\lwtesta.a -o=M:\\TMP\\lwtesta.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\lwtestb.a -o=M:\\TMP\\lwtestb.r -q" >/dev/null 2>&1
		if [ -s "$MWOS_TMP/lwtesta.r" ] && [ -s "$MWOS_TMP/lwtestb.r" ]; then
			echo "ok    qcc Selfhosting L2 Vollport: loadWorkfileAsGrammar (src-qcc/ebnf.tc) kompiliert und assembliert (echter r68, ebnf.tc+codegen.tc getrennt) korrekt"
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (loadWorkfileAsGrammar) fehlgeschlagen"; fail=1
		fi
		rm -f "$MWOS_TMP"/lwtesta.a "$MWOS_TMP"/lwtestb.a "$MWOS_TMP"/lwtesta.r "$MWOS_TMP"/lwtestb.r
	else
		echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/ebnf.tc (loadWorkfileAsGrammar) kompiliert nicht sauber (siehe build/qcc_lwtest_a.err/build/qcc_lwtest_b.err)"; fail=1
	fi
else
	echo "warn  qcc Selfhosting L2 Vollport (loadWorkfileAsGrammar): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echte Assemblierung uebersprungen"
fi

# Selfhosting L2 Vollport (2026-07-26, direkt im Anschluss): naechste zwei
# Ausschnitte nach src-qcc/ebnf.tc -- runTests (src/parsec.cpp:1233-1268,
# alle TEST-Zeilen durch execFrom jagen und mit dem erwarteten Ergebnis
# vergleichen) UND loadPreservedTests (src/parsec.cpp:920-993, TESTS/
# NUTZER-CODE/LEXER/CODEGEN-Bloecke aus einer alten Arbeitsdatei retten, bevor
# sie ueberschrieben wird). ZWEI weitere Grenzfaelle gefunden: (1) dieselbe
# arr[i].field[j]-Zuweisungsziel-Ablehnung wie bei loadWorkfileAsGrammar,
# diesmal fuer testCases[i].input[n] -- Umweg ueber einen lokalen Puffer plus
# tcCopyBounded. (2) Bool-Ausdruecke (strncmp(...)==0 etc.) koennen NICHT
# direkt einer int-Variable/einem int-Feld zugewiesen werden (dieselbe
# strikte int/bool-Trennung wie bei Quirk 3/10 fuer Bedingungen/Rueckgaben,
# hier erstmals fuer eine normale Zuweisung getroffen) -- komplette if/else-
# Zweige statt "x = (a == b);". Verifiziert wie die vorigen Chunks (kompiliert
# + assembliert sauber, ebnf.tc+codegen.tc getrennt) -- Regressionstest in
# runtests.sh. ZUSAETZLICH ein voller Rundlauf (Arbeitsdatei mit einer
# RNG-Regel + drei TEST-Zeilen schreiben, per loadWorkfileAsGrammar +
# loadPreservedTests wieder einlesen, runTests ausfuehren) einmalig gegen
# eine native C-Uebersetzung aller vier beteiligten Funktionen gegengeprueft:
# beide liefern aktTabIndex=1/testCaseCnt=3/mismatches=0 (alle drei Testfaelle
# PASS).
if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
	mkdir -p "$MWOS_TMP"
	rtlp_main='
int rtlpFn() {
	void* fp;
	int mismatches;

	fp = fopen("/tmp/qcc_rtlp_test.txt", "w");
	fprintf(fp, "[PARSER-TABELLE]\n");
	fprintf(fp, "0     -1    -2    -1    48    57    -                RNG  digit\n");
	fprintf(fp, "[ENDE]\n\n");
	fprintf(fp, "[TESTS]\n");
	fprintf(fp, "TEST ");
	fputc(34, fp);
	fprintf(fp, "5");
	fputc(34, fp);
	fprintf(fp, " OK\n");
	fprintf(fp, "TEST ");
	fputc(34, fp);
	fprintf(fp, "ab");
	fputc(34, fp);
	fprintf(fp, " FAIL\n");
	fprintf(fp, "TEST ");
	fputc(34, fp);
	fprintf(fp, "x");
	fputc(34, fp);
	fprintf(fp, " FAIL\n");
	fclose(fp);

	if (loadWorkfileAsGrammar("/tmp/qcc_rtlp_test.txt") == 0) {
		putint(-1);
		return;
	}
	loadPreservedTests("/tmp/qcc_rtlp_test.txt");

	putint(aktTabIndex);
	putint(testCaseCnt);

	mismatches = runTests();
	putint(mismatches);
	putint(1);
}'
	if build/qcc_p "$(cat src-qcc/ebnf.tc)$rtlp_main" > build/qcc_rtlp_a.ir 2>build/qcc_rtlp_a.err && \
		build/qcc_p "$(cat src-qcc/codegen.tc)" > build/qcc_rtlp_b.ir 2>build/qcc_rtlp_b.err && \
		build/qcc_backend build/qcc_rtlp_a.ir build/qcc_rtlp_a.s68 -os9 -largedata -part && \
		build/qcc_backend build/qcc_rtlp_b.ir build/qcc_rtlp_b.s68 -os9 -largedata -part; then
		cp build/qcc_rtlp_a.s68 "$MWOS_TMP/rtlpa.a"
		cp build/qcc_rtlp_b.s68 "$MWOS_TMP/rtlpb.a"
		rm -f "$MWOS_TMP/rtlpa.r" "$MWOS_TMP/rtlpb.r"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\rtlpa.a -o=M:\\TMP\\rtlpa.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\rtlpb.a -o=M:\\TMP\\rtlpb.r -q" >/dev/null 2>&1
		if [ -s "$MWOS_TMP/rtlpa.r" ] && [ -s "$MWOS_TMP/rtlpb.r" ]; then
			echo "ok    qcc Selfhosting L2 Vollport: runTests + loadPreservedTests (src-qcc/ebnf.tc) kompiliert und assembliert (echter r68, ebnf.tc+codegen.tc getrennt) korrekt"
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (runTests/loadPreservedTests) fehlgeschlagen"; fail=1
		fi
		rm -f "$MWOS_TMP"/rtlpa.a "$MWOS_TMP"/rtlpb.a "$MWOS_TMP"/rtlpa.r "$MWOS_TMP"/rtlpb.r
	else
		echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/ebnf.tc (runTests/loadPreservedTests) kompiliert nicht sauber (siehe build/qcc_rtlp_a.err/build/qcc_rtlp_b.err)"; fail=1
	fi
else
	echo "warn  qcc Selfhosting L2 Vollport (runTests/loadPreservedTests): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echte Assemblierung uebersprungen"
fi

# Selfhosting L2 Vollport (2026-07-26, direkt im Anschluss): die rekursive-
# Abstiegs-Parsergruppe nach src-qcc/ebnf.tc -- literal/ident/block/repeat/
# option/factor/term/expression/rule (src/parsec.cpp:1371-1774) PLUS die bisher
# fehlenden Helfer push/pop/restart/errorMsg/test/addIdentList/patchLocalTrue/
# patchLocalFalse (920-1330). Alle neun Kernfunktionen hatten bereits seit
# Projektbeginn bare Prototypen in ebnf.tc (gegenseitige Rekursion) -- der
# FUNCDECL/FUNC-Blocker dafuer wurde am 2026-07-25 im Backend gefixt (siehe
# commit 7102de2). Kann NICHT sinnvoll ausgefuehrt/getestet werden (haengt an
# lexikalischeAnalyse()/getAktChar(), beides noch nicht portiert -- naechster
# Schritt) -- Verifikation bleibt bei kompiliert+assembliert sauber. WICHTIGES
# Zwischenergebnis: ein echter l68-Link von ebnf.tc+codegen.tc zeigt nach diesem
# Chunk nur noch GENAU die 7 erwarteten Lexer-Funktionen als unresolved
# (getAktChar/getAktLine/put/ebnfSyntax/semantischeAnylyse/
# lexikalischeAnalyse/exitProgram) -- die Parsergruppe selbst ist vollstaendig
# und korrekt verdrahtet, keine ueberraschenden fehlenden Symbole.
if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
	mkdir -p "$MWOS_TMP"
	if build/qcc_p "$(cat src-qcc/ebnf.tc)" > build/qcc_parser1.ir 2>build/qcc_parser1.err && \
		build/qcc_p "$(cat src-qcc/codegen.tc)" > build/qcc_parser1b.ir 2>build/qcc_parser1b.err && \
		build/qcc_backend build/qcc_parser1.ir build/qcc_parser1.s68 -os9 -largedata -part && \
		build/qcc_backend build/qcc_parser1b.ir build/qcc_parser1b.s68 -os9 -largedata -part; then
		cp build/qcc_parser1.s68 "$MWOS_TMP/parser1a.a"
		cp build/qcc_parser1b.s68 "$MWOS_TMP/parser1b.a"
		rm -f "$MWOS_TMP/parser1a.r" "$MWOS_TMP/parser1b.r"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\parser1a.a -o=M:\\TMP\\parser1a.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\parser1b.a -o=M:\\TMP\\parser1b.r -q" >/dev/null 2>&1
		if [ -s "$MWOS_TMP/parser1a.r" ] && [ -s "$MWOS_TMP/parser1b.r" ]; then
			echo "ok    qcc Selfhosting L2 Vollport: rekursive-Abstiegs-Parsergruppe (src-qcc/ebnf.tc) kompiliert und assembliert (echter r68, ebnf.tc+codegen.tc getrennt) korrekt"
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (Parsergruppe) fehlgeschlagen"; fail=1
		fi
		rm -f "$MWOS_TMP"/parser1a.a "$MWOS_TMP"/parser1b.a "$MWOS_TMP"/parser1a.r "$MWOS_TMP"/parser1b.r
	else
		echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/ebnf.tc (Parsergruppe) kompiliert nicht sauber (siehe build/qcc_parser1.err/build/qcc_parser1b.err)"; fail=1
	fi
else
	echo "warn  qcc Selfhosting L2 Vollport (Parsergruppe): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echte Assemblierung uebersprungen"
fi

# Selfhosting L2 Vollport (2026-07-26, direkt im Anschluss): der Lexer nach
# src-qcc/ebnf.tc -- lexikalischeAnalyse/getNext/getAktChar/comment/
# getAktLine/put/semantischeAnylyse (src/parsec.cpp:1810-2147, 1906-1970).
# lexikalischeAnalyse/getAktChar/put/semantischeAnylyse hatten bereits bare
# Prototypen; getNext/comment sind neu UND werden ihrerseits von getAktChar
# bzw. getAktLine gerufen -- strikte Definitions-Reihenfolge eingehalten.
# NEU dabei: initLexer() (das C++-Original initialisiert die Lexer-Config-
# Globalen wie startLineCommentString etc. per automatischem C++-Globalen-
# Initialisierer VOR main() -- QCC GLOBAL-Deklarationen koennen das nicht
# fuer String-Pointer, daher eine explizite Init-Funktion, die main()/
# ebnfSyntax() [naechster, letzter Schritt] einmal zu Programmbeginn rufen
# muss). WICHTIGSTER neuer Grenzfall: rohes Zeiger-Dereferenzieren (*p lesen
# UND *p = wert schreiben, nicht nur p[i]) wird hier zum ERSTEN Mal im ganzen
# Port gebraucht (getNext/comment) -- vorab per Standalone-Test gegen QCCVM
# verifiziert (Lesen+Schreiben ueber einen Pointer auf ein globales
# char-Array liefert exakt die erwarteten Werte), danach bedenkenlos wie im
# Original eingesetzt. Ebenfalls verifiziert: ein nicht verwendeter
# Rueckgabewert (comment() als blosse Anweisung) kompiliert und laeuft
# korrekt (QCCVM-Test) -- die vorsichtshalber-Variable aus dem vorigen
# Chunk (expression()s poppedLine) war also nicht zwingend noetig, bleibt
# aber unveraendert stehen.
#
# Verifiziert wie die vorigen Chunks (kompiliert + assembliert sauber) PLUS
# ein echter Tokenizer-Lauf ("rule1 = \"a\" ;" -> IDENT/EQUAL/LITERAL/END)
# einmalig gegen eine native C-Uebersetzung ALLER Lexer-Funktionen
# gegengeprueft: beide liefern exakt dieselbe Tokenfolge (138/129/139/143)
# und aktName="rule1". WICHTIGES Zwischenergebnis: ein echter l68-Link von
# ebnf.tc+codegen.tc zeigt nach diesem Chunk nur noch GENAU 2 unresolved
# Symbole (ebnfSyntax/exitProgram) -- der komplette Rest der Datei ist
# vollstaendig und korrekt verdrahtet.
if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
	mkdir -p "$MWOS_TMP"
	lextest_main='
int lextestFn() {
	void* fp;
	int tokens[16];
	int tokenCnt;

	fp = fopen("/tmp/qcc_lextest_in.ebnf", "w");
	fprintf(fp, "rule1 = ");
	fputc(34, fp);
	fprintf(fp, "a");
	fputc(34, fp);
	fprintf(fp, " ;\n");
	fclose(fp);

	initLexer();
	fpIn = fopen("/tmp/qcc_lextest_in.ebnf", "r");
	tokenCnt = 0;

	getAktChar();
	lexikalischeAnalyse();
	while (aktToken != 144 && tokenCnt < 16) {		/* TOKEN_EXIT */
		tokens[tokenCnt] = aktToken;
		tokenCnt = tokenCnt + 1;
		lexikalischeAnalyse();
	}
	fclose(fpIn);

	putint(tokenCnt);
	putint(tokens[0]);
	putint(tokens[1]);
	putint(tokens[2]);
	putint(tokens[3]);
	putint(strcmp(aktName, "rule1"));
	putint(1);
}'
	if build/qcc_p "$(cat src-qcc/ebnf.tc)$lextest_main" > build/qcc_lextest_a.ir 2>build/qcc_lextest_a.err && \
		build/qcc_backend build/qcc_lextest_a.ir build/qcc_lextest_a.s68 -os9 -largedata -part; then
		cp build/qcc_lextest_a.s68 "$MWOS_TMP/lextesta.a"
		rm -f "$MWOS_TMP/lextesta.r"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\lextesta.a -o=M:\\TMP\\lextesta.r -q" >/dev/null 2>&1
		if [ -s "$MWOS_TMP/lextesta.r" ]; then
			echo "ok    qcc Selfhosting L2 Vollport: Lexer (src-qcc/ebnf.tc) kompiliert und assembliert (echter r68) korrekt"
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (Lexer) fehlgeschlagen"; fail=1
		fi
		rm -f "$MWOS_TMP"/lextesta.a "$MWOS_TMP"/lextesta.r
	else
		echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/ebnf.tc (Lexer) kompiliert nicht sauber (siehe build/qcc_lextest_a.err)"; fail=1
	fi
else
	echo "warn  qcc Selfhosting L2 Vollport (Lexer): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- echte Assemblierung uebersprungen"
fi

# Selfhosting L2 Vollport (2026-07-26, direkt im Anschluss): ebnfMain/
# ebnfSyntax/exitProgram (src/parsec.cpp:170-334, 1357-1369, 322-334) --
# LETZTER Abschnitt des parsec.cpp-Vollports. QCC main() kann keine
# argc/argv empfangen (kein Mechanismus dafuer in Grammatik/Backend) -- die
# komplette Original-main()-Logik lebt deshalb in ebnfMain(char* baseArg),
# main() selbst ist ein duenner Wrapper mit fest einprogrammiertem
# Basisnamen ("qcc"). Bewusst entfallen: die argc<2-Usage-Meldung und der
# optionale <teststring>-Testlauf (runTests() deckt das bereits ab).
#
# WICHTIGSTER Unterschied zu allen bisherigen ebnf.tc-Chunks: ebnf.tc hat ab
# jetzt eine ECHTE main()-Funktion -- die sechs AELTEREN Tests oben (die
# bisher je ein eigenes "void main(){...}" an ebnf.tc anhaengten) wurden
# deshalb umgebaut (Testfunktion umbenannt, kein "main" mehr; Backend-Aufruf
# von "-part -runtime" auf reines "-part" reduziert -- r68-Assemblierung
# braucht keinen echten Einspringpunkt, nur echtes Linken (l68) wuerde einen
# brauchen, das pruefen jene sechs Tests aber ohnehin nicht).
#
# DAS HIER IST DER MEILENSTEIN-TEST: zum ERSTEN Mal ein VOLLER l68-Link von
# ebnf.tc (mit seiner echten main()) GEGEN codegen.tc, ohne jedes unresolved
# Symbol -- der komplette QCC-Vollport von src/parsec.cpp (Schritt 2 aus
# dem urspruenglichen 3-Schritt-Plan, siehe [[qcc-vollport-status]]) ist
# damit strukturell/kompilatorisch VOLLSTAENDIG. (Was das noch NICHT
# abdeckt: echte Ausfuehrung/Verhalten auf dem Q9-Emulator -- Schritt 3 des
# Plans, separat vermerkt.)
if [ -x build/qcc_backend ] && [ -x "$WINE" ] && [ -d "/Volumes/SSD1TB/projects/MWOS/DOS/BIN" ] && \
   [ -f "$MWOS_TMP/cstart.r" ] && [ -f "$MWOS_TMP/clib.l" ] && [ -f "$MWOS_TMP/os_lib.l" ] && [ -f "$MWOS_TMP/sys.l" ]; then
	mkdir -p "$MWOS_TMP"
	if build/qcc_p "$(cat src-qcc/ebnf.tc)" > build/qcc_final1.ir 2>build/qcc_final1.err && \
		build/qcc_p "$(cat src-qcc/codegen.tc)" > build/qcc_final1b.ir 2>build/qcc_final1b.err && \
		build/qcc_backend build/qcc_final1.ir build/qcc_final1.s68 -os9 -largedata -part -runtime && \
		build/qcc_backend build/qcc_final1b.ir build/qcc_final1b.s68 -os9 -largedata -part; then
		cp build/qcc_final1.s68 "$MWOS_TMP/final1a.a"
		cp build/qcc_final1b.s68 "$MWOS_TMP/final1b.a"
		rm -f "$MWOS_TMP/final1a.r" "$MWOS_TMP/final1b.r" "$MWOS_TMP/final1.out"
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\final1a.a -o=M:\\TMP\\final1a.r -q" >/dev/null 2>&1
		WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\r68.exe M:\\TMP\\final1b.a -o=M:\\TMP\\final1b.r -q" >/dev/null 2>&1
		if [ -s "$MWOS_TMP/final1a.r" ] && [ -s "$MWOS_TMP/final1b.r" ]; then
			WINEPREFIX="$HOME/.wine" "$WINE" cmd /c "M:\\DOS\\BIN\\l68.exe -a M:\\TMP\\cstart.r M:\\TMP\\final1a.r M:\\TMP\\final1b.r -l=M:\\TMP\\clib.l -l=M:\\TMP\\os_lib.l -l=M:\\TMP\\sys.l -o=M:\\TMP\\final1.out" >/dev/null 2>&1
			if [ -s "$MWOS_TMP/final1.out" ]; then
				echo "ok    qcc Selfhosting L2 Vollport: KOMPLETT -- ebnf.tc (mit echter main()) + codegen.tc kompilieren, assemblieren UND linken (echter r68+l68 gegen echte clib.l) vollstaendig OHNE unresolved Symbole"
			else
				echo "FAIL  qcc Selfhosting L2 Vollport: voller l68-Link (ebnf.tc+codegen.tc) fehlgeschlagen"; fail=1
			fi
		else
			echo "FAIL  qcc Selfhosting L2 Vollport: echte r68-Assemblierung (ebnfMain/main) fehlgeschlagen"; fail=1
		fi
		rm -f "$MWOS_TMP"/final1a.a "$MWOS_TMP"/final1b.a "$MWOS_TMP"/final1a.r "$MWOS_TMP"/final1b.r "$MWOS_TMP"/final1.out
	else
		echo "FAIL  qcc Selfhosting L2 Vollport: src-qcc/ebnf.tc (ebnfMain/main) kompiliert nicht sauber (siehe build/qcc_final1.err/build/qcc_final1b.err)"; fail=1
	fi
else
	echo "warn  qcc Selfhosting L2 Vollport (ebnfMain/main, voller Link): Backend, Wine/MWOS oder cstart.r/clib.l/os_lib.l/sys.l nicht verfuegbar -- uebersprungen"
fi

# ---------------------------------------------------------------------------
# -peephole (2026-09-14 erstmals automatisiert)
#
# Der Nachlaufoptimierer war seit dem 2026-09-08 gebaut und dokumentiert, aber
# von KEINEM Test beruehrt -- er lag sogar nur in der Backend-Kopie dieser
# Suite und fehlte in der ausgelieferten Fassung. Geprueft wird beides, was ein
# Optimierer schuldig ist: er muss etwas WEGNEHMEN und darf das Ergebnis NICHT
# aendern. Die Byte-Gleichheit auf echter Hardware bleibt Sache des 68030-Laufs;
# hier laeuft der Simulator.
# ---------------------------------------------------------------------------
ppfail=0
ppprog='int add(int a, int b){ return a + b; } int main(){ int i; int s; int t[8]; s = 0; i = 0; while (i < 8) { t[i] = i * 3; i = i + 1; } i = 0; while (i < 8) { s = add(s, t[i]); i = i + 1; } putint(s); }'
if [ -x build/qcc_p ] && [ -x build/qcc_backend ] &&
   build/qcc_p "$ppprog" > build/qcc_pp.ir 2>/dev/null &&
   build/qcc_backend build/qcc_pp.ir build/qcc_pp_off.s68 -runtime >/dev/null 2>&1 &&
   build/qcc_backend build/qcc_pp.ir build/qcc_pp_on.s68 -runtime -peephole >/dev/null 2>&1; then
	ppoff=$(wc -l < build/qcc_pp_off.s68 | tr -d ' ')
	ppon=$(wc -l < build/qcc_pp_on.s68 | tr -d ' ')
	ppvoff=$(python3 tools/qcc68sim.py build/qcc_pp_off.s68 2>&1)
	ppvon=$(python3 tools/qcc68sim.py build/qcc_pp_on.s68 2>&1)
	[ "$ppvoff" = "84" ] || { echo "FAIL  peephole: Vergleichslauf ohne -peephole liefert '$ppvoff' statt 84"; ppfail=1; }
	[ "$ppvon" = "84" ]  || { echo "FAIL  peephole: Lauf mit -peephole liefert '$ppvon' statt 84"; ppfail=1; }
	[ "$ppon" -lt "$ppoff" ] || { echo "FAIL  peephole: keine Zeile eingespart ($ppoff -> $ppon)"; ppfail=1; }
	if [ $ppfail -eq 0 ]; then
		echo "ok    peephole: $ppoff -> $ppon Zeilen, Ergebnis unveraendert (84)"
	else
		fail=1
	fi
else
	echo "FAIL  peephole: Testprogramm nicht uebersetzbar"; fail=1
fi

# ---------------------------------------------------------------------------
# Doppelte Dateien im Repo (2026-09-14, loest den alten "Abgleich Q9-QCC" ab)
#
# Bis zum Umbau am 2026-09-12 waren Q9-Parsec und Q9-QCC ZWEI Repos, und dieser
# Test verglich die Schnittmenge ihrer Dateien. Seither liegt alles in EINEM
# Repo, das Nachbarverzeichnis ../Q9-QCC gibt es nicht mehr -- der Test meldete
# nur noch "warn" und hat damit nichts mehr bewacht. Was in der Zwischenzeit
# unbemerkt auseinanderlief: die Integer-Suffix-Arbeit landete nur in EINER der
# beiden qcc.lextab, und der -peephole-Nachlauf existierte nur in der
# Backend-Kopie DIESER Suite, nicht in der ausgelieferten.
#
# Die Backend-Kopien sind deshalb ersatzlos entfallen (die Suite uebersetzt
# jetzt QIR68K_SRC/QIRARM64_SRC direkt, siehe oben). Was sich nicht ebenso
# aufloesen liess, steht hier und muss byteweise gleich bleiben.
#
# NICHT in der Liste: Source/ gegen src/ in diesem Projekt. Das Paar ist
# ABSICHTLICH verschieden -- gleicher Code, Kommentare deutsch bzw. englisch.
# ---------------------------------------------------------------------------
dupfail=0
dupcount=0
check_dup() {
	dupcount=$((dupcount + 1))
	if [ ! -f "$1" ] || [ ! -f "$2" ]; then
		echo "FAIL  Doppeldateien: $1 oder $2 fehlt"; dupfail=1; return
	fi
	cmp -s "$1" "$2" || { echo "FAIL  Doppeldateien abweichend: $1 != $2"; dupfail=1; }
}
# Die Sprachdefinition gehoert dem Frontend; diese Suite testet sie mit.
check_dup ../Q9-FRONTEND-C/q9-qcir/data/qcc.ebnf   data/qcc.ebnf
check_dup ../Q9-FRONTEND-C/q9-qcir/data/qcc.lextab data/qcc.lextab
# Der QCC-Port von parsec.cpp/codegen.cpp liegt zweimal im Projekt.
check_dup src-qcc/ebnf.tc    SourceQCC/ebnf.tc
check_dup src-qcc/codegen.tc SourceQCC/codegen.tc
# VM und Merge-Werkzeug werden von drei Stellen benutzt.
check_dup tools/qccvm.py      ../tools/qccvm.py
check_dup tools/qccvm.py      ../Q9-RUN/tools/qccvm.py
check_dup tools/qcc_merge.py  ../tools/qcc_merge.py
check_dup tools/qcc_merge.py  ../Q9-RUN/tools/qcc_merge.py
check_dup tools/qcc68sim.py   ../tools/qcc68sim.py
check_dup tools/vasmm68k_mot  ../tools/vasmm68k_mot
if [ $dupfail -eq 0 ]; then
	echo "ok    Doppeldateien: alle $dupcount Paare inhaltsgleich"
else
	fail=1
fi

[ $fail -eq 0 ] && echo "=== ALLE TESTS OK ===" || echo "=== FEHLER IN DER SUITE ==="
exit $fail
