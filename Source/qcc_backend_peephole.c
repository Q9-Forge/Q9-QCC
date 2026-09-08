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
	for (i = 0; i + 1 < phLineCount; i++) {
		/* NICHT "const char *a, *b, *c;" -- mehrere Zeiger-Deklaratoren in
		   EINER Anweisung sind ein stiller QCC-Abbruch (FAIL, 0 Meldungen,
		   08.09.2026 gefunden; unabhaengig von "const", "char *a, *b;"
		   bricht ebenso), noch nicht behoben. Je ein eigener Deklarator. */
		const char* labelStart;
		const char* srcStart;
		const char* dst;
		int labelLen, srcLen;
		if (phRemoved[i] || phRemoved[i + 1]) continue;
		if (!phMatchPop(phLines[i + 1], &dst)) continue;
		if (!phMatchPush(phLines[i], &labelStart, &labelLen, &srcStart, &srcLen)) continue;
		phLines[i] = phEmitFused(labelStart, labelLen, srcStart, srcLen, dst);
		phRemoved[i + 1] = 1;
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
	int folded, kept, i;
	phLoad(srcPath);
	folded = phFoldPushPop();
	kept = 0;
	for (i = 0; i < phLineCount; i++) if (!phRemoved[i]) kept++;
	phWrite(dstPath);
	fprintf(stderr, "qcc_backend: peephole: %d Push/Pop-Paare zusammengefasst (%d von %d Zeilen)\n",
		folded, kept, phLineCount);
}
