#include <stdio.h>
#include <stdlib.h>
int main()
{
    int n;
    n=-1000;
    while(n<=1000) { printf("CASE abs %d %d\n",n,abs(n)); n=n+17; }
    printf("CASE zero %d\n",abs(0));
    printf("CASE limits %d %d\n",abs(-2147483647),abs(2147483647));
    printf("CASE done\n");
    return 0;
}
