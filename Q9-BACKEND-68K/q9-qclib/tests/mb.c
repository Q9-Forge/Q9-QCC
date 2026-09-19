#include <stdio.h>
#include <stdlib.h>
int main()
{
    char s[4]; int w; char out[4]; int a; int b; int c;
    s[0]='A'; s[1]=0; w=0; out[0]=0;
    a=mblen(s,1); b=mbtowc(&w,s,1); c=wctomb(out,'Z');
    printf("CASE mb %d %d %d %d %d %d\n",a,b,c,w,out[0]&255,mblen(s,0));
    printf("CASE done\n");
    return 0;
}
