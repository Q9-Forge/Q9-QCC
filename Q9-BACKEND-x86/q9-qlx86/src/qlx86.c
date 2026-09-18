/*
 * qlx86 -- Q9 ROF linker for OS-9000/x86.
 *
 * Usage: qlx86 [options] <input.r> -O=<module>
 *
 * Reads ROF objects (from qox86 assembler) and produces a loadable OS-9000/x86 module.
 *
 * Edition history:
 *   2026-09-16  Phase 12a: ROF Reader foundation
 *
 * Written in the same subset as qox86 and qirx86:
 * no unions, no "->", no floating point, literal array sizes, fixed tables
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================ Constants */
#define QL_IN       33554432        /* Input buffer (host) */
#define QL_OUT      33554432        /* Output buffer (host) */
#define QL_ROF      256             /* Max ROF objects */
#define QL_SYM      32768           /* Max symbols */
#define QL_POOL     1048576         /* Symbol name pool */

static char inBuf[QL_IN];
static int inLen;
static char outBuf[QL_OUT];
static int outLen;

/* ================================================================ Helpers */
static void fatal(const char *msg)
{
    printf("FEHLER: %s\n", msg);
    exit(1);
}

static int strLen(const char *s)
{
    int n = 0;
    while (s[n] != 0)
        n++;
    return n;
}

static int strEq(const char *a, const char *b)
{
    int i = 0;
    while (a[i] != 0 && a[i] == b[i])
        i++;
    return a[i] == b[i];
}

/* ================================================================ I/O */
static int be16(int at)
{
    if (at + 1 >= inLen)
        fatal("be16: buffer underrun");
    return ((inBuf[at] & 255) << 8) | (inBuf[at + 1] & 255);
}

static int be32(int at)
{
    if (at + 3 >= inLen)
        fatal("be32: buffer underrun");
    return ((inBuf[at] & 255) << 24) | ((inBuf[at + 1] & 255) << 16) |
           ((inBuf[at + 2] & 255) << 8) | (inBuf[at + 3] & 255);
}

static int le32(int at)
{
    if (at + 3 >= inLen)
        fatal("le32: buffer underrun");
    return (inBuf[at] & 255) | ((inBuf[at + 1] & 255) << 8) |
           ((inBuf[at + 2] & 255) << 16) | ((inBuf[at + 3] & 255) << 24);
}

/* Skip NUL-terminated name and return offset after it */
static int skipName(int at)
{
    while (at < inLen && inBuf[at] != 0)
        at++;
    return at + 1;
}

/* ================================================================ File I/O */
static int readFile(const char *path)
{
    FILE *fp = fopen(path, "rb");
    int n;

    if (!fp) {
        printf("Datei nicht gefunden: %s\n", path);
        return -1;
    }
    n = fread(inBuf, 1, QL_IN, fp);
    fclose(fp);
    return n;
}

static int writeFile(const char *path, int len)
{
    FILE *fp = fopen(path, "wb");
    int n;

    if (!fp) {
        printf("Datei oeffnen fehlgeschlagen: %s\n", path);
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
    
    int nameAt;         /* Offset in inBuf: PSECT name */
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

static void rofParse(int at0)
{
    RofInfo *r;
    int i, at;

    if (rofN >= QL_ROF)
        fatal("zu viele ROFs");

    r = &rof[rofN];
    rofN++;

    /* Check header length */
    if (at0 + 56 > inLen)
        fatal("ROF zu kurz fuer Header (56 bytes)");

    /* Check sync word (0xDEADFACE big-endian) */
    if ((inBuf[at0] & 255) != 0xDE || (inBuf[at0 + 1] & 255) != 0xAD ||
        (inBuf[at0 + 2] & 255) != 0xFA || (inBuf[at0 + 3] & 255) != 0xCE)
        fatal("kein ROF: Sync ist nicht $DEADFACE");

    /* Parse 56-byte header */
    r->tylan = be16(at0 + 4);
    r->attrev = be16(at0 + 6);
    if (be16(at0 + 8) != 0)
        fatal("ROF mit Fehler gekennzeichnet");
    r->edition = be16(at0 + 18);
    r->stat = be32(at0 + 20);
    r->idat = be32(at0 + 24);
    r->cod = be32(at0 + 28);
    r->stk = be32(at0 + 32);
    r->entry = be32(at0 + 36);
    r->trap = be32(at0 + 40);
    r->rem = be32(at0 + 44);

    if (be32(at0 + 48) != 0)
        fatal("Remote initialized data noch nicht unterstuetzt");
    if (be32(at0 + 52) != 0)
        fatal("Debug info noch nicht unterstuetzt");

    /* Parse PSECT name (after header) */
    at = at0 + 56;
    r->nameAt = at;
    at = skipName(at);

    /* Parse globals */
    r->globN = be32(at);
    r->globAt = at + 4;
    at = r->globAt;
    for (i = 0; i < r->globN; i++) {
        at = skipName(at);       /* symbol name */
        at = at + 6;             /* type (2) + value (4) */
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
        at = skipName(at);           /* symbol name */
        int refcount = be32(at);
        at = at + 4 + refcount * 6;  /* refs */
    }

    /* Local references */
    r->localN = be32(at);
    r->localAt = at + 4;

    /* Determine root psect: first ROF, or first with type/language set */
    if (rofRoot == -1)
        rofRoot = rofN - 1;
    if (r->tylan != 0 && rofRoot == 0)
        rofRoot = rofN - 1;

    printf("[ROF %d] PSECT: %s  Type/Lan: $%04X  Code: %d  Init: %d  Stat: %d\n",
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

static void symAdd(const char *name, int value, int type, int rofIdx)
{
    Symbol *s;
    int len, i;

    if (symN >= QL_SYM)
        fatal("zu viele Symbole");

    len = strLen(name);
    if (symPoolTop + len + 1 >= QL_POOL)
        fatal("Symbol pool voll");

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

static int symFind(const char *name)
{
    int i;
    for (i = symN - 1; i >= 0; i--) {
        if (strEq(&symPool[symTable[i].name], name))
            return i;
    }
    return -1;
}

static const char *symGetName(int idx)
{
    if (idx < 0 || idx >= symN)
        return "???";
    return &symPool[symTable[idx].name];
}

/* ================================================================ Layout */
typedef struct {
    int codeBase;       /* Offset in output */
    int codeSize;
    int idatBase;       /* Offset in output */
    int idatSize;
    int statSize;       /* Uninitialized */
} LayoutInfo;

static LayoutInfo layout[QL_ROF];

static void buildLayout(void)
{
    int i, coff, doff;

    coff = 0;
    doff = 0;

    for (i = 0; i < rofN; i++) {
        layout[i].codeBase = coff;
        layout[i].codeSize = rof[i].cod;
        coff = coff + layout[i].codeSize;

        layout[i].idatBase = doff;
        layout[i].idatSize = rof[i].idat;
        doff = doff + layout[i].idatSize;

        layout[i].statSize = rof[i].stat;

        printf("Layout ROF %d: Code [$%X..%X) Data [$%X..%X)\n",
               i, layout[i].codeBase, coff, layout[i].idatBase, doff);
    }

    printf("Total Code: %d bytes, Total Data: %d bytes\n", coff, doff);
}

/* ================================================================ Relocation */

/* Apply relocations from a ROF's external references */
static void applyRelocations(int rofIdx)
{
    RofInfo *r = &rof[rofIdx];
    int at = r->extAt;
    int i, j;
    int refCount;
    int refType, refOffs;
    int symIdx;
    int symValue;
    int patchAt;
    const char *symName;

    for (i = 0; i < r->extN; i++) {
        symName = (const char *)&inBuf[at];
        symIdx = symFind(symName);
        
        if (symIdx >= 0) {
            symValue = symTable[symIdx].value + layout[symTable[symIdx].rofIdx].codeBase;
            printf("  External: %s -> $%X (code base + $%X)\n", symName, symValue, 
                   symTable[symIdx].value);
        } else {
            printf("  External: %s -> UNDEFINED (0x00000000)\n", symName);
            symValue = 0;
        }

        at = skipName(at);
        refCount = be32(at);
        at = at + 4;

        for (j = 0; j < refCount; j++) {
            refType = be16(at);
            refOffs = be32(at + 2);
            at = at + 6;

            /* Apply relocation at refOffs in code section */
            patchAt = 56 + layout[rofIdx].codeBase + refOffs;
            if (patchAt + 3 < outLen) {
                int oldVal = (outBuf[patchAt] & 255) |
                            ((outBuf[patchAt + 1] & 255) << 8) |
                            ((outBuf[patchAt + 2] & 255) << 16) |
                            ((outBuf[patchAt + 3] & 255) << 24);
                int newVal = oldVal + symValue;
                outBuf[patchAt] = newVal & 255;
                outBuf[patchAt + 1] = (newVal >> 8) & 255;
                outBuf[patchAt + 2] = (newVal >> 16) & 255;
                outBuf[patchAt + 3] = (newVal >> 24) & 255;
            }
        }
    }
}

/* ================================================================ Module Output */
static void outByte(int v)
{
    if (outLen >= QL_OUT)
        fatal("Ausgabepuffer voll");
    outBuf[outLen] = v & 255;
    outLen++;
}

static void outWord(int v)
{
    outByte((v >> 8) & 255);
    outByte(v & 255);
}

static void outDword(int v)
{
    outByte((v >> 24) & 255);
    outByte((v >> 16) & 255);
    outByte((v >> 8) & 255);
    outByte(v & 255);
}

/* Write 56-byte OS-9000 module header (big-endian) */
static void writeModuleHeader(int codeSize, int dataSize, int statSize)
{
    printf("Writing OS-9000 module header...\n");

    outByte(0xDE);           /* Sync word */
    outByte(0xAD);
    outByte(0xFA);
    outByte(0xCE);

    outWord(0x0000);         /* M$Type/Language (0 for user program) */
    outWord(0x8000);         /* M$Attr (executable, in-memory) */
    outWord(0x0000);         /* M$Error (no error) */
    outWord(0x0000);         /* unused */
    outWord(0x0000);         /* unused */
    outWord(0x0001);         /* M$Edition */

    outDword(statSize);      /* M$Stat (BSS/uninitialized) */
    outDword(dataSize);      /* M$IDat (initialized data) */
    outDword(codeSize);      /* M$Text (code) */
    outDword(4096);          /* M$Stack (4 KB) */
    outDword(0);             /* M$Entry (entry point, filled by loader) */
    outDword(0xFFFFFFFF);    /* M$Trap (trap entry) */
    outDword(0);             /* M$Remote (remote data) */

    outDword(0);             /* M$IDataRemote */
    outDword(0);             /* M$Debug */

    printf("Header: 56 bytes, Code: %d, Data: %d, BSS: %d\n",
           codeSize, dataSize, statSize);
}

/* Write code sections */
static void writeCodeSections(void)
{
    int i, at;

    for (i = 0; i < rofN; i++) {
        at = rof[i].codeAt;
        int len = rof[i].cod;
        while (len > 0) {
            outByte(inBuf[at] & 255);
            at++;
            len--;
        }
    }
    printf("Wrote %d bytes of code\n", outLen - 56);
}

/* Write data sections */
static void writeDataSections(void)
{
    int startLen = outLen;
    int i, at;

    for (i = 0; i < rofN; i++) {
        at = rof[i].idatAt;
        int len = rof[i].idat;
        while (len > 0) {
            outByte(inBuf[at] & 255);
            at++;
            len--;
        }
    }
    printf("Wrote %d bytes of initialized data\n", outLen - startLen);
}
int main(int argc, char *argv[])
{
    const char *inFile = NULL;
    const char *outFile = NULL;
    int i;

    printf("qlx86 Phase 12d - Relocation Processing\n");

    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'O' && argv[i][2] == '=')
            outFile = &argv[i][3];
        else
            inFile = argv[i];
    }

    if (!inFile || !outFile) {
        printf("Syntax: qlx86 <input.r> -O=<output>\n");
        return 1;
    }

    /* Read input */
    inLen = readFile(inFile);
    if (inLen < 0)
        return 1;

    printf("Input: %d bytes\n", inLen);

    /* Parse first ROF */
    rofParse(0);

    if (rofRoot == -1)
        fatal("kein Root PSECT gefunden");

    /* Load globals into symbol table */
    {
        RofInfo *root = &rof[rofRoot];
        int at = root->globAt;
        int i;
        for (i = 0; i < root->globN; i++) {
            const char *name = (const char *)&inBuf[at];
            int type = be16(at + strLen(name) + 1);
            int value = be32(at + strLen(name) + 3);
            symAdd(name, value, type, rofRoot);
            at = skipName(at);
            at = at + 6;
        }
    }

    printf("Symbols loaded: %d\n\n", symN);

    /* Build memory layout */
    buildLayout();
    printf("\n");

    /* Calculate totals */
    int totalCode = 0, totalData = 0, totalStat = 0;
    for (i = 0; i < rofN; i++) {
        totalCode = totalCode + rof[i].cod;
        totalData = totalData + rof[i].idat;
        totalStat = totalStat + rof[i].stat;
    }

    /* Write module header */
    writeModuleHeader(totalCode, totalData, totalStat);
    outLen = 56;  /* Move past header */

    /* Write sections to output */
    writeCodeSections();
    writeDataSections();

    printf("\nApplying relocations...\n");
    for (i = 0; i < rofN; i++) {
        applyRelocations(i);
    }

    printf("\nTotal output: %d bytes (56 byte header + %d bytes data)\n", 
           outLen, outLen - 56);

    /* Write module */
    if (writeFile(outFile, outLen) < 0)
        return 1;

    printf("Written to: %s\n", outFile);
    printf("Phase 12c complete: OS-9000 module header and sections\n");

    return 0;
}
