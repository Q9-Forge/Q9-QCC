#include <stdio.h>
#include <string.h>

int sign(int n) { if (n < 0) return -1; if (n > 0) return 1; return 0; }
int main()
{
    char a[5];
    char b[5];
    int i;
    int j;
    i = 0;
    while (i < 5) { a[i] = i * 63; b[i] = a[i]; i++; }
    printf("CASE empty %d equal %d\n", memcmp(a,b,0), memcmp(a,b,5));
    i = 0;
    while (i < 5) {
        b[i] = (a[i] + 128) & 255;
        j = 0;
        while (j <= 5) {
            printf("CASE cmp %d %d %d %d\n", i, j, sign(memcmp(a,b,j)), sign(memcmp(b,a,j)));
            j++;
        }
        b[i] = a[i];
        i++;
    }
    printf("CASE done\n");
    return 0;
}
