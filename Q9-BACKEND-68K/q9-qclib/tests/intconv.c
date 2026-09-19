#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main()
{
    printf("CASE conv %ld %ld %ld %d %d %d\n", atol(" -2147483647x"), labs(-2147483647), labs(2147483647), strcoll("a","b"), strcoll("b","a"), strcoll("a","a"));
    printf("CASE done\n");
    return 0;
}
