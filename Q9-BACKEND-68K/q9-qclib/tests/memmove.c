#include <stdio.h>
#include <string.h>
int main()
{
    char a[32];
    char *r;
    int d; int s; int n; int i;
    d = 0;
    while (d < 8) {
        s = 0;
        while (s < 8) {
            n = 0;
            while (n <= 16) {
                i = 0;
                while (i < 32) { a[i] = i * 17; i++; }
                r = memmove(a+d,a+s,n);
                printf("CASE move %d %d %d return %d:",d,s,n,r==a+d);
                i = 0;
                while (i < 32) { printf(" %d",a[i]&255); i++; }
                printf("\n");
                n++;
            }
            s++;
        }
        d++;
    }
    printf("CASE done\n");
    return 0;
}
