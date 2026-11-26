/*
 * qrx86 -- initial QCC x86-32 assembler
 *
 * This first implementation deliberately supports a small, explicit source
 * subset and emits the private QCC ROF profile consumed by qlx86. It is not a
 * Microware r68/r386 compatible assembler yet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#define QR_LINE_MAX 1024
#define CODE_MAX 1048576
#define DATA_MAX 1048576
#define SYMBOL_MAX 8192
#define FIXUP_MAX 8192

typedef struct { char name[128]; int value; int section; int defined; int exported; } Symbol;
typedef struct { char name[128]; int site; int width; } Fixup;
typedef struct { int site; int type; int next; } ExtRef;
typedef struct { char name[128]; int count; int head; } External;
typedef struct { char name[128]; int site; } DataFixup;
typedef struct { char name[128]; int site; int relative; } CodeAddrFixup;
typedef struct { int site; int type; } LocalRef;
static unsigned char code[CODE_MAX], data[DATA_MAX];
static int codeN, dataN, bssN, section, lineNo, entry = -1, vsectMode;
static int psectTyLan, psectAttr, psectEdition, psectStack;
static int sawPsect, sawVsect;
static char entryName[128];
static int pendingVLabels[SYMBOL_MAX], pendingVLabelN;
static Symbol symbols[SYMBOL_MAX];
static Fixup fixups[FIXUP_MAX];
static ExtRef extRefs[FIXUP_MAX];
static External externals[FIXUP_MAX];
static DataFixup dataFixups[FIXUP_MAX];
static CodeAddrFixup codeAddrFixups[FIXUP_MAX];
static LocalRef localRefs[FIXUP_MAX];
static int symbolN, fixupN, extRefN, externalN, dataFixupN, codeAddrFixupN, localRefN;
static char psectName[128] = "QCC_X86";
static const char *reg32[8] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
static void fail(const char *message, const char *detail);
static void trim(char *s);
static void be16(FILE *f, unsigned int x);
static void be32(FILE *f, unsigned int x);

typedef struct { int kind, reg, base, index, scale, disp, imm, width; char symbol[128]; } Operand;
#define O_BAD 0
#define O_REG 1
#define O_IMM 2
#define O_MEM 3

static void putCode(unsigned int b);
static int regIndex(const char *s) {
    int i;
    for (i = 0; i < 8; ++i) if (strcmp(s, reg32[i]) == 0) return i;
    if (strcmp(s,"al")==0 || strcmp(s,"ax")==0) return 0;
    if (strcmp(s,"cl")==0 || strcmp(s,"cx")==0) return 1;
    if (strcmp(s,"dl")==0 || strcmp(s,"dx")==0) return 2;
    if (strcmp(s,"bl")==0 || strcmp(s,"bx")==0) return 3;
    if (strcmp(s,"ah")==0) return 4;
    return -1;
}
static int parseOperand(char *s, Operand *o) {
    char *p, *end;
    long v;
    memset(o, 0, sizeof(*o)); o->base = -1; o->index = -1; o->width = 4;
    if (strncmp(s, "byte ", 5) == 0) { o->width = 1; s += 5; }
    else if (strncmp(s, "word ", 5) == 0) { o->width = 2; s += 5; }
    else if (strncmp(s, "dword ", 6) == 0) s += 6;
    while (isspace((unsigned char)*s)) ++s;
    p = s + strlen(s); while (p > s && isspace((unsigned char)p[-1])) *--p = 0;
    o->reg = regIndex(s);
    if (o->reg >= 0) {
        o->kind = O_REG;
        if (strlen(s)==2 && s[1]=='l') o->width=1;
        else if (strlen(s)==2 && s[1]=='x') o->width=2;
        else if (strcmp(s,"ah")==0) { o->reg=4; o->width=1; }
        return 1;
    }
    if (*s == '[' && p > s && p[-1] == ']') {
        char expr[256], *plus, *tail, *star;
        size_t n = (size_t)(p - s - 2);
        int r;
        if (n >= sizeof(expr)) return 0;
        memcpy(expr, s + 1, n); expr[n] = 0;
        trim(expr);
        plus = strchr(expr, '+');
        if (plus != NULL) {
            *plus++ = 0;
            trim(expr); trim(plus);
        }
        r = regIndex(expr);
        if (r < 0) return 0;
        o->base = r;
        if (plus != NULL && *plus != 0) {
            star = strchr(plus, '*');
            if (star != NULL) {
                *star++ = 0; trim(plus); trim(star);
                r = regIndex(plus);
                if (r < 0) return 0;
                o->index = r;
                o->scale = atoi(star);
            } else if ((r = regIndex(plus)) >= 0) {
                o->index = r;
                o->scale = 1;
            } else {
                v = strtol(plus, &tail, 0);
                if (tail == plus || *tail != 0) {
                    size_t k;
                    for (k = 0; plus[k] && (isalnum((unsigned char)plus[k]) || plus[k] == '_' || plus[k] == '.'); ++k) { }
                    if (k == 0 || plus[k] != 0 || k >= sizeof(o->symbol)) return 0;
                    memcpy(o->symbol, plus, k + 1);
                } else o->disp = (int)v;
            }
        }
        if (o->base < 0 && o->index < 0 && !o->symbol[0]) return 0;
        if (o->index >= 0) {
            if (o->scale == 0) o->scale = 1;
            if (o->scale != 1 && o->scale != 2 && o->scale != 4 && o->scale != 8) return 0;
        }
        o->kind = O_MEM; return 1;
    }
    if (*s == '$') ++s;
    v = strtol(s, &end, 0);
    if (end != s && *end == 0) { o->kind = O_IMM; o->imm = (int)v; return 1; }
    return 0;
}
static void emitImm32(int x) { putCode(x); putCode(x >> 8); putCode(x >> 16); putCode(x >> 24); }
static void emitRM(int regField, Operand *rm) {
    int mod, base = rm->base, needDisp = rm->disp != 0;
    if (rm->kind == O_REG) { putCode(0xC0 | ((regField & 7) << 3) | rm->reg); return; }
    if (rm->kind != O_MEM) fail("expected register or memory operand", NULL);
    if (base < 0 && rm->index < 0) {
        int site;
        if(!rm->symbol[0]) fail("memory operand without base register",NULL);
        putCode(((regField&7)<<3)|5); site=codeN;
        if(codeAddrFixupN>=FIXUP_MAX) fail("too many address references",NULL);
        strncpy(codeAddrFixups[codeAddrFixupN].name,rm->symbol,sizeof(codeAddrFixups[codeAddrFixupN].name)-1);
        codeAddrFixups[codeAddrFixupN].site=site; codeAddrFixups[codeAddrFixupN].relative=0; ++codeAddrFixupN;
        emitImm32(0); return;
    }
    if(rm->symbol[0]) {
        int site;
        if(base!=2 || rm->index>=0 || rm->disp!=0 || strncmp(rm->symbol,"__offset_",9)!=0)
            fail("symbolic displacement is only supported for the QIRX86 EDX PIC form",rm->symbol);
        putCode((2<<6)|((regField&7)<<3)|base); site=codeN;
        if(codeAddrFixupN>=FIXUP_MAX) fail("too many address references",NULL);
        strncpy(codeAddrFixups[codeAddrFixupN].name,rm->symbol,sizeof(codeAddrFixups[codeAddrFixupN].name)-1);
        codeAddrFixups[codeAddrFixupN].site=site; codeAddrFixups[codeAddrFixupN].relative=1;
        ++codeAddrFixupN; emitImm32(7); return;
    }
    if(base<0) fail("memory index has no base register",NULL);
    if (rm->disp == 0 && base != 5) mod = 0;
    else if (rm->disp >= -128 && rm->disp <= 127) mod = 1;
    else mod = 2;
    if (rm->index >= 0 || base == 4) {
        putCode((mod << 6) | ((regField & 7) << 3) | 4);
        putCode(((rm->scale == 8 ? 3 : rm->scale == 4 ? 2 : rm->scale == 2 ? 1 : 0) << 6) |
                ((rm->index < 0 ? 4 : rm->index) << 3) | base);
    } else putCode((mod << 6) | ((regField & 7) << 3) | base);
    if (mod == 1) putCode(rm->disp);
    else if (mod == 2 || (mod == 0 && base == 5)) emitImm32(rm->disp);
    (void)needDisp;
}
static int conditionCode(const char *mn) {
    if(strcmp(mn,"je")==0||strcmp(mn,"jz")==0) return 4;
    if(strcmp(mn,"jne")==0||strcmp(mn,"jnz")==0) return 5;
    if(strcmp(mn,"jb")==0) return 2; if(strcmp(mn,"jae")==0) return 3;
    if(strcmp(mn,"jbe")==0) return 6; if(strcmp(mn,"ja")==0) return 7;
    if(strcmp(mn,"jl")==0) return 12; if(strcmp(mn,"jge")==0) return 13;
    if(strcmp(mn,"jle")==0) return 14; if(strcmp(mn,"jg")==0) return 15;
    return -1;
}

static void fail(const char *message, const char *detail) {
    fprintf(stderr, "qrx86:%d: %s%s%s\n", lineNo, message,
            detail ? ": " : "", detail ? detail : "");
    exit(1);
}
static int parseIntegerToken(char *s, const char *what) {
    char *end; long v;
    errno=0; v=strtol(s,&end,0);
    if(s==end || *end || errno==ERANGE || v<INT_MIN || v>INT_MAX) fail(what,s);
    return (int)v;
}
static void putCode(unsigned int b) {
    if (codeN >= CODE_MAX) fail("code section too large", NULL);
    code[codeN++] = (unsigned char)b;
}
static void putData(unsigned int b) {
    if (dataN >= DATA_MAX) fail("initialized data too large", NULL);
    data[dataN++] = (unsigned char)b;
}
static int symbolIndex(const char *name) {
    int i;
    for (i = 0; i < symbolN; ++i)
        if (strcmp(symbols[i].name, name) == 0) return i;
    return -1;
}
static int getSymbol(const char *name) {
    int i=symbolIndex(name);
    if(i>=0) return i;
    if(symbolN>=SYMBOL_MAX) fail("too many symbols",NULL);
    strncpy(symbols[symbolN].name,name,sizeof(symbols[symbolN].name)-1);
    symbols[symbolN].name[sizeof(symbols[symbolN].name)-1]=0;
    symbols[symbolN].value=0; symbols[symbolN].section=0;
    symbols[symbolN].defined=0; symbols[symbolN].exported=0;
    return symbolN++;
}
static void defineLabel(const char *name) {
    int i = getSymbol(name);
    if (section != 1 && section != 2) fail("label outside psect/vsect", name);
    if (symbols[i].defined) fail("duplicate label", name);
    symbols[i].value = section == 1 ? codeN : (section==2 && vsectMode==3 ? bssN : dataN);
    symbols[i].section = section == 2 && vsectMode==3 ? 3 : section;
    symbols[i].defined=1;
    if(section==2) pendingVLabels[pendingVLabelN++]=i;
}
static void settleVLabels(int isBss) {
    int i;
    for(i=0;i<pendingVLabelN;i++) {
        symbols[pendingVLabels[i]].section=isBss ? 3 : 2;
        symbols[pendingVLabels[i]].value=isBss ? bssN : dataN;
    }
    pendingVLabelN=0;
}
static void writeGlobals(FILE *f) {
    int i,n=0;
    for(i=0;i<symbolN;i++) if(symbols[i].exported) {
        if(!symbols[i].defined) fail("exported symbol has no definition",symbols[i].name);
        ++n;
    }
    be32(f,(unsigned int)n);
    for(i=0;i<symbolN;i++) if(symbols[i].exported) {
        unsigned int type=symbols[i].section==1?4:symbols[i].section==3?2:1;
        fwrite(symbols[i].name,1,strlen(symbols[i].name)+1,f);
        be16(f,type); be32(f,(unsigned int)symbols[i].value);
    }
}
static void addFixup(const char *name, int width) {
    if (fixupN >= FIXUP_MAX) fail("too many branch references", NULL);
    strncpy(fixups[fixupN].name, name, sizeof(fixups[fixupN].name) - 1);
    fixups[fixupN].site = codeN;
    fixups[fixupN].width = width;
    ++fixupN;
    if (width == 1) putCode(0);
    else { putCode(0); putCode(0); putCode(0); putCode(0); }
}
static void recordExternal(const char *name, int site, int type) {
    int e,r;
    for(e=0;e<externalN;e++) if(strcmp(externals[e].name,name)==0) break;
    if(e==externalN) {
        if(externalN>=FIXUP_MAX) fail("too many external symbols",NULL);
        strncpy(externals[e].name,name,sizeof(externals[e].name)-1);
        externals[e].name[sizeof(externals[e].name)-1]=0;
        externals[e].count=0; externals[e].head=-1; ++externalN;
    }
    if(extRefN>=FIXUP_MAX) fail("too many external references",NULL);
    r=extRefN++; extRefs[r].site=site; extRefs[r].type=type;
    extRefs[r].next=externals[e].head; externals[e].head=r; ++externals[e].count;
}
static void addDataFixup(const char *name) {
    if(dataFixupN>=FIXUP_MAX) fail("too many symbolic data values",NULL);
    strncpy(dataFixups[dataFixupN].name,name,sizeof(dataFixups[dataFixupN].name)-1);
    dataFixups[dataFixupN].site=dataN; ++dataFixupN;
    putData(0); putData(0); putData(0); putData(0);
}
static void resolveFixups(void) {
    int i;
    if (*entryName) {
        int s=symbolIndex(entryName);
        if(s<0) fail("unresolved entry label",entryName);
        if(symbols[s].section!=1) fail("entry label is not in code",entryName);
        entry=symbols[s].value;
    }
    for (i = 0; i < fixupN; ++i) {
        int s = symbolIndex(fixups[i].name);
        int d;
        if (s < 0) {
            recordExternal(fixups[i].name,fixups[i].site,0x00B8);
            continue;
        }
        if (symbols[s].section != 1) fail("branch target is outside code", fixups[i].name);
        d = symbols[s].value - (fixups[i].site + fixups[i].width);
        if (fixups[i].width == 1) {
            if (d < -128 || d > 127) fail("short branch out of range", fixups[i].name);
            code[fixups[i].site] = (unsigned char)d;
        } else {
            code[fixups[i].site] = (unsigned char)d;
            code[fixups[i].site + 1] = (unsigned char)(d >> 8);
            code[fixups[i].site + 2] = (unsigned char)(d >> 16);
            code[fixups[i].site + 3] = (unsigned char)(d >> 24);
        }
    }
    for(i=0;i<dataFixupN;i++) {
        int s=symbolIndex(dataFixups[i].name), site=dataFixups[i].site;
        if(s<0) { recordExternal(dataFixups[i].name,site,0x0018); continue; }
        if(!symbols[s].defined) fail("symbolic data target is undefined",dataFixups[i].name);
        data[site]=(unsigned char)symbols[s].value;
        data[site+1]=(unsigned char)(symbols[s].value>>8);
        data[site+2]=(unsigned char)(symbols[s].value>>16);
        data[site+3]=(unsigned char)(symbols[s].value>>24);
        if(localRefN>=FIXUP_MAX) fail("too many local references",NULL);
        localRefs[localRefN].site=site;
        localRefs[localRefN].type=symbols[s].section==1?0x001C:symbols[s].section==3?0x001A:0x0019;
        ++localRefN;
    }
    for(i=0;i<codeAddrFixupN;i++) {
        int s=symbolIndex(codeAddrFixups[i].name), site=codeAddrFixups[i].site;
        if (s < 0 && codeAddrFixups[i].relative && strncmp(codeAddrFixups[i].name, "__offset_", 9) == 0)
            s = symbolIndex(codeAddrFixups[i].name + 9);
        if(s<0) { recordExternal(codeAddrFixups[i].name,site,codeAddrFixups[i].relative?0x00B9:0x0038); continue; }
        if(!symbols[s].defined) fail("code address target is undefined",codeAddrFixups[i].name);
        {
            int value=symbols[s].value+(codeAddrFixups[i].relative?7:0);
            code[site]=(unsigned char)value; code[site+1]=(unsigned char)(value>>8);
            code[site+2]=(unsigned char)(value>>16); code[site+3]=(unsigned char)(value>>24);
        }
        if(localRefN>=FIXUP_MAX) fail("too many local references",NULL);
        localRefs[localRefN].site=site;
        localRefs[localRefN].type=(codeAddrFixups[i].relative?0x00B9:0x0039)+
            (symbols[s].section==1?3:symbols[s].section==3?1:0);
        ++localRefN;
    }
}
static void writeExternalRefs(FILE *f) {
    int e;
    be32(f,(unsigned int)externalN);
    for(e=0;e<externalN;e++) {
        int r;
        fwrite(externals[e].name,1,strlen(externals[e].name)+1,f);
        be32(f,(unsigned int)externals[e].count);
        for(r=externals[e].head;r>=0;r=extRefs[r].next) {
            be16(f,(unsigned int)extRefs[r].type); be32(f,(unsigned int)extRefs[r].site);
        }
    }
    be32(f,(unsigned int)localRefN);
    for(e=0;e<localRefN;e++) { be16(f,(unsigned int)localRefs[e].type); be32(f,(unsigned int)localRefs[e].site); }
    be32(f,0); /* common blocks */
}
static void be16(FILE *f, unsigned int x) { fputc((x >> 8) & 255, f); fputc(x & 255, f); }
static void be32(FILE *f, unsigned int x) {
    fputc((x >> 24) & 255, f); fputc((x >> 16) & 255, f);
    fputc((x >> 8) & 255, f); fputc(x & 255, f);
}
static void writeObject(const char *path) {
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) fail("cannot open output", path);
    be32(f, 0xDEADFACE); be16(f, (unsigned int)psectTyLan); be16(f, (unsigned int)psectAttr); be16(f, 0); be16(f, 999);
    for (i = 0; i < 6; ++i) fputc(0, f); /* timestamp not yet populated */
    be16(f, (unsigned int)psectEdition); be32(f, bssN); be32(f, dataN); be32(f, codeN);
    be32(f, (unsigned int)psectStack); be32(f, (unsigned int)entry); be32(f, 0xFFFFFFFF);
    be32(f, 0); be32(f, 0); be32(f, 0);
    fwrite(psectName, 1, strlen(psectName) + 1, f);
    writeGlobals(f);
    fwrite(code, 1, (size_t)codeN, f);
    fwrite(data, 1, (size_t)dataN, f);
    writeExternalRefs(f);
    if(ferror(f)) fail("write failed",path);
    if (fclose(f) != 0) fail("write failed", path);
}

static void trim(char *s) {
    char *p = s + strlen(s);
    while (p > s && isspace((unsigned char)p[-1])) --p;
    *p = 0;
    p = s;
    while (isspace((unsigned char)*p)) ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);
}
static void encodeInstruction(char *mn, char *args) {
    char *comma = NULL, *p;
    char left[256], right[256];
    Operand a, b;
    int haveTwo = 0;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    for (p = args; *p; ++p) {
        if (*p == '[') { while (*p && *p != ']') ++p; continue; }
        if (*p == ',') { comma = p; break; }
    }
    if (comma) {
        size_t n = (size_t)(comma - args);
        if (n >= sizeof(left)) fail("operand too long", NULL);
        memcpy(left, args, n); left[n] = 0; strncpy(right, comma + 1, sizeof(right) - 1);
        right[sizeof(right) - 1] = 0; trim(left); trim(right); haveTwo = 1;
        if (!parseOperand(left, &a)) fail("unsupported operand", left);
        if (!parseOperand(right, &b)) fail("unsupported operand", right);
    } else {
        strncpy(left, args, sizeof(left) - 1); left[sizeof(left) - 1] = 0; trim(left);
        if (*left && !parseOperand(left, &a)) {
            if (strcmp(mn,"jmp")==0 || strcmp(mn,"call")==0 || mn[0]=='j') {
                if (strcmp(left,"$ + 5")==0 && strcmp(mn,"call")==0) { putCode(0xE8); emitImm32(0); return; }
                if (left[0]=='$') {
                    char *q=strchr(left,'+'); long n;
                    if(!q || strcmp(mn,"jmp")==0 || strcmp(mn,"call")==0) fail("invalid PC-relative expression",left);
                    n=strtol(q+1,&p,0); if(p==q+1 || *p) fail("invalid PC-relative expression",left);
                    if(conditionCode(mn)<0) fail("unsupported branch",mn);
                    if(n< -126 || n>129) fail("short PC-relative branch out of range",left);
                    putCode(0x70+conditionCode(mn));
                    putCode((unsigned char)(n-2)); return;
                }
                if(strcmp(mn,"jmp")==0) putCode(0xE9);
                else if(strcmp(mn,"call")==0) putCode(0xE8);
                else { int cc=conditionCode(mn); if(cc<0) fail("unsupported branch",mn); putCode(0x0F); putCode(0x80+cc); }
                addFixup(left,4); return;
            }
            fail("unsupported operand", left);
        }
    }
    if (!haveTwo) {
        if (strcmp(mn,"push")==0) {
            if (a.kind==O_REG && a.width!=1) { if(a.width==2) putCode(0x66); putCode(0x50+a.reg); }
            else if (a.kind==O_IMM) {
                if(a.imm>=-128 && a.imm<=127) { putCode(0x6A); putCode(a.imm); }
                else { putCode(0x68); emitImm32(a.imm); }
            }
            else if (a.kind==O_MEM) { putCode(0xFF); emitRM(6,&a); }
            else fail("unsupported push operand", left);
        } else if (strcmp(mn,"pop")==0 && a.kind==O_REG && a.width!=1) { if(a.width==2) putCode(0x66); putCode(0x58+a.reg); }
        else if (strcmp(mn,"pop")==0 && a.kind==O_MEM) { putCode(0x8F); emitRM(0,&a); }
        else if ((strcmp(mn,"mul")==0 || strcmp(mn,"imul")==0 || strcmp(mn,"div")==0 || strcmp(mn,"idiv")==0 || strcmp(mn,"neg")==0 || strcmp(mn,"not")==0) && (a.kind==O_REG || a.kind==O_MEM)) {
            putCode(0xF7); emitRM(strcmp(mn,"mul")==0?4:strcmp(mn,"imul")==0?5:strcmp(mn,"div")==0?6:strcmp(mn,"idiv")==0?7:strcmp(mn,"neg")==0?3:2,&a);
        } else if ((strcmp(mn,"call")==0 || strcmp(mn,"jmp")==0) && (a.kind==O_REG || a.kind==O_MEM)) { putCode(0xFF); emitRM(strcmp(mn,"call")==0?2:4,&a); }
        else if ((strcmp(mn,"inc")==0 || strcmp(mn,"dec")==0) && a.kind==O_REG) { putCode(0x40+(mn[0]=='i' && mn[1]=='n'?a.reg:8+a.reg)); }
        else if ((strcmp(mn,"inc")==0 || strcmp(mn,"dec")==0) && a.kind==O_MEM) { putCode(0xFF); emitRM(mn[0]=='i'?0:1,&a); }
        else if (strcmp(mn,"cdq")==0) putCode(0x99);
		else if (strcmp(mn,"int")==0 && a.kind==O_IMM && a.imm >= 0 && a.imm <= 255) { putCode(0xCD); putCode(a.imm); }
        else if (strncmp(mn,"set",3)==0 && a.kind==O_REG) {
            int cc=strcmp(mn,"sete")==0||strcmp(mn,"setz")==0?4:strcmp(mn,"setne")==0||strcmp(mn,"setnz")==0?5:
              strcmp(mn,"setl")==0?12:strcmp(mn,"setle")==0?14:strcmp(mn,"setg")==0?15:strcmp(mn,"setge")==0?13:
              strcmp(mn,"setb")==0?2:strcmp(mn,"setbe")==0?6:strcmp(mn,"seta")==0?7:strcmp(mn,"setae")==0?3:-1;
            if(cc<0) fail("unsupported setcc condition",mn);
            putCode(0x0F); putCode(0x90+cc); emitRM(0,&a);
        }
        else if (strcmp(mn,"ret")==0 && !*left) putCode(0xC3);
        else if (strcmp(mn,"ret")==0 && a.kind==O_IMM && a.imm>=0 && a.imm<=65535) { putCode(0xC2); putCode(a.imm); putCode(a.imm>>8); }
        else if (strcmp(mn,"leave")==0) putCode(0xC9);
        else if (strcmp(mn,"nop")==0) putCode(0x90);
        else fail("unsupported instruction or operand", mn);
        return;
    }
    if (strcmp(mn,"mov")==0) {
        if (a.kind==O_REG && b.kind==O_IMM) {
            if (a.width==1) { putCode(0xB0+a.reg); putCode(b.imm); }
            else if (a.width==2) { putCode(0x66); putCode(0xB8+a.reg); putCode(b.imm); putCode(b.imm>>8); }
            else { putCode(0xB8+a.reg); emitImm32(b.imm); }
        }
        else if (a.kind==O_REG && (b.kind==O_REG || b.kind==O_MEM)) { if(a.width==2) putCode(0x66); putCode(a.width==1?0x8A:0x8B); emitRM(a.reg,&b); }
        else if ((a.kind==O_MEM) && b.kind==O_REG) { if(b.width==2) putCode(0x66); putCode(b.width==1?0x88:0x89); emitRM(b.reg,&a); }
        else if (a.kind==O_MEM && b.kind==O_IMM) {
            if(a.width==1) { putCode(0xC6); emitRM(0,&a); putCode(b.imm); }
            else if(a.width==2) { putCode(0x66); putCode(0xC7); emitRM(0,&a); putCode(b.imm); putCode(b.imm>>8); }
            else { putCode(0xC7); emitRM(0,&a); emitImm32(b.imm); }
        }
        else fail("unsupported mov form", args);
    } else if (strcmp(mn,"lea")==0 && a.kind==O_REG && b.kind==O_MEM) { putCode(0x8D); emitRM(a.reg,&b); }
    else if (strcmp(mn,"xchg")==0 && a.kind==O_REG && b.kind==O_REG) { putCode(0x87); emitRM(b.reg,&a); }
    else if (strcmp(mn,"movsx")==0 && a.kind==O_REG && (b.kind==O_MEM || b.kind==O_REG)) { putCode(0x0F); putCode(b.width==1?0xBE:0xBF); emitRM(a.reg,&b); }
    else if (strcmp(mn,"movzx")==0 && a.kind==O_REG && (b.kind==O_MEM || b.kind==O_REG)) { putCode(0x0F); putCode(b.width==1?0xB6:0xB7); emitRM(a.reg,&b); }
    else if (strncmp(mn,"set",3)==0 && a.kind==O_REG) {
        int cc=strcmp(mn,"sete")==0||strcmp(mn,"setz")==0?4:strcmp(mn,"setne")==0||strcmp(mn,"setnz")==0?5:
          strcmp(mn,"setl")==0?12:strcmp(mn,"setle")==0?14:strcmp(mn,"setg")==0?15:strcmp(mn,"setge")==0?13:
          strcmp(mn,"setb")==0?2:strcmp(mn,"setbe")==0?6:strcmp(mn,"seta")==0?7:strcmp(mn,"setae")==0?3:-1;
        if(cc<0) fail("unsupported setcc condition",mn);
        putCode(0x0F); putCode(0x90+cc); emitRM(0,&a);
    }
    else if ((strcmp(mn,"add")==0 || strcmp(mn,"sub")==0 || strcmp(mn,"and")==0 || strcmp(mn,"or")==0 || strcmp(mn,"xor")==0 || strcmp(mn,"cmp")==0 || strcmp(mn,"test")==0) && a.kind==O_REG && b.kind==O_REG) {
        if (strcmp(mn,"add")==0) putCode(0x01); else if (strcmp(mn,"or")==0) putCode(0x09);
        else if (strcmp(mn,"and")==0) putCode(0x21); else if (strcmp(mn,"sub")==0) putCode(0x29);
        else if (strcmp(mn,"xor")==0) putCode(0x31); else if (strcmp(mn,"cmp")==0) putCode(0x39); else putCode(0x85);
        emitRM(b.reg,&a);
    } else if ((strcmp(mn,"add")==0 || strcmp(mn,"sub")==0 || strcmp(mn,"and")==0 || strcmp(mn,"or")==0 || strcmp(mn,"xor")==0 || strcmp(mn,"cmp")==0) && a.kind==O_REG && b.kind==O_IMM) {
        int ext=strcmp(mn,"add")==0?0:strcmp(mn,"or")==0?1:strcmp(mn,"and")==0?4:strcmp(mn,"sub")==0?5:strcmp(mn,"xor")==0?6:7;
        putCode(0x81); emitRM(ext,&a); emitImm32(b.imm);
    } else if (strcmp(mn,"imul")==0 && a.kind==O_REG && (b.kind==O_REG || b.kind==O_MEM)) { putCode(0x0F); putCode(0xAF); emitRM(a.reg,&b); }
    else if ((strcmp(mn,"shl")==0 || strcmp(mn,"shr")==0 || strcmp(mn,"sar")==0) && a.kind==O_REG && (strcmp(right,"cl")==0 || b.kind==O_IMM)) {
        int ext=strcmp(mn,"shl")==0?4:strcmp(mn,"shr")==0?5:7;
        if (strcmp(right,"cl")==0) { putCode(0xD3); emitRM(ext,&a); }
        else { putCode(0xC1); emitRM(ext,&a); putCode(b.imm); }
    } else fail("unsupported instruction form", mn);
}
static void parseLine(char *line) {
    char *comment = strchr(line, ';');
    char *arg, *end;
    if (comment) *comment = 0;
    trim(line);
    if (!*line) return;
    end = strchr(line, ':');
    if (end && (strchr(line, ' ') == NULL || strchr(line, ' ') > end)) {
        *end = 0; trim(line); defineLabel(line); return;
    }
    arg = line;
    while (*arg && !isspace((unsigned char)*arg)) ++arg;
    if (*arg) *arg++ = 0;
    trim(line); trim(arg);
    if (strcmp(line, "psect") == 0) {
        char *part=strtok(arg, ","); int field=0;
        if(sawPsect) fail("multiple PSECTs are not supported yet",NULL);
        sawPsect=1;
        while(part) {
            trim(part);
            if(field==0 && *part) { strncpy(psectName,part,sizeof(psectName)-1); psectName[sizeof(psectName)-1]=0; }
            if(field>0) {
                int v=parseIntegerToken(part,"invalid PSECT parameter");
                if(v<0 && field!=5) fail("invalid PSECT parameter",part);
                if(field==1) psectTyLan=(int)v; else if(field==2) psectAttr=(int)v;
                else if(field==3) psectEdition=(int)v; else if(field==4) psectStack=(int)v;
                else if(field==5) entry=(int)v;
            }
            ++field; part=strtok(NULL,",");
        }
        section = 1;
    } else if (strcmp(line, "nam") == 0) { /* module comment/name only */ }
    else if (strcmp(line, "entry") == 0) { strncpy(entryName,arg,sizeof(entryName)-1); entryName[sizeof(entryName)-1]=0; }
    else if (strcmp(line,"global")==0 || strcmp(line,"globl")==0 || strcmp(line,"export")==0) {
        char *name=strtok(arg,",");
        while(name) { int i; trim(name); i=getSymbol(name); symbols[i].exported=1; name=strtok(NULL,","); }
    }
    else if (strcmp(line, "vsect") == 0) { if(!sawPsect || sawVsect) fail("vsect requires one preceding PSECT",NULL); sawVsect=1; section = 2; }
    else if (strcmp(line, "ends") == 0) { settleVLabels(vsectMode==3); section = 0; }
    else if (strcmp(line, "end") == 0) { /* source terminator */ }
    else if (strcmp(line, "ds.l") == 0 && section == 2) { int n = parseIntegerToken(arg,"invalid ds.l size"); settleVLabels(1); if (n < 0 || n > DATA_MAX/4-bssN/4) fail("invalid ds.l size", arg); bssN += n*4; vsectMode=3; }
    else if (strcmp(line, "ds.b") == 0 && section == 2) { int n = parseIntegerToken(arg,"invalid ds.b size"); settleVLabels(1); if (n < 0 || n > DATA_MAX-bssN) fail("invalid ds.b size", arg); bssN += n; vsectMode=3; }
    else if (strcmp(line, "ds.w") == 0 && section == 2) { int n = parseIntegerToken(arg,"invalid ds.w size"); settleVLabels(1); if (n < 0 || n > DATA_MAX/2-bssN/2) fail("invalid ds.w size", arg); bssN += n*2; vsectMode=3; }
    else if (strcmp(line, "align") == 0 && (section == 1 || section == 2)) {
        int n=parseIntegerToken(arg,"invalid alignment"), size=section==1?codeN:dataN;
        if(n<1 || (n & (n-1)) != 0 || n>4096) fail("align expects power of two <= 4096",arg);
        while(size % n) { if(section==1) putCode(0x90); else putData(0); ++size; }
    }
    else if (strcmp(line, "even") == 0 && (section == 1 || section == 2)) {
        if ((section==1 ? codeN : dataN) & 1) { if(section==1) putCode(0x90); else putData(0); }
    }
    else if (strncmp(line,"set",3)==0 || strcmp(line,"nop")==0 || strcmp(line,"ret")==0 || strcmp(line,"leave")==0 || strcmp(line,"push")==0 || strcmp(line,"pop")==0 || strcmp(line,"call")==0 || strcmp(line,"mov")==0 || strcmp(line,"movsx")==0 || strcmp(line,"lea")==0 || strcmp(line,"movzx")==0 || strcmp(line,"add")==0 || strcmp(line,"sub")==0 || strcmp(line,"and")==0 || strcmp(line,"or")==0 || strcmp(line,"xor")==0 || strcmp(line,"cmp")==0 || strcmp(line,"test")==0 || strcmp(line,"xchg")==0 || strcmp(line,"inc")==0 || strcmp(line,"dec")==0 || strcmp(line,"imul")==0 || strcmp(line,"mul")==0 || strcmp(line,"div")==0 || strcmp(line,"idiv")==0 || strcmp(line,"neg")==0 || strcmp(line,"not")==0 || strcmp(line,"cdq")==0 || strcmp(line,"int")==0 || strcmp(line,"shl")==0 || strcmp(line,"shr")==0 || strcmp(line,"sar")==0 || strncmp(line,"j",1)==0) {
        if (section != 1) fail("instruction outside psect", line);
        encodeInstruction(line,arg);
    }
    else if ((strcmp(line, "dc.l") == 0 || strcmp(line,"dc.w")==0 || strcmp(line,"dc.b")==0) && section == 2) {
        settleVLabels(0);
        vsectMode=2;
        char *part = strtok(arg, ",");
        int width=strcmp(line,"dc.l")==0?4:strcmp(line,"dc.w")==0?2:1;
        while (part) {
            char *tail; long value;
            trim(part); errno=0; value = strtol(part, &tail, 0);
            if (*part == 0) fail("empty dc value",NULL);
            if(errno==ERANGE) fail("dc value out of range",part);
            if(*tail) {
                if(width!=4) fail("symbolic dc.b/dc.w is not supported",part);
                addDataFixup(part);
            } else {
                putData((unsigned long)value); if(width>1) putData((unsigned long)value >> 8);
                if(width>2) { putData((unsigned long)value >> 16); putData((unsigned long)value >> 24); }
            }
            part = strtok(NULL, ",");
        }
    } else if (*line == '.') {
        fail("unsupported directive", line);
    } else fail("unsupported instruction or syntax", line);
}
int main(int argc, char **argv) {
    FILE *in;
    char line[QR_LINE_MAX];
    if (argc != 3) { fprintf(stderr, "usage: qrx86 input.sx86 output.r\n"); return 2; }
    in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }
    while (fgets(line, sizeof(line), in)) {
        size_t n; ++lineNo; n=strlen(line);
        if(n==sizeof(line)-1 && line[n-1]!='\n' && !feof(in)) fail("source line too long",NULL);
        parseLine(line);
    }
    if(ferror(in)) fail("input read failed",argv[1]);
    fclose(in);
    resolveFixups();
    if (codeN == 0) fail("empty code section", NULL);
    if(entry<0 || entry>=codeN) fail("entry point outside code",NULL);
    writeObject(argv[2]);
    return 0;
}
