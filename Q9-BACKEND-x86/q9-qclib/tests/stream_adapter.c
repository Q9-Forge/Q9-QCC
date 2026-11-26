extern int *fopen(const char *,const char *);
extern int fclose(int *), fgetc(int *), fputs(const char *,int *), ferror(int *), feof(int *), ungetc(int,int *);
extern char *fgets(char *,int,int *);
extern int fread(char *,unsigned,unsigned,int *);
extern void clearerr(int *);
static char input[]="ab\rcd";
static int pos, writes, fail_read;
int q9_x86_path_open(const char *n,const char *m) { (void)n;(void)m; pos=0; return 0; }
int q9_x86_path_close(int p) { return p == 0 ? 0 : -1; }
int q9_x86_path_read(int p,char *b,int n) { (void)p; if(fail_read)return -1; if(!input[pos])return 0; if(n>2)n=2; {int i=0;while(i<n && input[pos])b[i++]=input[pos++];return i;} }
int q9_x86_path_write(int p,const char *b,int n) { (void)p;(void)b; if(n>2)n=2;writes+=n;return n; }
int q9_x86_path_seek(int p,int offset,int origin) { (void)p; if(origin==1)offset+=pos;else if(origin==2)offset+=5;if(offset<0||offset>5)return -1;pos=offset;return pos; }
extern int fseek(int *,long,int);
extern long ftell(int *);
int main(void) {
 int *f=fopen("x","r"); char b[8];
 if(!f || fgetc(f)!='a')return 1;
 if(ungetc('X',f)!='X' || ungetc('Y',f)!=-1)return 2;
 if(!fgets(b,8,f) || b[0]!='X'||b[1]!='b'||b[2]!=13||b[3])return 3;
 if(fread(b,1,8,f)!=2 || !feof(f)||ferror(f))return 4;
 clearerr(f);if(feof(f)||ferror(f))return 5;
 fail_read=1;if(fgetc(f)!=-1||!ferror(f))return 6;
 if(fputs("12345",f)!=0||writes!=5)return 7;
 fail_read=0;clearerr(f);if(fseek(f,1,0)||fgetc(f)!='b'||ftell(f)!=2)return 9;
 if(ungetc('X',f)!='X'||ftell(f)!=1||fseek(f,0,1)||fgetc(f)!='b')return 10;
 if(fseek(f,-1,2)||fgetc(f)!='d')return 11;
 if(fclose(f)!=0||fclose(f)!=-1)return 8;
 return 0;
}
