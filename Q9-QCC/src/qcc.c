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
#define QCC_MKDIR "makdir -e"
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

/* Platform abstraction: the host currently uses the C89 system() function.
 * The OS-9 runtime can later replace q9_system with F$Fork/F$Load. */
/* Function: q9_system
 * Executes one configured pipeline command.
 * Parameters: command Shell command line.
 * Returns: Command status returned by the runtime. */
static int q9_system(const char *command)
{
	return system(command);
}
static int keep_files;
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
	printf("  -E               nur vorverarbeiten\n  -S               nur Assembler erzeugen\n  -c               nur Objektdatei erzeugen\n");
	printf("  --no-optimizer   Optimierer ueberspringen\n");
	printf("  --help, --version\n");
}
/* Function: config_line
 * Parses one configuration line and updates the active target settings.
 * Parameters: line Mutable configuration line.
 * Returns: Nothing. */
static void config_line(char *line)
{
	char key[TEXT], value[TEXT], section[TEXT];
	if (sscanf(line, "[%127[^]]", section) == 1) { copy_text(config_section, section); return; }
	if (sscanf(line, "%127[^=]=%127s", key, value) != 2) return;
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
	f = fopen("config/qcc.conf", "r");
	if (f == NULL) f = fopen("Q9-QCC/config/qcc.conf", "r");
	if (f == NULL) f = fopen("/dd/SYS/qcc.conf", "r");
	if (f == NULL) return;
	while (fgets(line, sizeof(line), f) != NULL) config_line(line);
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
	base = getenv("TMP");
	if (base == NULL || base[0] == '\0') base = getenv("TEMP");
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
		if (strcmp(tmpdir, "/dd") != 0) {
			sprintf(command, "%s %s", QCC_MKDIR, tmpdir);
			if (q9_system(command) != 0) return 4;
		}
		#if defined(_Q9OS) || defined(_OSK)
		sprintf(command, "%s -I/dd/DEFS/Q9 %s %s/input.i", qcpp, input, tmpdir);
		#else
		sprintf(command, "%s -I../Q9-FRONTEND-C/q9-qcpp/include %s %s/input.i", qcpp, input, tmpdir);
		#endif
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: qcpp fehlgeschlagen\n"); return 4; }
		if (preprocess_only) {
			if (output[0] != '\0') {
				sprintf(command, "%s %s/input.i %s", QCC_COPY, tmpdir, output);
				if (q9_system(command) != 0) return 4;
				printf("%s\n", output);
			} else printf("%s/input.i\n", tmpdir);
			if (!keep_files) { sprintf(command, "%s %s/input.i", QCC_REMOVE, tmpdir); q9_system(command); }
			return 0;
		}
		sprintf(command, "%s @%s/input.i > %s/output.ir", qcir, tmpdir, tmpdir);
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: qcir fehlgeschlagen\n"); return 4; }
		if (emit_ir) {
			if (output[0] != '\0') {
				sprintf(command, "%s %s/output.ir %s", QCC_COPY, tmpdir, output);
				if (q9_system(command) != 0) return 4;
				printf("%s\n", output);
			} else printf("%s/output.ir\n", tmpdir);
			if (!keep_files) { sprintf(command, "%s %s/input.i", QCC_REMOVE, tmpdir); q9_system(command); }
			return 0;
		}
		if (assembly_only || object_only) {
			sprintf(command, "../Q9-BACKEND-68K/q9-qir68k/build/qir68k %s/output.ir %s/output.s68k -os9", tmpdir, tmpdir);
			if (q9_system(command) != 0) { fprintf(stderr, "qcc: qir68k fehlgeschlagen\n"); return 4; }
			if (optimizer[0] != '\0') {
				sprintf(command, "../Q9-BACKEND-68K/q9-qo68k/build/qo68k %s/output.s68k %s/output.opt.s68k", tmpdir, tmpdir);
				if (q9_system(command) != 0) { fprintf(stderr, "qcc: qo68k fehlgeschlagen\n"); return 4; }
			}
			if (object_only) {
				sprintf(command, "../Q9-BACKEND-68K/q9-qr68k/build/qr68k %s/%s %s/output.r", tmpdir, optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k", tmpdir);
				if (q9_system(command) != 0) { fprintf(stderr, "qcc: qr68k fehlgeschlagen\n"); return 4; }
			}
			if (output[0] != '\0') {
				sprintf(command, "%s %s/%s %s", QCC_COPY, tmpdir, object_only ? "output.r" : (optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k"), output);
				if (q9_system(command) != 0) return 4;
			}
			if (!keep_files) { sprintf(command, "%s %s/input.i %s/output.ir", QCC_REMOVE, tmpdir, tmpdir); q9_system(command); }
			return 0;
		}
		/* Complete default pipeline: backend, optimizer, assembler, linker. */
		sprintf(command, "../Q9-BACKEND-68K/q9-qir68k/build/qir68k %s/output.ir %s/output.s68k -os9", tmpdir, tmpdir);
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: qir68k fehlgeschlagen\n"); return 4; }
		if (optimizer[0] != '\0') {
			sprintf(command, "../Q9-BACKEND-68K/q9-qo68k/build/qo68k %s/output.s68k %s/output.opt.s68k", tmpdir, tmpdir);
			if (q9_system(command) != 0) { fprintf(stderr, "qcc: qo68k fehlgeschlagen\n"); return 4; }
		}
		sprintf(command, "../Q9-BACKEND-68K/q9-qr68k/build/qr68k %s/%s %s/output.r", tmpdir, optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k", tmpdir);
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: qr68k fehlgeschlagen\n"); return 4; }
		sprintf(command, "../Q9-BACKEND-68K/q9-ql68k/build/ql68k ../Q9-BACKEND-68K/q9-qclib/build/q9_cstart.r %s/output.r -l=../Q9-BACKEND-68K/q9-qclib/build/qclib.l -O=%s", tmpdir, output[0] != '\0' ? output : "build/qcc-tmp/output.mod");
		if (q9_system(command) != 0) { fprintf(stderr, "qcc: ql68k fehlgeschlagen\n"); return 4; }
		if (!keep_files) {
			sprintf(command, "%s %s/input.i %s/output.ir %s/output.s68k %s/output.opt.s68k %s/output.r", QCC_REMOVE, tmpdir, tmpdir, tmpdir, tmpdir, tmpdir);
			q9_system(command);
		}
		if (output[0] != '\0') {
			printf("%s\n", output);
		} else {
			printf("%s/output.mod\n", tmpdir);
		}
	}
	return 0;
}
