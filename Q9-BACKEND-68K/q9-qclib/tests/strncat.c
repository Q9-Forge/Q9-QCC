#include <stdio.h>
#include <string.h>
int main()
{
    char a[24]; char b[8]; char *r;
    int d; int s; int n; int i;
    d=0;
    while(d<5) {
        s=0;
        while(s<5) {
            i=0;
            while(i<s) { b[i]=128+i; i++; }
            b[s]=0;
            n=0;
            while(n<=7) {
                i=0;
                while(i<24) { a[i]=42; i++; }
                a[d]=0;
                r=strncat(a,b,n);
                printf("CASE cat %d %d %d %d:",d,s,n,r==a);
                i=0;
                while(i<24) { printf(" %d",a[i]&255); i++; }
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
