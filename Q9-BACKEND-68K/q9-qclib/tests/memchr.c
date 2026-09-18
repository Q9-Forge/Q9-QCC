#include <stdio.h>
#include <string.h>
int main()
{
    char a[8]; char *p;
    int i; int c; int n;
    i=0;
    while(i<8) { a[i]=(i%4)*85; i++; }
    c=-256;
    while(c<=511) {
        n=0;
        while(n<=8) {
            p=memchr(a,c,n);
            i=-1;
            if(p!=0) i=p-a;
            printf("CASE chr %d %d %d\n",c,n,i);
            n++;
        }
        c=c+17;
    }
    printf("CASE done\n");
    return 0;
}
