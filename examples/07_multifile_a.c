/* Mehrdatei-Beispiel, Teil A: benutzt Teil B. Zusammen mit 07_multifile_b.c
   zeigt dies FUNCDECL/GLOBALDECL -- reine Vorwaertsdeklarationen ohne
   Rumpf, die QCCVM nicht selbst ausfuehren kann (siehe IR_OPCODES_de.md),
   sondern die erst ein "Linker" aufloesen muss. tools/qcc_merge.py
   uebernimmt das fuer QCCVM: siehe README.md.
   WICHTIG: eine Funktion braucht dafuer einen Prototyp OHNE "extern"
   ("int step(int n);") -- "extern int step(int n);" waere die AELTERE,
   eigenstaendige Funktion fuer echte Microware-clib-Aufrufe (CALLEXT) und
   damit etwas ganz anderes. Nur bei GLOBALEN VARIABLEN bedeutet "extern"
   tatsaechlich "in einer anderen QCC-Datei definiert" (GLOBALDECL). */
extern int shared_counter;
int step(int n);

int run(int start) {
    shared_counter = start;
    return step(shared_counter);
}

int main(void) {
    putint(run(41)); /* erwartet: 42 */
    return 0;
}
