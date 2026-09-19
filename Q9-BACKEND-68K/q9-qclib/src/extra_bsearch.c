typedef int (*QSearchCmp)(char*, char*);

char *qf_bsearch(char *key, char *base, int count, int size, QSearchCmp cmp)
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
