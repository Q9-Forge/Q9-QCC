#include <stdio.h>
#include <stdlib.h>
int main()
{
    int i;
    srand(1234);
    i=0;
    while(i<8) { printf("CASE rand %d\n",rand()); i++; }
    srand(1234);
    printf("CASE repeat %d %d\n",rand(),rand());
    printf("CASE done\n");
    return 0;
}
