#include <stdio.h>
#include <stdlib.h>
int main()
{
    char s[4]; int w[4]; char out[4]; int a; int b; int i;
    s[0]='A'; s[1]='b'; s[2]=0; s[3]=0;
    a=mbstowcs(w,s,4); b=wcstombs(out,w,4);
    printf("CASE mbs %d %d %d %d %d\n",a,b,w[0],w[1],out[1]&255);
    printf("CASE count %d %d\n",mbstowcs(0,s,1),wcstombs(0,w,1));
    printf("CASE done\n");
    return 0;
}
