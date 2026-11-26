#include <RPC/rpc.h>
int check_xdr(void)
{
    XDR stream;
    char wire[64];char *text,*decoded,*array,*object;
    int value,values[2],pointed;
    unsigned int count,length;
    int *integers;
    xdrproc_t integer_filter,string_filter;
    integer_filter=xdr_int;string_filter=xdr_wrapstring;
    values[0]=1;values[1]=0x12345678;pointed=17;
    value=-123;text="abc";array=(char *)values;count=2;object=(char *)&pointed;
    xdrmem_create(&stream,wire,sizeof wire,XDR_ENCODE);
    if (!xdr_int(&stream,&value) || !xdr_string(&stream,&text,8) ||
        !xdr_array(&stream,&array,&count,8,sizeof(int),integer_filter) ||
        !xdr_pointer(&stream,&object,sizeof(int),integer_filter)) return 100;
    length=xdr_getpos(&stream);if(length!=32)return 101;
    if ((unsigned char)wire[0]!=255 || (unsigned char)wire[3]!=133 || wire[7]!=3 || wire[8]!='a' || wire[11]!=0 || wire[15]!=2 || wire[20]!=0x12 || wire[23]!=0x78 || wire[27]!=1 || wire[31]!=17) return 102;
    decoded=(char *)0;array=(char *)0;object=(char *)0;count=0;value=0;
    xdrmem_create(&stream,wire,length,XDR_DECODE);
    if (!xdr_int(&stream,&value) || !xdr_wrapstring(&stream,&decoded) ||
        !xdr_array(&stream,&array,&count,8,sizeof(int),integer_filter) ||
        !xdr_pointer(&stream,&object,sizeof(int),integer_filter)) return 103;
    integers=(int *)array;
    if(value!=-123 || strcmp(decoded,"abc") || count!=2 || integers[0]!=1 || integers[1]!=0x12345678 || *(int *)object!=17 || xdr_getpos(&stream)!=32) return 104;
    if(xdr_int(&stream,&value) || xdr_getpos(&stream)!=32)return 105;
    if(!xdr_setpos(&stream,0) || !xdr_int(&stream,&value) || value!=-123)return 106;
    if(xdr_setpos(&stream,33))return 107;
    xdr_free(string_filter,&decoded);if(decoded)return 108;
    xdrmem_create(&stream,(char *)0,0,XDR_FREE);
    if(!xdr_array(&stream,&array,&count,8,sizeof(int),integer_filter) || array || count)return 109;
    if(!xdr_pointer(&stream,&object,sizeof(int),integer_filter) || object)return 110;
    xdrmem_create(&stream,wire+4,7,XDR_DECODE);
    if(xdr_wrapstring(&stream,&decoded) || decoded || xdr_getpos(&stream)!=4)return 111;
    wire[0]=255;wire[1]=255;wire[2]=255;wire[3]=255;array=(char *)0;count=0;
    xdrmem_create(&stream,wire,4,XDR_DECODE);
    if(xdr_array(&stream,&array,&count,1024,sizeof(int),integer_filter) || array)return 112;
    return 0;
}
