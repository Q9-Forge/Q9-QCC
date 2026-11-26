/* x86 host/network byte order and BSD IPv4 text conversion. No OS service
 * is needed. The one-to-four-component inet_addr notation accepts decimal,
 * octal and hexadecimal numbers, and rejects overflow before accumulating. */
unsigned int htonl(unsigned int value)
{
    return ((value & 255U) << 24) | ((value & 65280U) << 8) |
           ((value >> 8) & 65280U) | ((value >> 24) & 255U);
}
unsigned short htons(unsigned short value)
{ return ((value & 255U) << 8) | ((value >> 8) & 255U); }
unsigned int ntohl(unsigned int value) { return htonl(value); }
unsigned short ntohs(unsigned short value) { return htons(value); }

unsigned int inet_addr(const char *text)
{
    unsigned int parts[4], value, limit;
    int count=0, base, digit, digits;
    if (!text) return 0xffffffffU;
    for (;;) {
        value=0; base=10; digits=0;
        if (*text=='0') {
            base=8; digits=1; ++text;
            if (*text=='x' || *text=='X') { base=16; digits=0; ++text; }
        }
        for (;;) {
            if (*text>='0' && *text<='9') digit=*text-'0';
            else if (*text>='a' && *text<='f') digit=*text-'a'+10;
            else if (*text>='A' && *text<='F') digit=*text-'A'+10;
            else break;
            if (digit>=base || value>(0xffffffffU-(unsigned int)digit)/(unsigned int)base) return 0xffffffffU;
            value=value*base+digit; ++digits; ++text;
        }
        if (!digits || count>=4) return 0xffffffffU;
        parts[count++]=value;
        if (*text!='.') break;
        if (value>255U || count>=4) return 0xffffffffU;
        ++text;
    }
    while (*text==' ' || *text=='\t' || *text==13 || *text==10) ++text;
    if (*text) return 0xffffffffU;
    limit=count==1 ? 0xffffffffU : (count==2 ? 0xffffffU : (count==3 ? 65535U : 255U));
    if (value>limit) return 0xffffffffU;
    if (count==2) value|=parts[0]<<24;
    else if (count==3) value|=(parts[0]<<24)|(parts[1]<<16);
    else if (count==4) value|=(parts[0]<<24)|(parts[1]<<16)|(parts[2]<<8);
    return htonl(value);
}
struct in_addr { unsigned int s_addr; };
extern int sprintf(char *,const char *,...);
char *inet_ntoa(struct in_addr address)
{
    static char text[16];
    unsigned int value=ntohl(address.s_addr);
    sprintf(text,"%u.%u.%u.%u",value>>24,(value>>16)&255U,(value>>8)&255U,value&255U);
    return text;
}
