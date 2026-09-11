/* qcc - universeller Q9-Treiber, Konfigurationskern */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define QCC_VERSION "0.2.0-dev"
#define TEXT 128

static char target[TEXT] = "OS9-68K";
static char frontend[TEXT] = "c";
static char cpu[TEXT] = "68000";
static char optimizer[TEXT] = "qo68k";
static char backend[TEXT] = "qir68k";
static char assembler[TEXT] = "qr68k";
static char linker[TEXT] = "ql68k";
static char startup[TEXT] = "cstart.a";
static char libraries[TEXT] = "clib.l,os9.l,sys.l";
static char qcpp[TEXT] = "../Q9-FRONTEND-C/q9-qcpp/build/qcpp";
static char qcir[TEXT] = "../Q9-FRONTEND-C/q9-qcir/build/qcir";
static int dry_run;
static int print_config;
static int emit_ir;
static int preprocess_only;
static int assembly_only;
static int object_only;
static int keep_files;
static char tmpdir[TEXT] = "build/qcc-tmp";
static char output[TEXT] = "";
static int tmpdir_set;
static char config_section[TEXT] = "global";

static void copy_text(char *dst, const char *src) { strncpy(dst, src, TEXT - 1); dst[TEXT - 1] = '\0'; }
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
static void show_config(void)
{
	printf("target=%s\nfrontend=%s\ncpu=%s\nbackend=%s\noptimizer=%s\n", target, frontend, cpu, backend, optimizer);
	printf("assembler=%s\nlinker=%s\nstartup=%s\nlibraries=%s\nqcpp=%s\nqcir=%s\n", assembler, linker, startup, libraries, qcpp, qcir);
}
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
		sprintf(command, "mkdir -p %s", tmpdir);
		if (system(command) != 0) return 4;
		sprintf(command, "%s -I../Q9-FRONTEND-C/q9-qcpp/include %s %s/input.i", qcpp, input, tmpdir);
		if (system(command) != 0) { fprintf(stderr, "qcc: qcpp fehlgeschlagen\n"); return 4; }
		if (preprocess_only) {
			if (output[0] != '\0') {
				sprintf(command, "cp %s/input.i %s", tmpdir, output);
				if (system(command) != 0) return 4;
				printf("%s\n", output);
			} else printf("%s/input.i\n", tmpdir);
			if (!keep_files) { sprintf(command, "rm -f %s/input.i", tmpdir); system(command); }
			return 0;
		}
		sprintf(command, "%s @%s/input.i > %s/output.ir", qcir, tmpdir, tmpdir);
		if (system(command) != 0) { fprintf(stderr, "qcc: qcir fehlgeschlagen\n"); return 4; }
		if (assembly_only) {
			sprintf(command, "../Q9-BACKEND-68K/q9-qir68k/build/qir68k %s/output.ir %s/output.s68k", tmpdir, tmpdir);
			if (system(command) != 0) { fprintf(stderr, "qcc: qir68k fehlgeschlagen\n"); return 4; }
			if (optimizer[0] != '\0') {
				sprintf(command, "../Q9-BACKEND-68K/q9-qo68k/build/qo68k %s/output.s68k %s/output.opt.s68k", tmpdir, tmpdir);
				if (system(command) != 0) { fprintf(stderr, "qcc: qo68k fehlgeschlagen\n"); return 4; }
			}
			if (output[0] != '\0') {
				sprintf(command, "cp %s/%s %s", tmpdir, optimizer[0] != '\0' ? "output.opt.s68k" : "output.s68k", output);
				if (system(command) != 0) return 4;
			}
			if (!keep_files) { sprintf(command, "rm -f %s/input.i %s/output.ir", tmpdir, tmpdir); system(command); }
			return 0;
		}
		if (assembly_only || object_only) {
			fprintf(stderr, "qcc: diese Stopppunkt-Stufe ist noch nicht implementiert\n");
			return 3;
		}
		if (output[0] != '\0') {
			sprintf(command, "cp %s/output.ir %s", tmpdir, output);
			if (system(command) != 0) { fprintf(stderr, "qcc: Ausgabedatei konnte nicht geschrieben werden\n"); return 4; }
		}
		if (!keep_files && !emit_ir) {
			sprintf(command, "rm -f %s/input.i %s/output.ir", tmpdir, tmpdir);
			if (system(command) != 0) return 4;
		}
		if (output[0] != '\0') printf("%s\n", output); else printf("%s/output.ir\n", tmpdir);
	}
	return 0;
}
