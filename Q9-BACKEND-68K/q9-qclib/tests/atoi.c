#include <stdio.h>
#include <stdlib.h>
int main()
{
    printf("CASE atoi %d %d %d %d %d %d\n", atoi("0"), atoi("  -42x"), atoi("+17"), atoi("x9"), atoi("\t23"), atoi("-2147483647"));
    printf("CASE done\n");
    return 0;
}
