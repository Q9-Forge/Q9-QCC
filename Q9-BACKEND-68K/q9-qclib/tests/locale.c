#include <stdio.h>
#include <locale.h>
int main()
{
    char *a; char *b; char *c;
    a=setlocale(0,0); b=setlocale(0,"C"); c=setlocale(0,"de_DE");
    printf("CASE locale %d %d %d %d\n",a!=0,a[0],b!=0,c==0);
    printf("CASE done\n");
    return 0;
}
