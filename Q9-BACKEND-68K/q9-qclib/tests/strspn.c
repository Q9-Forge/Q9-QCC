#include <stdio.h>
#include <string.h>
int main()
{
    char s[9]; char set[6]; char values[5];
    int mask; int len; int i; int k; int bit; int r;
    values[0]=1; values[1]=65; values[2]=128; values[3]=255; values[4]=42;
    mask=0;
    while(mask<32) {
        i=0; k=0; bit=1;
        while(i<5) {
            if((mask & bit)!=0) { set[k]=values[i]; k++; }
            bit=bit*2; i++;
        }
        set[k]=0;
        len=0;
        while(len<=8) {
            i=0;
            while(i<len) { s[i]=values[i%4]; i++; }
            s[len]=0;
            r=strspn(s,set);
            printf("CASE span %d %d %d\n",mask,len,r);
            len++;
        }
        mask++;
    }
    printf("CASE done\n");
    return 0;
}
