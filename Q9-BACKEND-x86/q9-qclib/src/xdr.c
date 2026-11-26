/* Wire specification: https://www.rfc-editor.org/rfc/rfc4506.html
 * Sections 4.1/4.2, 4.4, 4.9/4.11, 4.13 and 4.19.
 * Memory XDR for Q9's RPC runtime. Integers are always network byte order;
 * opaque values are padded to a multiple of four bytes. This implementation
 * owns its memory-stream convention (x_ops == NULL), not Microware x_ops ABI. */
#include <RPC/rpc.h>
#include <stdlib.h>
#include <string.h>
void xdrmem_create(XDR *stream,char *buffer,unsigned int size,int operation)
{
    stream->x_op=operation;stream->x_ops=(struct xdr_ops *)0;
    stream->x_public=(char *)0;stream->x_private=buffer;stream->x_base=buffer;
    stream->x_handy=size<=2147483647U ? size : -1;
    if (operation<0 || operation>2 || (!buffer && size)) stream->x_handy=-1;
}
unsigned int xdr_getpos(XDR *stream)
{ return stream->x_private-stream->x_base; }
int xdr_setpos(XDR *stream,unsigned int position)
{
    unsigned int capacity;
    if (stream->x_ops || stream->x_handy<0 || stream->x_op==2) return 0;
    capacity=xdr_getpos(stream)+stream->x_handy;
    if (position>capacity) return 0;
    stream->x_private=stream->x_base+position;stream->x_handy=capacity-position;
    return 1;
}
static int xdr_word(XDR *stream,unsigned int *value)
{
    unsigned char *p;
    if (!stream || !value || stream->x_ops) return 0;
    if (stream->x_op==2) return 1;
    if (stream->x_handy<4 || (stream->x_op!=0 && stream->x_op!=1)) return 0;
    p=(unsigned char *)stream->x_private;
    if (stream->x_op==0) {
        p[0]=*value>>24;p[1]=*value>>16;p[2]=*value>>8;p[3]=*value;
    } else *value=((unsigned int)p[0]<<24)|((unsigned int)p[1]<<16)|((unsigned int)p[2]<<8)|p[3];
    stream->x_private+=4;stream->x_handy-=4;return 1;
}
int xdr_int(XDR *stream,int *value) { return xdr_word(stream,(unsigned int *)value); }
int xdr_u_int(XDR *stream,unsigned int *value) { return xdr_word(stream,value); }
int xdr_opaque(XDR *stream,char *buffer,unsigned int count)
{
    unsigned int padded,i;
    if (!stream || stream->x_ops) return 0;
    if (stream->x_op==2) return 1;
    if (count>2147483644U || (!buffer && count)) return 0;
    padded=(count+3U)&0xfffffffcU;
    if (stream->x_handy<0 || padded>(unsigned int)stream->x_handy) return 0;
    if (stream->x_op==0) {
        for(i=0;i<count;++i) stream->x_private[i]=buffer[i];
        while(i<padded) stream->x_private[i++]=0;
    } else if (stream->x_op==1) {
        for(i=0;i<count;++i) buffer[i]=stream->x_private[i];
    } else return 0;
    stream->x_private+=padded;stream->x_handy-=padded;return 1;
}
int xdr_string(XDR *stream,char **text,unsigned int maximum)
{
    unsigned int count;int allocated=0;char *destination;
    if (!stream || !text || stream->x_ops) return 0;
    if (stream->x_op==2) { free(*text);*text=(char *)0;return 1; }
    if (stream->x_op==0) { if (!*text) return 0;count=strlen(*text); }
    else count=0;
    if (!xdr_word(stream,&count) || count>maximum || count>2147483646U) return 0;
    if (stream->x_op==1 && !*text) {
        *text=malloc(count+1);if (!*text) return 0;allocated=1;
    }
    if (!xdr_opaque(stream,*text,count)) {
        if (allocated) { free(*text);*text=(char *)0; }
        return 0;
    }
    if (stream->x_op==1) { destination=*text;destination[count]=0; }
    return 1;
}
int xdr_wrapstring(XDR *stream,char **text)
{ return xdr_string(stream,text,0xffffffffU); }
int xdr_array(XDR *stream,char **array,unsigned int *count,unsigned int maximum,
              unsigned int element_size,xdrproc_t filter)
{
    unsigned int i,total;
    if (!stream || !array || !count || !filter || !element_size || stream->x_ops) return 0;
    if (!xdr_word(stream,count) || *count>maximum || *count>2147483647U/element_size) return 0;
    total=*count*element_size;
    if (stream->x_op==1 && !*array && total) {
        *array=calloc(*count,element_size);if (!*array) return 0;
    }
    if (*count && !*array) return 0;
    for (i=0;i<*count;++i) if (!filter(stream,*array+i*element_size)) return 0;
    if (stream->x_op==2) { free(*array);*array=(char *)0;*count=0; }
    return 1;
}
int xdr_pointer(XDR *stream,char **object,unsigned int size,xdrproc_t filter)
{
    unsigned int present;
    if (!stream || !object || !filter || !size || size>2147483647U || stream->x_ops) return 0;
    present=*object!=0;
    if (stream->x_op!=2 && !xdr_word(stream,&present)) return 0;
    if (present>1) return 0;
    if (!present) { *object=(char *)0;return 1; }
    if (stream->x_op==1 && !*object) { *object=calloc(1,size);if (!*object) return 0; }
    if (!filter(stream,*object)) return 0;
    if (stream->x_op==2) { free(*object);*object=(char *)0; }
    return 1;
}
int xdr_void(void) { return 1; }
void xdr_free(xdrproc_t filter,void *object)
{
    XDR stream;
    if (!filter) return;
    xdrmem_create(&stream,(char *)0,0,2);filter(&stream,object);
}
