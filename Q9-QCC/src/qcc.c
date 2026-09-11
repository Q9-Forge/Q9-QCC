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
static char qcpp[TEXT] = "Q9-FRONTEND-C/q9-qcpp/build/qcpp";
static char qcir[TEXT] = "Q9-FRONTEND-C/q9-qcir/build/qcir";
static int dry_run;
static int print_config;
static int emit_ir;
static char config_section[TEXT] = "global";

static void copy_text(char *dst, const char *src) { strncpy(dst, src, TEXT - 1); dst[TEXT - 1] = '\0'; }
static void usage(const char *name)
{
	printf("Usage: %s [options] input...\n", name);
	printf("  --target NAME    Zielprofil\n  --frontend NAME  Frontend (Standard: c)\n");
	printf("  --cpu TYPE       CPU an passende Werkzeuge weiterreichen\n");
	printf("  --dry-run        Pipeline nur anzeigen\n  --print-config   Konfiguration anzeigen\n");
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
	f = fopen("Q9-QCC/config/qcc.conf", "r");
	if (f == NULL) f = fopen("/dd/SYS/qcc.conf", "r");
	if (f == NULL) return;
	while (fgets(line, sizeof(line), f) != NULL) config_line(line);
	fclose(f);
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
		if (strcmp(argv[i], "--print-config") == 0) { print_config = 1; continue; }
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
		if (system("mkdir -p build/qcc") != 0) return 4;
		sprintf(command, "%s -I Q9-FRONTEND-C/q9-qcpp/include %s build/qcc/input.i", qcpp, input);
		if (system(command) != 0) { fprintf(stderr, "qcc: qcpp fehlgeschlagen\n"); return 4; }
		if (emit_ir) sprintf(command, "%s @build/qcc/input.i", qcir);
		else sprintf(command, "%s @build/qcc/input.i > build/qcc/output.ir", qcir);
		if (system(command) != 0) { fprintf(stderr, "qcc: qcir fehlgeschlagen\n"); return 4; }
		if (emit_ir) printf("build/qcc/input.i\n"); else printf("build/qcc/output.ir\n");
	}
	return 0;
}
