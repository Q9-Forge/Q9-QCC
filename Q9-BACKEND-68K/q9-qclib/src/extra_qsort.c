typedef int (*QSortCmp)(char*, char*);

void qf_qsort(char *base, int count, int size, QSortCmp cmp)
{
    int i;
    int j;
    int k;
    char *x;
    char *y;
    char t;
    i = 1;
    while (i < count) {
        j = i;
        while (j > 0) {
            x = base + (j - 1) * size;
            y = base + j * size;
            if (cmp(x, y) <= 0)
                break;
            k = 0;
            while (k < size) {
                t = x[k];
                x[k] = y[k];
                y[k] = t;
                k++;
            }
            j--;
        }
        i++;
    }
}
