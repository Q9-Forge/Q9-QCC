/*
 * qcc -- universal Q9 compiler driver
 *
 * Purpose:
 *   Loads the selected target configuration and executes the frontend,
 *   backend, optimizer, assembler and linker stages.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define QCC_VERSION "0.2.0-dev"
#define TEXT 128

/* Host and OS-9 use different command names for the small file operations
 * needed by the pipeline.  Keep this translation at the driver boundary so
 * the compiler stages themselves remain platform-neutral. */
#if defined(_Q9OS) || defined(_OSK)
/* 2026-09-27: das echte OS-9-"makdir" kennt KEINE Option "-e" (nur -p/-q/-x/-z, per
 * "makdir --help" am echten Q9SYS-Emulator verifiziert) -- "-e" fuehrte zu "unknown
 * option 'e'" und liess q9_system() jeden Aufruf mit einem generischen E$FNA scheitern,
 * noch bevor qcc ueberhaupt sein Tempoverzeichnis anlegen konnte. "-p" (wie beim
 * Host-Pendant "mkdir -p") existiert wirklich und hat dieselbe Bedeutung. */
#define QCC_MKDIR "makdir -p"
#define QCC_COPY  "copy"
#define QCC_REMOVE "del"
#else
#define QCC_MKDIR "mkdir -p"
#define QCC_COPY  "cp"
#define QCC_REMOVE "rm -f"
#endif

static char target[TEXT] = "OS9-68K";
static char frontend[TEXT] = "c";
static char cpu[TEXT] = "68000";
static char optimizer[TEXT] = "qo68k";
static char backend[TEXT] = "qir68k";
static char assembler[TEXT] = "qr68k";
static char linker[TEXT] = "ql68k";
static char startup[TEXT] = "cstart.a";
static char libraries[TEXT] = "clib.l,os9.l,sys.l";
#if defined(_Q9OS) || defined(_OSK)
static char qcpp[TEXT] = "qcpp";
static char qcir[TEXT] = "qcir";
#else
static char qcpp[TEXT] = "../Q9-FRONTEND-C/q9-qcpp/build/qcpp";
static char qcir[TEXT] = "../Q9-FRONTEND-C/q9-qcir/build/qcir";
#endif
static int dry_run;
static int print_config;
static int emit_ir;
static int preprocess_only;
static int assembly_only;
static int object_only;

#if !defined(_Q9OS) && !defined(_OSK)
/* The host uses the native C89 system() function.  The OS-9 driver launches
 * tools directly via q9_os9exec/q9_os9exec_stdout below and must not retain a
 * link-time dependency on qclib's placeholder system(). */
/* Function: q9_system
 * Executes one configured pipeline command.
 * Parameters: command Shell command line.
 * Returns: Command status returned by the runtime. */
static int q9_system(const char *command)
{
	return system(command);
}
#endif
#if defined(_Q9OS) || defined(_OSK)
/* 2026-09-27: process.h/modes.h waren Ueberbleibsel des fruehen, seither
 * entfernten creat()-Codes -- nichts hier braucht heute noch etwas aus
 * ihnen (modes.h-Konstanten wie S_IREAD kommen nirgends mehr vor). Beide
 * ziehen bei einer Uebersetzung mit dem PROJEKTEIGENEN qcpp einen Berg
 * echter Microware-OS9000-Header nach (procid.h, types.h, ...), die dort
 * gar nicht vorhanden sind ("qcpp: #include: Datei nicht gefunden:
 * process.h") -- die drei folgenden extern-Deklarationen reichen. */
#ifndef QCC_NATIVE_BRIDGE
extern int q9_os9exec(const char *module, char **argv, char **environment);
extern int q9_os9exec_stdout(const char *module, char **argv,
			     char **environment, const char *path);
#endif
static char *q9_os9_path_env[2];
static char q9_os9_path[] = "PATH=/dd/CMDS_QCC";
static char **_environ;

/* The native driver currently supplies only PATH to child modules.  Until
 * environment import is implemented, its configuration and SDK locations
 * come from /dd/SYS and the documented OS-9 defaults.  Avoid qclib's getenv
 * stub (which always reports "not found") in the Q9 executable. */
#define qcc_getenv(name) 0

/* Execute one OS-9 module with an explicit argument vector. */
static int q9_exec_argv(const char *module, char **argv)
{
	return q9_os9exec(module, argv, _environ);
}

/* Run a child with its standard output connected to an OS-9 file.
 *
 * 2026-09-27: die urspruengliche Fassung benutzte rohes creat()/dup()/close() --
 * am echten Q9-Emulator liefert creat() dort zuverlaessig -1 (mit einem winzigen,
 * isolierten Testprogramm einzeln nachgestellt: "creat: -1"), obwohl dieselbe
 * Datei ueber die hoehere stdio-Ebene (fopen()/freopen()) klaglos funktioniert
 * (ebenfalls isoliert bestaetigt: "freopen ergab: 1", Inhalt danach lesbar). qcir
 * (Q9-FRONTEND-C/q9-qcir/data/qcc_p.c) kennt kein eigenes "-o" und schreibt IMMER
 * auf stdout -- deshalb muss stdout selbst umgeleitet werden, jetzt per
 * freopen(), nicht per Descriptor-Verbiegen. os9exec()/os9forkc() vererben dem
 * Kind den zum Zeitpunkt des Forks bereits umgebogenen Pfad 1 (dasselbe Prinzip,
 * nach dem die OS-9-Shell selbst "cmd >datei" umsetzt -- erst umlenken, dann
 * forken), das genuegt hier.
 *
 * Bekannte, akzeptierte Einschraenkung: stdout wird NICHT zurueck auf die
 * Konsole umgebogen (der urspruengliche Pfad ist nach freopen() geschlossen,
 * ohne dessen Namen zu kennen liesse er sich nicht zurueckholen) -- fuer die
 * Pipeline unschaedlich, da nach diesem einen Aufruf keine weitere Stufe mehr
 * auf stdout schreibt (Erfolg bleibt wie bei den meisten Compilern stumm,
 * Fehler laufen ohnehin ueber stderr). */
static int q9_exec_stdout(const char *module, char **argv, const char *path)
{
	return q9_os9exec_stdout(module, argv, _environ, path);
}

/* Remove up to 5 temp files via os9exec/os9forkc instead of q9_system()'s
 * system() call.
 *
 * 2026-09-27: the "del a b c d e" cleanup at pipeline end failed with a
 * generic E$FNA through q9_system() ("File not accessible" -- one of the
 * arguments is legitimately missing whenever an earlier stage was skipped,
 * e.g. output.opt.s68k without --optimizer), the same class of problem the
 * makdir/qcpp/qcir/backend calls already had before they moved to
 * q9_exec_argv(). This uses the same, already-proven mechanism, and drops
 * any argument that is NULL so a skipped stage's missing file is never
 * passed to "del" in the first place. */
static void q9_remove_files(const char *f1, const char *f2, const char *f3, const char *f4, const char *f5)
{
	char *del_argv[7];
	const char *files[5];
	int i;
	int n;
	files[0] = f1; files[1] = f2; files[2] = f3; files[3] = f4; files[4] = f5;
	del_argv[0] = "del";
	n = 1;
	for (i = 0; i < 5; i++) {
		if (files[i] != NULL)
			del_argv[n++] = (char *)files[i];
	}
	del_argv[n] = NULL;
	if (n > 1)
		q9_exec_argv("del", del_argv);
}

/* Use direct module launch for file copies too. system("copy ...") fails to
 * open otherwise-readable ROFs from this driver's OS-9 process context. */
static int q9_copy_file(const char *source, const char *destination)
{
	char *copy_argv[4];
	copy_argv[0] = "copy";
	copy_argv[1] = (char *)source;
	copy_argv[2] = (char *)destination;
	copy_argv[3] = NULL;
	return q9_exec_argv("copy", copy_argv);
}
#else
/* The host driver delegates environment lookup to its native C runtime. */
static char *qcc_getenv(const char *name)
{
	return getenv(name);
}
#endif
static int keep_files;
static int large_data;
static char stack_size[TEXT] = "";
static char tmpdir[TEXT] = "build/qcc-tmp";
static char output[TEXT] = "";
static int tmpdir_set;
static char config_section[TEXT] = "global";

/* Function: copy_text
 * Copies bounded configuration text and always terminates it.
 * Parameters: dst Destination buffer; src Source string.
 * Returns: Nothing. */
static void copy_text(char *dst, const char *src) { strncpy(dst, src, TEXT - 1); dst[TEXT - 1] = '\0'; }
/* Function: usage
 * Prints qcc command-line usage information.
 * Parameters: name Program name.
 * Returns: Nothing. */
static void usage(const char *name)
{
	printf("Usage: %s [options] input...\n", name);
	printf("  --target NAME    Zielprofil\n  --frontend NAME  Frontend (Standard: c)\n");
	printf("  --cpu TYPE       CPU an passende Werkzeuge weiterreichen\n");
	printf("  --dry-run        Pipeline nur anzeigen\n  --print-config   Konfiguration anzeigen\n");
	printf("  --emit-ir        nach Q9 Stack-IR stoppen\n  --tmpdir DIR     Zwischenverzeichnis\n");
	printf("  --keep           Zwischendateien behalten\n  -o FILE           Ausgabedatei\n");
	printf("  --stack SIZE     OS-9-Modulstack, z. B. 512K\n");
	printf("  -E               nur vorverarbeiten\n  -S               nur Assembler erzeugen\n  -c               nur Objektdatei erzeugen\n");
	printf("  --no-optimizer   Optimierer ueberspringen\n");
	printf("  --help, --version\n");
}
/* Function: config_space
 * Identifies whitespace accepted around configuration syntax.
 * Parameters: c Character to inspect.
 * Returns: Non-zero for space, tab, CR or LF. */
static int config_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
/* Function: config_line
 * Parses one configuration line and updates the active target settings.
 * Parameters: line Mutable configuration line.
 * Returns: Nothing. */
static void config_line(char *line)
{
	char key[TEXT], value[TEXT], section[TEXT];
	char *p;
	int n;

	p = line;
	while (config_space(*p)) ++p;
	if (*p == '\0' || *p == '#' || *p == ';') return;
	if (*p == '[') {
		++p;
		n = 0;
		while (*p != '\0' && *p != ']' && n < TEXT - 1)
			section[n++] = *p++;
		if (*p != ']') return;
		section[n] = '\0';
		copy_text(config_section, section);
		return;
	}

	n = 0;
	while (*p != '\0' && *p != '=' && !config_space(*p) && n < TEXT - 1)
		key[n++] = *p++;
	key[n] = '\0';
	while (config_space(*p)) ++p;
	if (*p != '=') return;
	++p;
	while (config_space(*p)) ++p;
	n = 0;
	while (*p != '\0' && !config_space(*p) && *p != '#' && n < TEXT - 1)
		value[n++] = *p++;
	value[n] = '\0';
	if (key[0] == '\0' || value[0] == '\0') return;
	if (strcmp(config_section, "global") != 0 && strcmp(config_section, "target.OS9-68K") != 0) return;
	if (strcmp(key, "target") == 0) copy_text(target, value);
	else if (strcmp(key, "frontend") == 0) copy_text(frontend, value);
	else if (strcmp(key, "cpu") == 0) copy_text(cpu, value);
	else if (strcmp(key, "backend") == 0) copy_text(backend, value);
	else if (strcmp(key, "optimizer") == 0) copy_text(optimizer, value);
	else if (strcmp(key, "assembler") == 0) copy_text(assembler, value);
	else if (strcmp(key, "linker") == 0) copy_text(linker, value);
	else if (strcmp(key, "startup") == 0) copy_text(startup, value);
	else if (strcmp(key, "libraries") == 0) copy_text(libraries, value);
	else if (strcmp(key, "qcpp") == 0) copy_text(qcpp, value);
	else if (strcmp(key, "qcir") == 0) copy_text(qcir, value);
}
/* Function: load_config
 * Loads the first available qcc configuration file.
 * Parameters: None.
 * Returns: Nothing. */
static void load_config(void)
{
	FILE *f; char line[256];
	char config_path[512];
	const char *config_env;
	const char *sdk_env;
	const char *host_dir;
	f = NULL;
	config_env = qcc_getenv("QCC_CONFIG");
	if (config_env != NULL && config_env[0] != '\0')
		f = fopen(config_env, "r");
	if (f == NULL) {
		sdk_env = qcc_getenv("Q9SDK");
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
		host_dir = "macOS/ARM64";
#elif defined(__APPLE__) && defined(__x86_64__)
		host_dir = "macOS/x86_64";
#elif defined(__linux__)
		host_dir = "Linux";
#elif defined(_WIN32) || defined(_WIN64)
		host_dir = "Windows";
#else
		host_dir = NULL;
#endif
		/* 2026-10-02: Der Host-SDK-Pfad braucht den Architekturordner wie
		 * bei qmake (macOS/ARM64/SYS bzw. macOS/x86_64/SYS) -- die alte
		 * Form "macOS/SYS/qcc.conf" existiert an keiner Stelle, die
		 * stage_macos_sdk.sh je anlegt. load_config() fiel deshalb auf dem
		 * Host IMMER auf die eingebauten relativen Entwicklungspfade
		 * zurueck, sobald qcc nicht aus dem eigenen Quellverzeichnis heraus
		 * gestartet wurde (SDK-Installation per CMDS-Verzeichnis, Aufruf
		 * aus einem beliebigen Projektverzeichnis).
		 * 2026-10-03: Zuerst nur fuer macOS gefixt, dann live unter WSL/
		 * Linux (LQQ-Profil) reproduziert -- host_dir deckt jetzt auch
		 * Linux und Windows ab (das Format Linux/SYS bzw. Windows/SYS
		 * braucht anders als macOS keinen Architektur-Unterordner, da
		 * $(Q9SDK)/Linux/CMDS flach ist). */
		if (sdk_env != NULL && sdk_env[0] != '\0' && host_dir != NULL &&
		    strlen(sdk_env) + sizeof("//SYS/qcc.conf") + strlen(host_dir) <= sizeof(config_path)) {
			sprintf(config_path, "%s/%s/SYS/qcc.conf", sdk_env, host_dir);
			f = fopen(config_path, "r");
		}
	}
	if (f == NULL) f = fopen("config/qcc.conf", "r");
	if (f == NULL) f = fopen("Q9-QCC/config/qcc.conf", "r");
	if (f == NULL) f = fopen("/dd/SYS/qcc.conf", "r");
	if (f == NULL) return;
	while (fgets(line, 256, f) != NULL) config_line(line);
	fclose(f);
}
/* Function: resolve_tmpdir
 * Selects the temporary directory from explicit options or the environment.
 * Parameters: None.
 * Returns: Nothing. */
static void resolve_tmpdir(void)
{
	const char *base;
	if (tmpdir_set) return;
	base = qcc_getenv("TMP");
	if (base == NULL || base[0] == '\0') base = qcc_getenv("TEMP");
	if (base != NULL && base[0] != '\0') {
		sprintf(tmpdir, "%s/qcc", base);
	}
}
/* Function: show_config
 * Prints the effective compiler-driver configuration.
 * Parameters: None.
 * Returns: Nothing. */
static void show_config(void)
{
	printf("target=%s\nfrontend=%s\ncpu=%s\nbackend=%s\noptimizer=%s\n", target, frontend, cpu, backend, optimizer);
	printf("assembler=%s\nlinker=%s\nstartup=%s\nlibraries=%s\nqcpp=%s\nqcir=%s\n", assembler, linker, startup, libraries, qcpp, qcir);
}
/* Function: main
 * Parses options and executes the configured compiler pipeline.
 * Parameters: argc, argv Command-line argument count and vector.
 * Returns: Process status, zero on success. */
int main(int argc, char **argv)
{
	int i, inputs = 0;
	#if defined(_Q9OS) || defined(_OSK)
	q9_os9_path_env[0] = q9_os9_path;
	q9_os9_path_env[1] = NULL;
	_environ = q9_os9_path_env;
	#endif
	load_config();
	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
		if (strcmp(argv[i], "--version") == 0) { printf("qcc %s\n", QCC_VERSION); return 0; }
		if (strcmp(argv[i], "--dry-run") == 0) { dry_run = 1; continue; }
		if (strcmp(argv[i], "--emit-ir") == 0) { emit_ir = 1; continue; }
		if (strcmp(argv[i], "-E") == 0) { preprocess_only = 1; continue; }
		if (strcmp(argv[i], "-S") == 0) { assembly_only = 1; continue; }
		if (strcmp(argv[i], "-c") == 0) { object_only = 1; continue; }
		if (strcmp(argv[i], "--no-optimizer") == 0) { optimizer[0] = '\0'; continue; }
		if (strcmp(argv[i], "--keep") == 0) { keep_files = 1; continue; }
		/* 2026-09-27: qir68k warnt selbst ab einer gewissen Groesse globaler
		 * Daten ("value out of range" droht bei r68) und schlaegt "-largedata"
		 * vor -- bislang gab es keinen Weg, das durchzureichen. Bewusst kein
		 * automatisches Umschalten: -largedata aendert das Codegen-Modell
		 * (zusaetzliche Indirektionstabelle je Modul), also nur auf Wunsch. */
		if (strcmp(argv[i], "--largedata") == 0) { large_data = 1; continue; }
		if (strcmp(argv[i], "--stack") == 0) {
			if (i + 1 >= argc) { fprintf(stderr, "qcc: --stack erwartet eine Groesse\n"); return 2; }
			copy_text(stack_size, argv[++i]);
			continue;
		}
		if (strcmp(argv[i], "--print-config") == 0) { print_config = 1; continue; }
		if (strcmp(argv[i], "--tmpdir") == 0 || strcmp(argv[i], "-o") == 0) {
			if (i + 1 >= argc) { fprintf(stderr, "qcc: Option erwartet einen Wert\n"); return 2; }
			if (strcmp(argv[i], "--tmpdir") == 0) { copy_text(tmpdir, argv[++i]); tmpdir_set = 1; }
			else copy_text(output, argv[++i]);
			continue;
		}
		if (strcmp(argv[i], "--target") == 0 || strcmp(argv[i], "--frontend") == 0 || strcmp(argv[i], "--cpu") == 0) {
			if (i + 1 >= argc) { fprintf(stderr, "qcc: Option erwartet einen Wert\n"); return 2; }
			if (strcmp(argv[i], "--target") == 0) copy_text(target, argv[++i]);
			else if (strcmp(argv[i], "--frontend") == 0) copy_text(frontend, argv[++i]);
			else copy_text(cpu, argv[++i]);
			continue;
		}
		if (argv[i][0] == '-') { fprintf(stderr, "qcc: unbekannte Option: %s\n", argv[i]); return 2; }
		++inputs;
	}
	if (print_config) show_config();
	if (inputs == 0 && !print_config) { fprintf(stderr, "qcc: keine Eingabedatei (siehe --help)\n"); return 2; }
	if (inputs == 0) return 0;
	resolve_tmpdir();
	if (dry_run) {
		printf("qcc: %d Eingabe(n), Frontend %s, Target %s, CPU %s\n", inputs, frontend, target, cpu);
		printf("  1. qcpp -> .i\n  2. qcir -> .ir\n  3. %s\n", backend);
		if (optimizer[0] != '\0') printf("  4. %s (optional)\n", optimizer);
		printf("  5. %s -> .r\n  6. %s (%s; %s)\n", assembler, linker, startup, libraries);
		return 0;
	}
	if (inputs != 1 || strstr(argv[argc - 1], ".c") == NULL) {
		fprintf(stderr, "qcc: momentan genau eine .c-Eingabe unterstuetzt\n");
		return 3;
	}
	{
		char command[512];
		const char *input = argv[argc - 1];
		#if defined(_Q9OS) || defined(_OSK)
		{
			char input_i[TEXT]; char output_ir[TEXT];
			char output_s[TEXT]; char output_opt[TEXT]; char output_r[TEXT];
			/* I$Create/fopen("w") in the target runtime does not reliably
			 * replace an existing file. Clear stale products first so a second
			 * build in the same tmpdir behaves like the first. --keep preserves
			 * this run's outputs; it must not make stale inputs observable. */
			sprintf(input_i, "%s/input.i", tmpdir);
			sprintf(output_ir, "%s/output.ir", tmpdir);
			sprintf(output_s, "%s/output.s68k", tmpdir);
			sprintf(output_opt, "%s/output.opt.s68k", tmpdir);
			sprintf(output_r, "%s/output.r", tmpdir);
			q9_remove_files(input_i, output_ir, output_s, output_opt, output_r);
		}
		#endif
		if (strcmp(tmpdir, "/dd") != 0 && strcmp(tmpdir, "/dd/CMDS") != 0 && strcmp(tmpdir, ".") != 0) {
			/* 2026-09-27: auf OS-9 scheiterte "makdir -p <dir>" ueber q9_system()/system()
			 * am echten Q9-Emulator mit einem generischen E$FNA, obwohl "makdir -p <dir>"
			 * von Hand in derselben Shell klaglos funktioniert -- system()s eigener Shell-
			 * Fork scheint in dieser Umgebung nicht zuverlaessig zu sein. Der Rest der
			 * Pipeline (qcpp/qcir/...) benutzt dafuer bereits q9_exec_argv() (os9exec/
			 * os9forkc, direkter Fork+Load ohne Shell) -- denselben, bereits bewiesenen
			 * Mechanismus jetzt auch hier statt q9_system(). */
			#if defined(_Q9OS) || defined(_OSK)
			{
				/* 2026-09-27: dieses echte "makdir -p" bricht ab, wenn <dir> schon
				 * existiert -- "-p" unterdrueckt hier nur Fehler ueber fehlende
				 * Elternverzeichnisse, nicht "existiert bereits" (an diesem Emulator
				 * mit "makdir -p /r0/qcctest; makdir -p /r0/qcctest" nachgestellt:
				 * der zweite Aufruf meldet "can't make"). Das trifft jeden zweiten
				 * Pipeline-Lauf mit demselben Tmpdir, seit "del" (s.o.) die Dateien
				 * darin zuverlaessig aufraeumt und das Verzeichnis selbst bestehen
				 * bleibt. Ein fehlgeschlagenes makdir hier ist deshalb kein
				 * verlaesslicher Fehlerindikator mehr -- nicht fatal behandeln; ein
				 * WIRKLICH fehlendes/unschreibbares Tmpdir faellt beim naechsten
				 * Schritt (qcpp kann input.i nicht anlegen) ohnehin klar auf. */
				char *mkdir_argv[4];
				mkdir_argv[0] = "makdir";
				mkdir_argv[1] = "-p";
				mkdir_argv[2] = (char *)tmpdir;
				mkdir_argv[3] = 0;
				q9_exec_argv("makdir", mkdir_argv);
			}
			#else
			sprintf(command, "%s %s", QCC_MKDIR, tmpdir);
			if (q9_system(command) != 0) return 4;
			#endif
		}
		#if defined(_Q9OS) || defined(_OSK)
		{
			char *qcpp_argv[6];
			char qcpp_output[TEXT];
			char qcpp_include[TEXT];
			const char *qcpp_module = qcpp;
			const char *inc_base;
			/* 2026-09-27: der OS-9-Zweig rief qcpp bisher OHNE jeden Include-Pfad auf
			 * (der Host-Zweig unten hat "-I../Q9-FRONTEND-C/q9-qcpp/include" fest dabei) --
			 * jede #include-Datei (auch stdio.h) schlug deshalb fehl. QCC_INCLUDE
			 * (Umgebungsvariable, wie schon TMP/TEMP fuer tmpdir) erlaubt eine andere
			 * Ablage. 2026-09-27 (spaeter am selben Tag): Standardpfad auf die neue,
			 * projektunabhaengige SDK-Ablage "/dd/DEFS/Q9" umgestellt (Nutzerwunsch) --
			 * vorher zeigte er auf die tief verschachtelte Q9-FORGE-Projektkopie. */
			inc_base = qcc_getenv("QCC_INCLUDE");
			if (inc_base == NULL || inc_base[0] == '\0')
				inc_base = "/dd/DEFS/Q9";
			sprintf(qcpp_include, "-I%s", inc_base);
			sprintf(qcpp_output, "%s/input.i", tmpdir);
			qcpp_argv[0] = (char *)qcpp_module;
			qcpp_argv[1] = "-v";
			qcpp_argv[2] = qcpp_include;
			qcpp_argv[3] = (char *)input;
			qcpp_argv[4] = qcpp_output;
			qcpp_argv[5] = NULL;
			if (q9_exec_argv(qcpp_module, qcpp_argv) != 0) {
				fprintf(stderr, "qcc: qcpp fehlgeschlagen\n");
				return 4;
			}
		}
		#else
		{
			const char *inc_base;
			const char *sdk_base;
			char include_arg[TEXT * 2];
				inc_base = qcc_getenv("QCC_INCLUDE");
			if (inc_base == NULL || inc_base[0] == '\0') {
				sdk_base = qcc_getenv("Q9SDK");
				if (sdk_base != NULL && sdk_base[0] != '\0') {
					sprintf(include_arg, "%s/Q9/68k/DEFS", sdk_base);
					inc_base = include_arg;
				} else inc_base = "../Q9-FRONTEND-C/q9-qcpp/include";
			}
			sprintf(command, "%s -I%s %s %s/input.i", qcpp, inc_base, input, tmpdir);
		}
		#endif
		#if !defined(_Q9OS) && !defined(_OSK)
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: qcpp fehlgeschlagen\n"); return 4; }
		#endif
		if (preprocess_only) {
			if (output[0] != '\0') {
				#if defined(_Q9OS) || defined(_OSK)
				{
					char copy_source[TEXT];
					sprintf(copy_source, "%s/input.i", tmpdir);
					if (q9_copy_file(copy_source, output) != 0) return 4;
				}
				#else
				sprintf(command, "%s %s/input.i %s", QCC_COPY, tmpdir, output);
				if (q9_system(command) != 0) return 4;
				#endif
				printf("%s\n", output);
			} else printf("%s/input.i\n", tmpdir);
			if (!keep_files) {
				#if defined(_Q9OS) || defined(_OSK)
				{
					char input_i[TEXT];
					sprintf(input_i, "%s/input.i", tmpdir);
					q9_remove_files(input_i, NULL, NULL, NULL, NULL);
				}
				#else
				sprintf(command, "%s %s/input.i", QCC_REMOVE, tmpdir); q9_system(command);
				#endif
			}
			return 0;
		}
		#if defined(_Q9OS) || defined(_OSK)
		{
			char *qcir_argv[3];
			char qcir_input[TEXT];
			char qcir_response[TEXT];
			char qcir_output[TEXT];
			sprintf(qcir_input, "%s/input.i", tmpdir);
			sprintf(qcir_response, "@%s", qcir_input);
			if (emit_ir && output[0] != '\0') sprintf(qcir_output, "%s", output);
			else sprintf(qcir_output, "%s/output.ir", tmpdir);
			/* 2026-09-27: zwei Bugs auf einmal. (1) "/dd/CMDS/qcir" stand fest verdrahtet,
			 * existiert in dieser Ablage nicht -- wie beim qcpp-Aufruf oben die
			 * konfigurierbare Variable `qcir` benutzen (os9exec loest sie per PATH-Suche
			 * auf). (2) qcir (Q9-FRONTEND-C/q9-qcir/data/qcc_p.c, main()) liest NUR
			 * argv[1] und schreibt IMMER auf stdout -- kennt gar kein "-o"; der bisherige
			 * dritte/vierte Argv-Eintrag wurde von qcir schlicht ignoriert, und die
			 * anschliessend erwartete Ausgabedatei existierte nie. qir68k meldete deshalb
			 * zu Recht "kann IR nicht lesen" -- kein Bug in qir68k selbst. Fix: stdout
			 * wie beim schon vorhandenen (bisher nie genutzten) q9_exec_stdout() in die
			 * Zieldatei umleiten, statt eine erfundene Option zu uebergeben. Auch der
			 * `output`-Name gehoert nur bei --emit-ir zur IR-Datei -- sonst wuerde sie
			 * beim finalen ql68k-Schritt spaeter denselben Namen wie das fertige Modul
			 * benutzen und ueberschrieben werden. */
			qcir_argv[0] = (char *)qcir;
			qcir_argv[1] = qcir_response;
			qcir_argv[2] = NULL;
			if (q9_exec_stdout(qcir, qcir_argv, qcir_output) != 0) {
				fprintf(stderr, "qcc: qcir fehlgeschlagen\n");
				return 4;
			}
		}
		#else
		sprintf(command, "%s @%s/input.i > %s/output.ir", qcir, tmpdir, tmpdir);
		#endif
		#if !defined(_Q9OS) && !defined(_OSK)
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: qcir fehlgeschlagen\n"); return 4; }
		#endif
		if (emit_ir) {
			if (output[0] != '\0') {
				#if defined(_Q9OS) || defined(_OSK)
				{
					char copy_source[TEXT];
					sprintf(copy_source, "%s/output.ir", tmpdir);
					if (q9_copy_file(copy_source, output) != 0) return 4;
				}
				#else
				sprintf(command, "%s %s/output.ir %s", QCC_COPY, tmpdir, output);
				if (q9_system(command) != 0) return 4;
				#endif
				printf("%s\n", output);
			} else printf("%s/output.ir\n", tmpdir);
			if (!keep_files) {
				#if defined(_Q9OS) || defined(_OSK)
				{
					char input_i[TEXT];
					sprintf(input_i, "%s/input.i", tmpdir);
					q9_remove_files(input_i, NULL, NULL, NULL, NULL);
				}
				#else
				sprintf(command, "%s %s/input.i", QCC_REMOVE, tmpdir); q9_system(command);
				#endif
			}
			return 0;
		}
		if (assembly_only || object_only) {
			#if defined(_Q9OS) || defined(_OSK)
			{
				char *stage_argv[7];
				char stage_in[TEXT]; char stage_out[TEXT];
				int sa;
				sprintf(stage_in, "%s/output.ir", tmpdir);
				sprintf(stage_out, "%s/output.s68k", tmpdir);
				stage_argv[0] = (char *)backend; stage_argv[1] = stage_in;
				stage_argv[2] = stage_out; stage_argv[3] = "-os9";
				sa = 4;
				if (large_data) {
					stage_argv[sa++] = "-largedata";
					stage_argv[sa++] = "-remotedata";
				}
				stage_argv[sa] = NULL;
				if (q9_exec_argv(backend, stage_argv) != 0) { fprintf(stderr, "qcc: qir68k fehlgeschlagen\n"); return 4; }
			}
			#else
			sprintf(command, "%s %s/output.ir %s/output.s68k -os9%s", backend, tmpdir, tmpdir, large_data ? " -largedata -remotedata" : "");
			if (q9_system(command) != 0) { fprintf(stderr, "qcc: qir68k fehlgeschlagen\n"); return 4; }
			#endif
			if (optimizer[0] != '\0') {
				#if defined(_Q9OS) || defined(_OSK)
				{
					char *stage_argv[4];
					char stage_in[TEXT]; char stage_out[TEXT];
					sprintf(stage_in, "%s/output.s68k", tmpdir);
					sprintf(stage_out, "%s/output.opt.s68k", tmpdir);
					stage_argv[0] = (char *)optimizer; stage_argv[1] = stage_in;
					stage_argv[2] = stage_out; stage_argv[3] = NULL;
					if (q9_exec_argv(optimizer, stage_argv) != 0) { fprintf(stderr, "qcc: qo68k fehlgeschlagen\n"); return 4; }
				}
				#else
				sprintf(command, "%s %s/output.s68k %s/output.opt.s68k", optimizer, tmpdir, tmpdir);
				if (q9_system(command) != 0) { fprintf(stderr, "qcc: qo68k fehlgeschlagen\n"); return 4; }
				#endif
			}
			if (object_only) {
				#if defined(_Q9OS) || defined(_OSK)
				{
					char *stage_argv[4];
					char stage_in[TEXT]; char stage_out[TEXT];
					int stage_status;
					sprintf(stage_in, "%s/%s", tmpdir, optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k");
					sprintf(stage_out, "%s/output.r", tmpdir);
					stage_argv[0] = (char *)assembler; stage_argv[1] = stage_in;
					stage_argv[2] = stage_out; stage_argv[3] = NULL;
					stage_status = q9_exec_argv(assembler, stage_argv);
					if (stage_status != 0) {
						if (stage_status < 0)
							fprintf(stderr, "qcc: qr68k Fork fehlgeschlagen (E$%d)\n", -stage_status);
						else
							fprintf(stderr, "qcc: qr68k beendet mit Status %d\n", stage_status);
						return 4;
					}
				}
				#else
				sprintf(command, "%s %s/%s %s/output.r", assembler, tmpdir, optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k", tmpdir);
				if (q9_system(command) != 0) { fprintf(stderr, "qcc: qr68k fehlgeschlagen\n"); return 4; }
				#endif
			}
			if (output[0] != '\0') {
				#if defined(_Q9OS) || defined(_OSK)
				{
					char copy_source[TEXT];
					sprintf(copy_source, "%s/%s", tmpdir,
						object_only ? "output.r" : (optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k"));
					if (q9_copy_file(copy_source, output) != 0) return 4;
				}
				#else
				sprintf(command, "%s %s/%s %s", QCC_COPY, tmpdir, object_only ? "output.r" : (optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k"), output);
				if (q9_system(command) != 0) return 4;
				#endif
			}
			if (!keep_files) {
				#if defined(_Q9OS) || defined(_OSK)
				{
					char input_i[TEXT]; char output_ir[TEXT];
					sprintf(input_i, "%s/input.i", tmpdir);
					sprintf(output_ir, "%s/output.ir", tmpdir);
					q9_remove_files(input_i, output_ir, NULL, NULL, NULL);
				}
				#else
				sprintf(command, "%s %s/input.i %s/output.ir", QCC_REMOVE, tmpdir, tmpdir); q9_system(command);
				#endif
			}
			return 0;
		}
		/* Complete default pipeline: backend, optimizer, assembler, linker. */
		#if defined(_Q9OS) || defined(_OSK)
		{
			char *stage_argv[7]; char stage_in[TEXT]; char stage_out[TEXT];
			int sa;
			sprintf(stage_in, "%s/output.ir", tmpdir); sprintf(stage_out, "%s/output.s68k", tmpdir);
			stage_argv[0] = (char *)backend; stage_argv[1] = stage_in; stage_argv[2] = stage_out; stage_argv[3] = "-os9";
			sa = 4;
			if (large_data) {
				stage_argv[sa++] = "-largedata";
				stage_argv[sa++] = "-remotedata";
			}
			stage_argv[sa] = NULL;
			if (q9_exec_argv(backend, stage_argv) != 0) { fprintf(stderr, "qcc: qir68k fehlgeschlagen\n"); return 4; }
		}
		#else
		sprintf(command, "%s %s/output.ir %s/output.s68k -os9%s", backend, tmpdir, tmpdir, large_data ? " -largedata -remotedata" : "");
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: qir68k fehlgeschlagen\n"); return 4; }
		#endif
		if (optimizer[0] != '\0') {
			#if defined(_Q9OS) || defined(_OSK)
			{
				char *stage_argv[4]; char stage_in[TEXT]; char stage_out[TEXT];
				sprintf(stage_in, "%s/output.s68k", tmpdir); sprintf(stage_out, "%s/output.opt.s68k", tmpdir);
				stage_argv[0] = (char *)optimizer; stage_argv[1] = stage_in; stage_argv[2] = stage_out; stage_argv[3] = NULL;
				if (q9_exec_argv(optimizer, stage_argv) != 0) { fprintf(stderr, "qcc: qo68k fehlgeschlagen\n"); return 4; }
			}
			#else
			sprintf(command, "%s %s/output.s68k %s/output.opt.s68k", optimizer, tmpdir, tmpdir);
			if (q9_system(command) != 0) { fprintf(stderr, "qcc: qo68k fehlgeschlagen\n"); return 4; }
			#endif
		}
		#if defined(_Q9OS) || defined(_OSK)
		{
			char *stage_argv[4]; char stage_in[TEXT]; char stage_out[TEXT];
			sprintf(stage_in, "%s/%s", tmpdir, optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k"); sprintf(stage_out, "%s/output.r", tmpdir);
			stage_argv[0] = (char *)assembler; stage_argv[1] = stage_in; stage_argv[2] = stage_out; stage_argv[3] = NULL;
			if (q9_exec_argv(assembler, stage_argv) != 0) { fprintf(stderr, "qcc: qr68k fehlgeschlagen\n"); return 4; }
		}
		#else
		sprintf(command, "%s %s/%s %s/output.r", assembler, tmpdir, optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k", tmpdir);
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: qr68k fehlgeschlagen\n"); return 4; }
		#endif
		#if defined(_Q9OS) || defined(_OSK)
		{
			/* 2026-09-27: dieselbe Klasse Fehler wie qcir/qir68k/qo68k/qr68k oben --
			 * "/dd/CMDS/{ql68k,q9_start.r,qclib.l,output.mod}" existieren dort nicht.
			 * `linker` ist bereits die konfigurierbare Variable (Default "ql68k") und wird
			 * ueber os9exec's eigene Modulsuche (PATH/chx) gefunden -- das funktioniert nur
			 * fuer AUSFUEHRBARE Module. q9_start.r und qclib.l sind aber reine Daten-
			 * dateien, die ql68k selbst per fopen() oeffnet; fopen() loest bare/relative
			 * Namen gegen das aktuelle DATENverzeichnis (chd) auf, nicht gegen die Exec-
			 * Liste -- als bare Namen fanden sie sich dort so gut wie nie, und ql68k schlug
			 * (bislang unsichtbar, siehe q9_exec_stdout-Kommentar bei qcir) fehl, ohne dass
			 * "output.mod" je entstand. Fix: beide ueber QCC_LIBDIR zu absoluten Pfaden
			 * machen, analog zu QCC_INCLUDE oben bei qcpp. 2026-09-27 (spaeter am
			 * selben Tag): Standardpfad auf die neue, projektunabhaengige SDK-Ablage
			 * "/dd/LIBS/Q9" umgestellt (Nutzerwunsch), vorher "/dd/CMDS_XCC" (dort
			 * lagen Werkzeuge und Bibliotheken gemischt durcheinander). */
			char *stage_argv[8]; char stage_in[TEXT]; char stage_out[TEXT]; char stack_arg[TEXT];
			char cstart_path[TEXT]; char qclib_arg[TEXT];
			const char *libdir;
			libdir = qcc_getenv("QCC_LIBDIR");
			if (libdir == NULL || libdir[0] == '\0') libdir = "/dd/LIBS/Q9";
			sprintf(cstart_path, "%s/q9_start.r", libdir);
			sprintf(qclib_arg, "-l=%s/qclib.l", libdir);
			sprintf(stage_in, "%s/output.r", tmpdir);
			sprintf(stage_out, "-O=%s", output[0] != '\0' ? output : "output.mod");
			stage_argv[0] = (char *)linker; stage_argv[1] = cstart_path;
			stage_argv[2] = stage_in; stage_argv[3] = qclib_arg;
			stage_argv[4] = stage_out;
			/* 2026-09-27: "ql68: Bezug zu weit fuer ein Wort, l68 braucht dafuer
			 * -a: tc_div_i32" -- sobald ein Programm gross genug wird (gemessen an
			 * q9-qclib/tests/hello.c), reicht ein 16-Bit-Wort fuer den BSR zu den
			 * qir68k-eigenen Laufzeithelfern (tc_div_i32 & Co.) nicht mehr. -a
			 * schaltet ql68ks Sprungtabelle fuer genau diesen Fall ein (s. ql68
			 * -help) -- ohne erkennbaren Nachteil fuer kleine Module, also immer
			 * mitgeben statt erst ab einer gemessenen Groesse. */
			stage_argv[5] = "-a";
			if (stack_size[0] != '\0') {
				sprintf(stack_arg, "-M=%s", stack_size);
				stage_argv[6] = stack_arg;
				stage_argv[7] = NULL;
			} else {
				stage_argv[6] = NULL; stage_argv[7] = NULL;
			}
			if (q9_exec_argv(linker, stage_argv) != 0) {
				fprintf(stderr, "qcc: ql68k fehlgeschlagen\n");
				return 4;
			}
		}
		#else
		{
			const char *libdir;
			const char *sdk_base;
			char libdir_default[TEXT * 2];
			libdir = qcc_getenv("QCC_LIBDIR");
			if (libdir == NULL || libdir[0] == '\0') {
				sdk_base = qcc_getenv("Q9SDK");
				if (sdk_base != NULL && sdk_base[0] != '\0') {
					sprintf(libdir_default, "%s/Q9/68k/LIBS", sdk_base);
					libdir = libdir_default;
				} else libdir = "../Q9-BACKEND-68K/q9-qclib/build";
			}
			if (stack_size[0] != '\0')
				sprintf(command, "%s %s/q9_start.r %s/output.r -l=%s/qclib.l -a -M=%s -O=%s", linker, libdir, tmpdir, libdir, stack_size, output[0] != '\0' ? output : "build/qcc-tmp/output.mod");
			else
				sprintf(command, "%s %s/q9_start.r %s/output.r -l=%s/qclib.l -a -O=%s", linker, libdir, tmpdir, libdir, output[0] != '\0' ? output : "build/qcc-tmp/output.mod");
		}
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: ql68k fehlgeschlagen\n"); return 4; }
		#endif
		if (!keep_files) {
			#if defined(_Q9OS) || defined(_OSK)
			{
				char input_i[TEXT]; char output_ir[TEXT]; char output_s68k[TEXT];
				char output_opt[TEXT]; char output_r[TEXT];
				sprintf(input_i, "%s/input.i", tmpdir);
				sprintf(output_ir, "%s/output.ir", tmpdir);
				sprintf(output_s68k, "%s/output.s68k", tmpdir);
				sprintf(output_r, "%s/output.r", tmpdir);
				/* output.opt.s68k only exists when the optimizer actually ran;
				 * passing a nonexistent name to "del" is what produced the E$FNA
				 * seen at every pipeline run regardless of --no-optimizer. */
				if (optimizer[0] != '\0') {
					sprintf(output_opt, "%s/output.opt.s68k", tmpdir);
					q9_remove_files(input_i, output_ir, output_s68k, output_opt, output_r);
				} else {
					q9_remove_files(input_i, output_ir, output_s68k, output_r, NULL);
				}
			}
			#else
			sprintf(command, "%s %s/input.i %s/output.ir %s/output.s68k %s/output.opt.s68k %s/output.r", QCC_REMOVE, tmpdir, tmpdir, tmpdir, tmpdir, tmpdir);
			q9_system(command);
			#endif
		}
		if (output[0] != '\0') {
			printf("%s\n", output);
		} else {
			printf("%s/output.mod\n", tmpdir);
		}
	}
	return 0;
}
