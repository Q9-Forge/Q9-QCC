extern int declared_pair(int,int);
extern int declared_pair(int,int);
int call_before_definition(void) { return declared_pair(8,9); }
int declared_pair(int a,int b) { return a*10+b; }
extern int external_pair(int, int);
int pair(int a,int b){return a*10+b;}
int *pass(int *p){return p;}
static int static_pair(int a, int b){return a*100+b;}
struct inner { int padding; int value; };
struct outer { int padding; struct inner nested; };
unsigned char global_byte;
unsigned short global_half;
int main(void){
 int a[4]; int *p; unsigned short h; unsigned char c;
 int (*old_callback)(); int (*callback)(int,int); int *(*pointer_callback)(int *);
 struct outer object; struct outer *object_pointer; struct outer records[2]; struct outer *record_pointers[2];
 a[0]=7;a[1]=11;a[2]=13;a[3]=17;p=a;
 if(pair(2,3)!=23)return 1;
 if(call_before_definition()!=89 || declared_pair(7,8)!=78)return 20;
 if(external_pair(4,5)!=45)return 2;
 if(*(p+2)!=13)return 3;
 if(p[2]!=13)return 8;
 if(*(2+p)!=13)return 4;
 if((p+3)-p!=3)return 5;
 if(pass(p)!=p)return 6;
 h=65535;if(h!=65535)return 7;
 old_callback=pair;if(old_callback(4,6)!=46)return 18;
 callback=pair;if(callback(6,7)!=67)return 9;
 callback=static_pair;if(callback(3,5)!=305)return 10;
 callback=external_pair;if(callback(7,8)!=78)return 11;
 pointer_callback=pass;if(pointer_callback(p)!=p)return 12;
 object_pointer=&object;p=&object.nested.value;*p=1234;
 if(object.nested.value!=1234)return 13;
 if(&object_pointer->nested.value!=p)return 14;
 c=511;if(c!=255)return 15;
 global_byte=513;if(global_byte!=1)return 16;
 global_half=65539;if(global_half!=3)return 17;
 object_pointer=&records[1];object_pointer->nested.value=987;
 record_pointers[0]=&object;record_pointers[1]=&records[1];
 object_pointer=record_pointers[1];
 if(object_pointer->nested.value!=987)return 19;
 return 0;
}
