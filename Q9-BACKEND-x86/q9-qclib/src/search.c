/* CPU-neutral sorting/search logic from the 68k CLib, with cdecl callbacks. */
typedef int (*QSortCmp)(char*, char*);

void qsort(char *base, int count, int size, QSortCmp cmp)
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

typedef int (*QSearchCmp)(char*, char*);

char *bsearch(char *key, char *base, int count, int size, QSearchCmp cmp)
{
    int lo;
    int hi;
    int mid;
    int c;
    char *p;

    lo = 0;
    hi = count;
    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        p = base + mid * size;
        c = cmp(key, p);
        if (c == 0)
            return p;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

