#include <stdio.h>
#include <string.h>
int main()
{
    char d[16];
    int n; int i; int r;
    n=0;
    while(n<=12) {
        i=0; while(i<16) { d[i]=42; i++; }
        r=strxfrm(d,"alpha",n);
        printf("CASE xfrm %d %d:",n,r);
        i=0; while(i<8) { printf(" %d",d[i]&255); i++; }
        printf("\n");
        n++;
    }
    printf("CASE done\n");
    return 0;
}
