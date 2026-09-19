/*
 * qlv32 -- Q9 ROF linker for OS-9000/RISC-V-32
 *
 * Usage: qlv32 [options] <input.r> -O=<module>
 *
 * Reads ROF objects (from qov32 assembler) and produces OS-9000/RISC-V-32 modules.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================ Constants */
#define QL_IN       33554432        /* Input buffer */
#define QL_OUT      33554432        /* Output buffer */
#define QL_ROF      256             /* Max ROF objects */
#define QL_SYM      32768           /* Max symbols */
#define QL_POOL     1048576         /* Symbol name pool */

static char inBuf[QL_IN];
static int inLen;
static char outBuf[QL_OUT];
static int outLen;

/* ================================================================ Helpers */
static void fatal(const char *msg) {
    printf("ERROR: %s\n", msg);
    exit(1);
}

static int strLen(const char *s) {
    int n = 0;
    while (s[n] != 0) n++;
    return n;
}

static int strEq(const char *a, const char *b) {
    int i = 0;
    while (a[i] != 0 && a[i] == b[i]) i++;
    return a[i] == b[i];
}

/* ================================================================ I/O */
static int be16(int at) {
    if (at + 1 >= inLen) fatal("be16: buffer underrun");
    return ((inBuf[at] & 255) << 8) | (inBuf[at + 1] & 255);
}

static int be32(int at) {
    if (at + 3 >= inLen) fatal("be32: buffer underrun");
    return ((inBuf[at] & 255) << 24) | ((inBuf[at + 1] & 255) << 16) |
           ((inBuf[at + 2] & 255) << 8) | (inBuf[at + 3] & 255);
}

static int le32(int at) {
    if (at + 3 >= inLen) fatal("le32: buffer underrun");
    return (inBuf[at] & 255) | ((inBuf[at + 1] & 255) << 8) |
           ((inBuf[at + 2] & 255) << 16) | ((inBuf[at + 3] & 255) << 24);
}

static int skipName(int at) {
    while (at < inLen && inBuf[at] != 0) at++;
    return at + 1;
}

/* ================================================================ File I/O */
static int readFile(const char *path) {
    FILE *fp = fopen(path, "rb");
    int n;
    
    if (!fp) {
        printf("File not found: %s\n", path);
        return -1;
    }
    n = fread(inBuf, 1, QL_IN, fp);
    fclose(fp);
    return n;
}

static int writeFile(const char *path, int len) {
    FILE *fp = fopen(path, "wb");
    int n;
    
    if (!fp) {
        printf("Cannot open file: %s\n", path);
        return -1;
    }
    n = fwrite(outBuf, 1, len, fp);
    fclose(fp);
    return n;
}

/* ================================================================ ROF Parser */
typedef struct {
    int tylan;          /* Type/Language */
    int attrev;         /* Attributes/Revision */
    int edition;        /* Edition */
    int stat;           /* Uninitialized static data */
    int idat;           /* Initialized data */
    int cod;            /* Code size */
    int stk;            /* Stack size */
    int entry;          /* Entry point */
    int trap;           /* Trap entry */
    int rem;            /* Remote data */
    
    int nameAt;         /* Offset: PSECT name */
    int codeAt;         /* Offset: code section */
    int idatAt;         /* Offset: initialized data */
    int globAt;         /* Offset: globals list */
    int globN;          /* Global symbol count */
    int extAt;          /* Offset: external refs */
    int extN;           /* External symbol count */
    int localAt;        /* Offset: local refs */
    int localN;         /* Local symbol count */
} RofInfo;

static RofInfo rof[QL_ROF];
static int rofN = 0;
static int rofRoot = -1;

static void rofParse(int at0) {
    RofInfo *r;
    int i, at;
    
    if (rofN >= QL_ROF) fatal("too many ROFs");
    
    r = &rof[rofN];
    rofN++;
    
    if (at0 + 56 > inLen) fatal("ROF too short for header");
    
    /* Check DEADFACE sync */
    if ((inBuf[at0] & 255) != 0xDE || (inBuf[at0 + 1] & 255) != 0xAD ||
        (inBuf[at0 + 2] & 255) != 0xFA || (inBuf[at0 + 3] & 255) != 0xCE)
        fatal("No ROF: sync is not DEADFACE");
    
    r->tylan = be16(at0 + 4);
    r->attrev = be16(at0 + 6);
    if (be16(at0 + 8) != 0) fatal("ROF marked with error");
    r->edition = be16(at0 + 18);
    r->stat = be32(at0 + 20);
    r->idat = be32(at0 + 24);
    r->cod = be32(at0 + 28);
    r->stk = be32(at0 + 32);
    r->entry = be32(at0 + 36);
    r->trap = be32(at0 + 40);
    r->rem = be32(at0 + 44);
    
    if (be32(at0 + 48) != 0) fatal("Remote initialized data not supported");
    if (be32(at0 + 52) != 0) fatal("Debug info not supported");
    
    /* Parse PSECT name */
    at = at0 + 56;
    r->nameAt = at;
    at = skipName(at);
    
    /* Parse globals */
    r->globN = be32(at);
    r->globAt = at + 4;
    at = r->globAt;
    for (i = 0; i < r->globN; i++) {
        at = skipName(at);
        at = at + 6;
    }
    
    /* Sections */
    r->codeAt = at;
    at = at + r->cod;
    r->idatAt = at;
    at = at + r->idat;
    
    /* External references */
    r->extN = be32(at);
    r->extAt = at + 4;
    at = r->extAt;
    for (i = 0; i < r->extN; i++) {
        at = skipName(at);
        int refcount = be32(at);
        at = at + 4 + refcount * 6;
    }
    
    /* Local references */
    r->localN = be32(at);
    r->localAt = at + 4;
    
    if (rofRoot == -1) rofRoot = rofN - 1;
    if (r->tylan != 0 && rofRoot == 0) rofRoot = rofN - 1;
    
    printf("[ROF %d] PSECT: %s  Type/Lang: 0x%04X  Code: %d  Init: %d  Stat: %d\n",
           rofN - 1, &inBuf[r->nameAt], r->tylan, r->cod, r->idat, r->stat);
}

/* ================================================================ Symbol Table */
typedef struct {
    int name;           /* Offset in symPool */
    int value;          /* Address/value */
    int type;           /* 0=reserved, 1=data, 4=code */
    int rofIdx;         /* Which ROF defined this */
} Symbol;

static Symbol symTable[QL_SYM];
static int symN = 0;
static char symPool[QL_POOL];
static int symPoolTop = 0;

static void symAdd(const char *name, int value, int type, int rofIdx) {
    Symbol *s;
    int len, i;
    
    if (symN >= QL_SYM) fatal("too many symbols");
    
    len = strLen(name);
    if (symPoolTop + len + 1 >= QL_POOL) fatal("symbol pool full");
    
    s = &symTable[symN];
    s->name = symPoolTop;
    s->value = value;
    s->type = type;
    s->rofIdx = rofIdx;
    
    for (i = 0; i <= len; i++)
        symPool[symPoolTop + i] = name[i];
    symPoolTop = symPoolTop + len + 1;
    symN++;
}

static int symFind(const char *name) {
    int i;
    for (i = symN - 1; i >= 0; i--) {
        if (strEq(&symPool[symTable[i].name], name))
            return i;
    }
    return -1;
}

static const char *symGetName(int idx) {
    if (idx < 0 || idx >= symN) return "???";
    return &symPool[symTable[idx].name];
}

/* ================================================================ Layout */
typedef struct {
    int codeBase;
    int codeSize;
    int idatBase;
    int idatSize;
    int statSize;
} LayoutInfo;

static LayoutInfo layout[QL_ROF];

static void buildLayout(void) {
    int i, coff, doff;
    
    coff = 0;
    doff = 0;
    
    for (i = 0; i < rofN; i++) {
        layout[i].codeBase = coff;
        layout[i].codeSize = rof[i].cod;
        layout[i].idatBase = doff;
        layout[i].idatSize = rof[i].idat;
        layout[i].statSize = rof[i].stat;
        
        coff = coff + rof[i].cod;
        doff = doff + rof[i].idat;
    }
}

/* ================================================================ Main */

int main(int argc, char **argv) {
    const char *infile = NULL, *outfile = NULL;
    int i;
    
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'O' && argv[i][2] == '=') {
            outfile = argv[i] + 3;
        } else if (infile == NULL) {
            infile = argv[i];
        }
    }
    
    if (!infile || !outfile) {
        printf("Usage: qlv32 <input.r> -O=<output.module>\n");
        return 1;
    }
    
    if ((inLen = readFile(infile)) < 0) {
        return 1;
    }
    
    printf("Read %d bytes from %s\n", inLen, infile);
    
    /* Parse ROF */
    rofParse(0);
    
    if (rofN == 0) {
        fatal("no ROF found");
    }
    
    /* Extract symbols from globals */
    for (i = 0; i < rofN; i++) {
        int at = rof[i].globAt;
        int j;
        for (j = 0; j < rof[i].globN; j++) {
            int nameAt = at;
            const char *name = &inBuf[nameAt];
            at = skipName(at);
            int type = be16(at);
            int value = be32(at + 2);
            at = at + 6;
            
            symAdd(name, value + layout[i].codeBase, type, i);
        }
    }
    
    /* Build output layout */
    buildLayout();
    
    /* Calculate total sizes */
    int totalCode = 0, totalIdat = 0;
    for (i = 0; i < rofN; i++) {
        totalCode = totalCode + layout[i].codeSize;
        totalIdat = totalIdat + layout[i].idatSize;
    }
    
    printf("Total: %d bytes code, %d bytes initialized data\n", totalCode, totalIdat);
    
    /* Build output module in RISC-V-compatible format */
    outLen = 0;
    
    /* Helper macros for big-endian output */
    #define outByte(b) do { \
        if (outLen < QL_OUT) outBuf[outLen++] = (b); \
    } while(0)
    
    #define outWord(w) do { \
        int _w = (w); \
        outByte((_w >> 8) & 0xFF); \
        outByte(_w & 0xFF); \
    } while(0)
    
    #define outDword(d) do { \
        int _d = (d); \
        outWord((_d >> 16) & 0xFFFF); \
        outWord(_d & 0xFFFF); \
    } while(0)
    
    #define outStr(s) do { \
        const char* _s = (s); \
        while (*_s && outLen < QL_OUT) outBuf[outLen++] = *_s++; \
        if (outLen < QL_OUT) outBuf[outLen++] = 0; \
    } while(0)
    
    /* Header (56 bytes, big-endian) */
    RofInfo *root = &rof[rofRoot];
    outDword(0xDEADFACE);           /* sync */
    outWord(root->tylan);            /* type/language */
    outWord(root->attrev);           /* attributes/revision */
    outWord(0);                      /* valid */
    outWord(999);                    /* series */
    outByte(126);                    /* year - 1900 */
    outByte(9);                      /* month */
    outByte(16);                     /* day */
    outByte(17);                     /* hour */
    outByte(57);                     /* minute */
    outByte(31);                     /* second */
    outWord(root->edition);          /* edition */
    outDword(0);                     /* static storage */
    outDword(totalIdat);             /* initialized data */
    outDword(totalCode);             /* code size */
    outDword(root->stk);             /* stack */
    outDword(root->entry);           /* entry point */
    outDword(root->trap);            /* trap entry */
    outDword(0);                     /* remote static */
    outDword(0);                     /* remote idata */
    outDword(0);                     /* debug */
    
    /* PSECT name */
    outStr(&inBuf[root->nameAt]);
    
    /* Global symbols */
    outDword(symN);
    for (i = 0; i < symN; i++) {
        outStr(&symPool[symTable[i].name]);
        outWord(symTable[i].type);
        outDword(symTable[i].value);
    }
    
    /* Write header to output */
    if (writeFile(outfile, outLen) < 0) {
        return 1;
    }
    
    /* Append code sections */
    FILE *out = fopen(outfile, "ab");
    if (!out) {
        printf("Cannot open output for append\n");
        return 1;
    }
    
    for (i = 0; i < rofN; i++) {
        fwrite(&inBuf[rof[i].codeAt], 1, rof[i].cod, out);
    }
    
    /* Append initialized data */
    for (i = 0; i < rofN; i++) {
        fwrite(&inBuf[rof[i].idatAt], 1, rof[i].idat, out);
    }
    
    /* External/local counts (zeros for now) */
    unsigned char counts[12];
    memset(counts, 0, 12);
    fwrite(counts, 1, 12, out);
    
    fclose(out);
    
    printf("Wrote %d bytes to %s\n", outLen + totalCode + totalIdat + 12, outfile);
    printf("OK\n");
    
    return 0;
}
