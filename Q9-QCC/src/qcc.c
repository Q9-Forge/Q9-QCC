/*
 * qcc - universeller Q9-Treiber
 *
 * Bewusst nur der portable Kommandozeilenkern. Die eigentliche
 * Werkzeugkette wird in den folgenden Schritten als Pipeline ergänzt.
 */
#include <stdio.h>
#include <string.h>

#define QCC_VERSION "0.1.0-dev"

static void usage(const char *name)
{
	printf("Usage: %s [options] input...\n", name);
	printf("Options:\n");
	printf("  --target ARCH   Zielarchitektur (z. B. 68k, x86, arm64)\n");
	printf("  --help           diese Hilfe anzeigen\n");
	printf("  --version        Version anzeigen\n");
}

int main(int argc, char **argv)
{
	int i;
	int inputs = 0;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--version") == 0) {
			printf("qcc %s\n", QCC_VERSION);
			return 0;
		}
		if (strcmp(argv[i], "--target") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "qcc: --target erwartet eine Architektur\n");
				return 2;
			}
			++i;
			continue;
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "qcc: unbekannte Option: %s\n", argv[i]);
			return 2;
		}
		++inputs;
	}

	if (inputs == 0) {
		fprintf(stderr, "qcc: keine Eingabedatei (siehe --help)\n");
		return 2;
	}

	fprintf(stderr, "qcc: Pipeline noch nicht implementiert; Eingaben erkannt: %d\n", inputs);
	return 3;
}
