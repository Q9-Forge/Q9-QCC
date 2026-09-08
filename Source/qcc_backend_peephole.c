/*================================================================================
 * qcc_backend_peephole.c -- Peephole-Optimierer fuer die 68k-Ausgabe (-peephole)
 *
 * Arbeitet NICHT auf der IR, sondern auf dem bereits erzeugten Assemblertext --
 * dieselbe Stelle, an der Microwares eigener Optimierer o68 in der klassischen
 * Kette sitzt (cc -> cpp -> c68 -> o68 -> r68 -> l68, auf einer echten OS-9/68K-
 * Binaerdatei nachgemessen, 08.09.2026). Grund: die Verschwendung entsteht erst
 * bei der UMSETZUNG der abstrakten Stack-IR in echte 68k-Speicherzugriffe, nicht
 * in der IR selbst -- ein PUSH/POP-Paar in der IR ist dort keine ueberfluessige
 * Sequenz, sondern die Opcode-Semantik selbst (jeder Aufrufer verlaesst sich
 * darauf, dass sein Operand "vor ihm" auf dem Stapel liegt). Erst wenn das
 * 68k-Backend daraus "move.l X,-(a7)" gefolgt von "move.l (a7)+,Y" macht, ist
 * der Umweg ueber den Speicher sichtbar und entfernbar.
 *
 * ERSTES MUSTER: "move.l SRC,-(a7)" unmittelbar gefolgt von "move.l (a7)+,DST"
 * wird zu "move.l SRC,DST". Das ist an DIESER Stelle immer sicher, unabhaengig
 * vom Kontext: ein Push, dem sofort und ausschliesslich sein eigener Pop folgt,
 * aendert A7 zwischenzeitlich, aber nichts sonst beobachtet das -- der Wert
 * geht unveraendert von SRC nach DST, in einem Schritt statt zwei.
 *
 * ZWEITES MUSTER (08.09.2026, an der echten Haeufigkeitsverteilung von
 * qr68s eigener Ausgabe gefunden -- 2064 Vorkommen, der mit Abstand groesste
 * Einzelfund): "move.l SRC,Dn" unmittelbar gefolgt von "tst.l Dn" (DIESELBE
 * Nummer, ein DATENregister d0-d7). MOVE.L setzt N/Z auf 68000-Hardwareebene
 * bereits GENAUSO wie TST.L es fuer denselben Wert taete (V/C werden bei
 * beiden auf 0 geloescht) -- das TST ist also niemals mehr als eine
 * Wiederholung, die Zeile faellt komplett weg. BEWUSST NUR d0-d7, NIE
 * a0-a6: "move.l SRC,An" wird von r68/qr68 als MOVEA assembliert (die
 * einzige Opcode-Form fuer ein Adressregister-Ziel, unabhaengig vom
 * geschriebenen Mnemonic), und MOVEA setzt KEINE Flags -- ein TST danach
 * waere dort echt gebraucht.
 *
 * DRITTES MUSTER (dieselbe Haeufigkeitsliste): "move.l SRC,Dn" unmittelbar
 * gefolgt von "move.l Dn,DST" (dasselbe Datenregister) wird zu
 * "move.l SRC,DST" -- 385 Vorkommen allein fuer den Fall SRC="(a0)"/
 * DST="-(a7)". Sicher aus demselben Grund wie das erste Muster: der
 * Registerinhalt wird zwischen den beiden Zeilen von nichts sonst
 * beobachtet. DST darf ALLES sein (auch "-(a7)" -- dann ist es dasselbe
 * Ergebnis wie Muster eins, nur ueber diesen Matcher gefunden), SRC
 * ebenso: 68k erlaubt Speicher-zu-Speicher-MOVE, und eine Adressierung
 * mit Seiteneffekt (Post-/Praedekrement) wertet ihre effektive Adresse in
 * einem wie in zwei Schritten exakt einmal aus -- die Verschmelzung
 * aendert daran nichts. Geprueft: alle 23.387 move.l-Zeilen in qr68s
 * eigener Ausgabe haben genau EIN Komma (keine indizierte Adressierung
 * mit eingebettetem Komma in diesem Backend), das rechteste Komma trennt
 * also immer sauber SRC von DST.
 *
 * MEHRERE DURCHLAEUFE: eine Streichung legt oft die naechste frei --
 * "PUSH x / POP d0 / TST d0" faltet das erste Muster zu "move.l x,d0",
 * und ERST DANACH steht "tst.l d0" unmittelbar daneben. peepholeRun()
 * wiederholt deshalb alle drei Muster, bis ein Durchlauf nichts mehr
 * aendert (klassisches Peephole-Verhalten, dieselbe Erwartung wie bei
 * o68). Die Fold-Funktionen duerfen sich deshalb NICHT auf physische
 * Nachbarschaft verlassen (phLines[i+1]) -- eine schon gestrichene Zeile
 * liegt weiterhin im Array, phNextKept() ueberspringt sie.
 *
 * ARCHITEKTUR fuer weitere Muster (o68-Lehre): reines LESEN der Originalzeilen
 * (keine Mutation), Ersetzungen landen in einem eigenen Synthesepuffer, eine
 * Zeile wird durch Streichen markiert statt physisch verschoben (o68s remins-
 * Idee) -- neue Muster kommen als weitere phFold*-Funktionen dazu, nicht als
 * Sonderfaelle in einer bestehenden.
 *
 * SPEICHERGROESSEN GEMESSEN, NICHT GERATEN: qr68s eigene Assemblerausgabe mit
 * -remotedata (der groesste bisher auf dem Ziel gelaufene Fall ausserhalb des
 * Selbsthosts) hat 75.273 Zeilen / 1.588.771 Byte. Die Grenzen unten geben
 * darauf reichlich Kopfraum; QCCs eigener Selbsthost-Bau (222.832 Zeilen /
 * 4,59 MB) sprengt sie bewusst -- -peephole ist fuer den noch nicht verdrahtet,
 * das waere eine eigene, spaetere Speicherbudget-Entscheidung. Ueberschreitung
 * bricht mit fatal() ab, wie jede andere Kapazitaetsgrenze in diesem Backend --
 * keine stille Kuerzung.
 *================================================================================*/

#define PH_MAX_LINES  100000
#define PH_TEXT_BYTES 2097152   /* 2 MB, ~35% Kopfraum ueber qr68 (1,59 MB) */
#define PH_SYNTH_BYTES PH_TEXT_BYTES /* eine ersetzte Zeile ist nie laenger als
                                        die beiden Originalzeilen zusammen --
                                        die Summe aller Ersetzungen passt also
                                        immer in denselben Rahmen wie der
                                        Originaltext. */

static char phText[PH_TEXT_BYTES];
static const char* phLines[PH_MAX_LINES];
static int phRemoved[PH_MAX_LINES];
static int phLineCount = 0;

static char phSynth[PH_SYNTH_BYTES];
static int phSynthUsed = 0;

static void phLoad(const char* path) {
	FILE* fp;
	int size, got, i;
	fp = fopen(path, "r");
	if (!fp) fatal("peephole: kann Assemblerdatei nicht lesen");
	size = 0;
	for (;;) {
		got = fread(phText + size, 1, PH_TEXT_BYTES - 1 - size, fp);
		if (got <= 0) break;
		size += got;
		if (size >= PH_TEXT_BYTES - 1) fatal("peephole: Assemblerausgabe zu gross fuer PH_TEXT_BYTES");
	}
	fclose(fp);
	phText[size] = 0;
	phLineCount = 0;
	phLines[phLineCount++] = phText;
	for (i = 0; i < size; i++) {
		if (phText[i] != '\n') continue;
		phText[i] = 0;
		if (i + 1 < size) {
			if (phLineCount >= PH_MAX_LINES) fatal("peephole: zu viele Zeilen fuer PH_MAX_LINES");
			phLines[phLineCount++] = &phText[i + 1];
		}
	}
	for (i = 0; i < phLineCount; i++) phRemoved[i] = 0;
}

/* Naechste NICHT gestrichene Zeile nach i, oder -1. Siehe Kommentar am
 * Dateianfang zu "mehrere Durchlaeufe": nach einer Faltung liegt die
 * logisch naechste Zeile nicht mehr zwingend bei i+1. */
static int phNextKept(int i) {
	int j = i + 1;
	while (j < phLineCount && phRemoved[j]) j++;
	return j < phLineCount ? j : -1;
}

/* Erkennt "move.l SRC,-(a7)", optional mit einem "label:\t"-Vorspann auf
 * DERSELBEN Zeile (dieses Backend haengt Labels so an, siehe emitIR()).
 * Liest NUR -- schreibt nichts in die Zeile, damit ein Fehlschlag hier
 * (Pop passt am Ende doch nicht) den Originaltext nicht beschaedigt. */
static int phMatchPush(const char* line, const char** labelStart, int* labelLen,
                        const char** srcStart, int* srcLen) {
	const char* p;
	const char* colon = strchr(line, ':');
	*labelLen = 0;
	if (colon != 0 && colon[1] == '\t') {
		*labelStart = line;
		*labelLen = (int)(colon - line);
		p = colon + 2;
	} else {
		if (line[0] != '\t') return 0;
		p = line + 1;
	}
	if (strncmp(p, "move.l\t", 7) != 0) return 0;
	p += 7;
	{
		const char* comma = strrchr(p, ',');
		if (comma == 0 || strcmp(comma, ",-(a7)") != 0) return 0;
		*srcStart = p;
		*srcLen = (int)(comma - p);
	}
	return 1;
}

/* Erkennt "move.l (a7)+,DST" -- KEIN Label davor: koennte ein Sprungziel
 * sein, und dafuer gibt es hier (noch) keine Sprungziel-Nachfuehrung
 * (s. o68-Kommentar oben am Dateianfang). Lieber nicht falten als falsch. */
static int phMatchPop(const char* line, const char** dstStart) {
	if (line[0] != '\t') return 0;
	if (strncmp(line + 1, "move.l\t(a7)+,", 13) != 0) return 0;
	*dstStart = line + 1 + 13;
	return 1;
}

static const char* phEmitFused(const char* labelStart, int labelLen,
                                const char* srcStart, int srcLen, const char* dst) {
	char* p = phSynth + phSynthUsed;
	int n;
	if (labelLen > 0)
		n = sprintf(p, "%.*s:\tmove.l\t%.*s,%s", labelLen, labelStart, srcLen, srcStart, dst);
	else
		n = sprintf(p, "\tmove.l\t%.*s,%s", srcLen, srcStart, dst);
	phSynthUsed += n + 1;
	if (phSynthUsed >= PH_SYNTH_BYTES) fatal("peephole: Synthesepuffer zu klein");
	return p;
}

/* Push unmittelbar gefolgt vom eigenen Pop, s. Kommentar am Dateianfang.
 * Pop wird ZUERST geprueft (lesend, folgenlos bei Fehlschlag) -- erst wenn
 * er passt, lohnt sich die genauere Pruefung des Push davor. */
static int phFoldPushPop(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		/* NICHT "const char *a, *b, *c;" -- mehrere Zeiger-Deklaratoren in
		   EINER Anweisung sind ein stiller QCC-Abbruch (FAIL, 0 Meldungen,
		   08.09.2026 gefunden; unabhaengig von "const", "char *a, *b;"
		   bricht ebenso), noch nicht behoben. Je ein eigener Deklarator. */
		const char* labelStart;
		const char* srcStart;
		const char* dst;
		int labelLen, srcLen;
		int j;
		if (phRemoved[i]) continue;
		j = phNextKept(i);
		if (j < 0) continue;
		if (!phMatchPop(phLines[j], &dst)) continue;
		if (!phMatchPush(phLines[i], &labelStart, &labelLen, &srcStart, &srcLen)) continue;
		phLines[i] = phEmitFused(labelStart, labelLen, srcStart, srcLen, dst);
		phRemoved[j] = 1;
		folded++;
	}
	return folded;
}

/* Erkennt "move.l SRC,Dn" (Dn eines von d0-d7 -- NIE a0-a6, s. Kommentar
 * am Dateianfang zu Muster zwei/drei), optional mit Label-Vorspann.
 * Liefert SOWOHL SRC (fuer Muster drei) ALS AUCH nur das Zielregister
 * (fuer Muster zwei) -- eine Funktion statt zwei fast identischer. */
static int phMatchMoveIntoDataReg(const char* line, const char** labelStart, int* labelLen,
                                   const char** srcStart, int* srcLen,
                                   const char** regStart, int* regLen) {
	const char* p;
	const char* comma;
	const char* colon = strchr(line, ':');
	*labelLen = 0;
	if (colon != 0 && colon[1] == '\t') {
		*labelStart = line;
		*labelLen = (int)(colon - line);
		p = colon + 2;
	} else {
		if (line[0] != '\t') return 0;
		p = line + 1;
	}
	if (strncmp(p, "move.l\t", 7) != 0) return 0;
	p += 7;
	comma = strrchr(p, ',');
	if (comma == 0) return 0;
	if (comma[1] != 'd' || comma[2] < '0' || comma[2] > '7' || comma[3] != '\0') return 0;
	*srcStart = p;
	*srcLen = (int)(comma - p);
	*regStart = comma + 1;
	*regLen = 2;
	return 1;
}

static int phMatchTst(const char* line, const char* regStart, int regLen) {
	if (line[0] != '\t') return 0;
	if (strncmp(line + 1, "tst.l\t", 6) != 0) return 0;
	if ((int)strlen(line + 7) != regLen) return 0;
	return strncmp(line + 7, regStart, regLen) == 0;
}

/* Kein Ersatzbau noetig -- die Move-Zeile bleibt UNVERAENDERT stehen, nur
 * das ueberfluessige TST verschwindet. */
static int phFoldMoveTst(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		/* Je ein eigener Deklarator -- s. Kommentar in phFoldPushPop oben. */
		const char* labelStart;
		const char* srcStart;
		const char* regStart;
		int labelLen, srcLen, regLen, j;
		if (phRemoved[i]) continue;
		if (!phMatchMoveIntoDataReg(phLines[i], &labelStart, &labelLen, &srcStart, &srcLen, &regStart, &regLen)) continue;
		j = phNextKept(i);
		if (j < 0) continue;
		if (!phMatchTst(phLines[j], regStart, regLen)) continue;
		phRemoved[j] = 1;
		folded++;
	}
	return folded;
}

/* DRITTES MUSTER, s. Kommentar am Dateianfang: "move.l Dn,DST" -- dasselbe
 * Datenregister, das die vorherige Zeile (phMatchMoveIntoDataReg) gerade
 * gefuellt hat. KEIN Label davor zugelassen (koennte Sprungziel sein,
 * dieselbe Vorsicht wie bei phMatchPop). DST reicht bis zum Zeilenende --
 * derselbe Aufbau wie phMatchPop, deshalb direkt an phEmitFused
 * uebergebbar, keine eigene Ersatzbau-Funktion noetig. */
static int phMatchMoveFromDataReg(const char* line, const char* regStart, int regLen,
                                   const char** dstStart) {
	const char* p;
	if (line[0] != '\t') return 0;
	if (strncmp(line + 1, "move.l\t", 7) != 0) return 0;
	p = line + 8;
	if (strncmp(p, regStart, regLen) != 0) return 0;
	if (p[regLen] != ',') return 0;
	*dstStart = p + regLen + 1;
	return 1;
}

static int phFoldLoadThenMove(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		/* Je ein eigener Deklarator -- s. Kommentar in phFoldPushPop oben. */
		const char* labelStart;
		const char* srcStart;
		const char* regStart;
		const char* dstStart;
		int labelLen, srcLen, regLen, j;
		if (phRemoved[i]) continue;
		if (!phMatchMoveIntoDataReg(phLines[i], &labelStart, &labelLen, &srcStart, &srcLen, &regStart, &regLen)) continue;
		j = phNextKept(i);
		if (j < 0) continue;
		if (!phMatchMoveFromDataReg(phLines[j], regStart, regLen, &dstStart)) continue;
		phLines[i] = phEmitFused(labelStart, labelLen, srcStart, srcLen, dstStart);
		phRemoved[j] = 1;
		folded++;
	}
	return folded;
}

static void phWrite(const char* path) {
	FILE* fp;
	int i;
	fp = fopen(path, "w");
	if (!fp) fatal("peephole: kann Ausgabedatei nicht neu schreiben");
	for (i = 0; i < phLineCount; i++) {
		if (phRemoved[i]) continue;
		fputs(phLines[i], fp);
		fputc('\n', fp);
	}
	if (ferror(fp)) fatal("peephole: Schreibfehler");
	fclose(fp);
}

/* srcPath ist die von emitIR() beschriebene (temporaere, bei -peephole)
   Datei; dstPath die eigentliche Zieldatei -- siehe Kommentar in main()
   zu -peephole: dstPath darf hier zum ERSTEN und einzigen Mal angelegt
   werden (OS-9s I$Create scheitert sonst an einer schon bestehenden
   Datei). srcPath bleibt als .tmp-Datei liegen -- kein Aufrufer in
   dieser Kette raeumt Zwischendateien auf, s. .i/.ir ueberall sonst. */
static void peepholeRun(const char* srcPath, const char* dstPath) {
	int total, roundTotal, kept, i, rounds;
	phLoad(srcPath);
	total = 0;
	rounds = 0;
	do {
		roundTotal = phFoldPushPop();
		roundTotal += phFoldMoveTst();
		roundTotal += phFoldLoadThenMove();
		total += roundTotal;
		rounds++;
	} while (roundTotal > 0);
	kept = 0;
	for (i = 0; i < phLineCount; i++) if (!phRemoved[i]) kept++;
	phWrite(dstPath);
	fprintf(stderr, "qcc_backend: peephole: %d Optimierungen in %d Durchlaeufen (%d von %d Zeilen)\n",
		total, rounds, kept, phLineCount);
}
