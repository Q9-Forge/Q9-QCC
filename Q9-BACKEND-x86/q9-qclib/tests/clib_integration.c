#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <SPF/BSD/netinet/in.h>
#include <SPF/BSD/netdb.h>
#include "xdr_memory.c"
char captured[256];
int written;
int q9_x86_path_open(const char *n,const char *m){return 3;}
int q9_x86_path_close(int p){return 0;}
int q9_x86_path_read(int p,char *b,int n){return 0;}
int q9_x86_path_write(int p,const char *b,int n){int i;for(i=0;i<n;++i)captured[written++]=b[i];return n;}
static int compare_ints(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
int q9_x86_path_seek(int path, int offset, int origin){return 0;}
int main(void){
 time_t stamp; struct tm *calendar; struct tm date;
 char b[128]; int n; char *p, *q, *t; int values[5]; int key; int *found; struct in_addr address;
 n=sprintf(b,"%02d %s %u",7,"abc",42);
 if(n!=9)return 1;
 if(strcmp(b,"07 abc 42"))return 2;
 if(sprintf(b,"%d",-123)!=4)return 3;
 if(strcmp(b,"-123"))return 4;
 if(fputs("hello",stdout)<0)return 5;
 if(written!=5)return 6;
 if(strncmp(captured,"hello",5))return 7;
 if(sprintf(b,"%+08d",-123)!=8||strcmp(b,"-0000123"))return 20;
 if(sprintf(b,"% 5d",12)!=5||strcmp(b,"   12"))return 21;
 if(sprintf(b,"%#08x",42)!=8||strcmp(b,"0x00002a"))return 22;
 if(sprintf(b,"%#.0o",0)!=1||strcmp(b,"0"))return 23;
 if(sprintf(b,"%.0d",0)!=0||strcmp(b,""))return 24;
 if(sprintf(b,"%08.5d",42)!=8||strcmp(b,"   00042"))return 25;
 if(sprintf(b,"%*.*s",-6,3,"abcdef")!=6||strcmp(b,"abc   "))return 26;
 if(sprintf(b,"%05s","ab")!=5||strcmp(b,"   ab"))return 27;
 if(sprintf(b,"%hd %hu",65535,65537)!=4||strcmp(b,"-1 1"))return 28;
 if(sprintf(b,"%d",(int)0x80000000U)!=11||strcmp(b,"-2147483648"))return 29;
 p=malloc(100);q=malloc(100);if(!p||!q)return 8;
 strcpy(p,"kept");free(q);q=malloc(60000);if(!q)return 9;
 t=realloc(p,1000);if(!t||strcmp(t,"kept"))return 10;
 free(t);free(q);p=malloc(65528);if(!p)return 11;free(p);
 p=calloc(8,4);if(!p)return 12;for(n=0;n<32;++n)if(p[n])return 13;free(p);
 if(calloc(0x80000000U,8))return 14;
 values[0]=7;values[1]=2;values[2]=9;values[3]=-3;values[4]=2;
 qsort(values,5,sizeof(int),compare_ints);
 if(values[0]!=-3||values[1]!=2||values[2]!=2||values[3]!=7||values[4]!=9)return 15;
 key=7;found=bsearch(&key,values,5,sizeof(int),compare_ints);if(!found||*found!=7)return 16;
 if(htonl(0x12345678U)!=0x78563412U||htons(0x1234)!=0x3412)return 30;
 if(ntohl(htonl(0xabcdef01U))!=0xabcdef01U)return 31;
 if(inet_addr("127.1")!=inet_addr("127.0.0.1"))return 32;
 if(inet_addr("0x7f000001")!=inet_addr("127.0.0.1"))return 33;
 if(inet_addr("256.1.1.1")!=0xffffffffU||inet_addr("4294967296")!=0xffffffffU)return 34;
 address.s_addr=inet_addr("192.168.2.3");if(strcmp(inet_ntoa(address),"192.168.2.3"))return 35;
 stamp=0;calendar=gmtime(&stamp);
 if(!calendar || calendar->tm_year!=70 || calendar->tm_mon!=0 || calendar->tm_mday!=1 || calendar->tm_wday!=4)return 36;
 stamp=-1;calendar=gmtime(&stamp);
 if(calendar->tm_year!=69 || calendar->tm_mon!=11 || calendar->tm_mday!=31 || calendar->tm_sec!=59)return 37;
 stamp=951782400L;calendar=gmtime(&stamp);
 if(calendar->tm_year!=100 || calendar->tm_mon!=1 || calendar->tm_mday!=29 || calendar->tm_yday!=59)return 38;
 if(mktime(calendar)!=stamp)return 39;
 stamp=(long)0x80000000U;calendar=gmtime(&stamp);if(mktime(calendar)!=stamp)return 40;
 stamp=2147483647L;calendar=gmtime(&stamp);if(mktime(calendar)!=stamp)return 41;
 calendar->tm_sec+=1;if(mktime(calendar)!=-1)return 42;
 date.tm_year=101;date.tm_mon=1;date.tm_mday=29;date.tm_hour=0;date.tm_min=0;date.tm_sec=0;
 if(mktime(&date)!=983404800L || date.tm_mon!=2 || date.tm_mday!=1)return 43;
 n=check_xdr();if(n)return n;
 return 0;
}
