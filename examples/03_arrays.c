/* Lokale und globale Arrays. Zeigt: LARRAY/GARRAY/GINIT,
   LOADIDX/STOREIDX (indizierter Zugriff), Adressbildung PUSHADDR. */
int global_table[4];

int fill_and_sum(int n) {
    int local_table[4];
    int i, total;
    i = 0;
    while (i < n) {
        local_table[i] = i * 2;
        global_table[i] = i;
        i = i + 1;
    }
    total = 0;
    i = 0;
    while (i < n) {
        total = total + local_table[i] + global_table[i];
        i = i + 1;
    }
    return total;
}

int main(void) {
    putint(fill_and_sum(4)); /* local: 0+2+4+6=12, global: 0+1+2+3=6 -> 18 */
    return 0;
}
