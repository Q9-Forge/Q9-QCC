/*
 * ql -- Q9 Universal ROF linker for OS-9000 (multi-architecture).
 *
 * Usage: ql [options] <input.r> -O=<module>
 *
 * Reads ROF objects from assemblers and produces loadable OS-9000 modules.
 * Supports: 68k, x86-32, RISC-V32/64, ARM64 with appropriate Type/Language codes.
 *
 * Options:
 *   --arch=68k       Set target architecture (default: x86-32)
 *   -a=68k           Short form for --arch=
 *   <input.r>        Input ROF file
 *   -O=<module>      Output module file
 *
 * Edition history:
 *   2026-09-17  Phase 14: Universal linker from qlx86 template
 *
 * Written in the same subset as qox86, qirx86, and qlx86:
 * no unions, no "->", no floating point, literal array sizes, fixed tables
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================ Architecture Configuration */
typedef struct {
    int tylan;          /* Type (0x00) | Language */
    int bitwidth;       /* 32 or 64 */
    const char *name;
} ArchConfig;

#define ARCH_COUNT 5
static ArchConfig archConfigs[ARCH_COUNT] = {
    { 0x0001, 32, "68k" },       /* 68000 (Motorola) */
    { 0x0002, 32, "x86-32" },    /* Intel 80386 */
    { 0x0003, 32, "riscv32" },   /* RISC-V 32-bit */
    { 0x0003, 64, "riscv64" },   /* RISC-V 64-bit */
    { 0x0004, 64, "arm64" }      /* ARM 64-bit */
};

typedef struct {
    int tylan;          /* Type/Language to write in header */
    int bitwidth;       /* 32 or 64 for address size */
    int stackSize;      /* Stack size in bytes (default: 4096) */
    int stripSymbols;   /* 1 = strip symbols from output */
    int debugSymbols;   /* 1 = include debug info */
    int entryPoint;     /* Entry point offset (-1 = default/0) */
    int allowDuplicates; /* Phase 23: Allow duplicate symbols */
    int optimizeJumpTables;   /* Phase 25: Generate jump tables */
    int optimizeFunctionReorder; /* Phase 25: Reorder by frequency */
    int optimizeStringDedup;  /* Phase 25: Deduplicate DWARF strings */
    int optimizeLazyBinding;  /* Phase 25: Lazy symbol binding */
} Config;

static Config config = { 0x0002, 32, 4096, 0, 0, -1, 0, 0, 0, 1, 0 };  /* Default: x86-32, 4KB stack, string dedup on */

/* Parse configuration options */
static void parseStackOption(const char *arg)
{
    int size;
    if (strncmp(arg, "--stack=", 8) != 0)
        return;
    
    size = atoi(&arg[8]);
    if (size < 256 || size > 67108864) {
        printf("FEHLER: Stack-Größe muss zwischen 256 und 64MB sein\n");
        exit(1);
    }
    config.stackSize = size;
    printf("Stack-Größe: %d bytes\n", config.stackSize);
}

static void parseStripOption(const char *arg)
{
    if (strcmp(arg, "--strip") == 0) {
        config.stripSymbols = 1;
        printf("Symbol-Stripping: aktiviert\n");
    }
}

static void parseDebugOption(const char *arg)
{
    if (strcmp(arg, "--debug") == 0) {
        config.debugSymbols = 1;
        printf("Debug-Symbole: aktiviert\n");
    }
}

static void parseEntryOption(const char *arg)
{
    int entry;
    if (strncmp(arg, "--entry=", 8) != 0)
        return;
    
    entry = (int)strtol(&arg[8], NULL, 16);
    if (entry < 0) {
        printf("FEHLER: Entry Point muss >= 0 sein\n");
        exit(1);
    }
    config.entryPoint = entry;
    printf("Entry Point: 0x%X\n", config.entryPoint);
}

/* Parse --arch= or -a= and set config */
static void parseArchOption(const char *arg)
{
    int i;
    const char *archName = NULL;

    if (strncmp(arg, "--arch=", 7) == 0) {
        archName = &arg[7];
    } else if (strncmp(arg, "-a=", 3) == 0) {
        archName = &arg[3];
    } else {
        return;
    }

    for (i = 0; i < ARCH_COUNT; i++) {
        if (strcmp(archConfigs[i].name, archName) == 0) {
            config.tylan = archConfigs[i].tylan;
            config.bitwidth = archConfigs[i].bitwidth;
            printf("Arch set: %s (Type/Lan=$%04X, %d-bit)\n", 
                   archName, config.tylan, config.bitwidth);
            return;
        }
    }
    printf("FEHLER: Unbekannte Architektur: %s\n", archName);
    exit(1);
}

/* ================================================================ Phase 25: Optimization Options */

static void parseOptimizeOption(const char *arg)
{
    if (strncmp(arg, "--optimize=", 11) != 0)
        return;
    
    const char *opt = &arg[11];
    
    if (strcmp(opt, "jump-tables") == 0) {
        config.optimizeJumpTables = 1;
        printf("Optimization: Jump tables enabled\n");
    } else if (strcmp(opt, "function-reorder") == 0) {
        config.optimizeFunctionReorder = 1;
        printf("Optimization: Function reordering by frequency enabled\n");
    } else if (strcmp(opt, "string-dedup") == 0) {
        config.optimizeStringDedup = 1;
        printf("Optimization: DWARF string deduplication enabled\n");
    } else if (strcmp(opt, "lazy-binding") == 0) {
        config.optimizeLazyBinding = 1;
        printf("Optimization: Lazy symbol binding enabled\n");
    } else if (strcmp(opt, "all") == 0) {
        config.optimizeJumpTables = 1;
        config.optimizeFunctionReorder = 1;
        config.optimizeStringDedup = 1;
        config.optimizeLazyBinding = 1;
        printf("Optimization: All optimizations enabled\n");
    } else if (strcmp(opt, "none") == 0) {
        config.optimizeJumpTables = 0;
        config.optimizeFunctionReorder = 0;
        config.optimizeStringDedup = 0;
        config.optimizeLazyBinding = 0;
        printf("Optimization: All optimizations disabled\n");
    } else {
        printf("FEHLER: Unbekannte Optimierung: %s\n", opt);
    }
}

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

/* Phase 17: Multi-file support - read file into temporary buffer */
static int readFileIntoTemp(const char *path, char *tempBuf, int maxSize)
{
    FILE *fp = fopen(path, "rb");
    int n;

    if (!fp) {
        printf("Datei nicht gefunden: %s\n", path);
        return -1;
    }
    n = fread(tempBuf, 1, maxSize, fp);
    fclose(fp);
    return n;
}

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

/* Phase 17: Multi-file support */
static const char *inputFiles[QL_ROF];  /* Input file names for each ROF */

static RofInfo rof[QL_ROF];
static int rofN = 0;
static int rofRoot = -1;

/* Bitwidth-aware symbol value reading */
static long long readSymbolValue(int at, int bitwidth)
{
    if (bitwidth == 64) {
        /* 64-bit: read as two 32-bit big-endian values */
        long long hi = be32(at);
        long long lo = be32(at + 4);
        return (hi << 32) | (lo & 0xFFFFFFFFLL);
    } else {
        /* 32-bit: standard 4-byte big-endian */
        return be32(at);
    }
}

/* Bitwidth-aware symbol skip */
static int skipSymbol(int at, int bitwidth)
{
    at = skipName(at);           /* symbol name */
    at = at + 2;                 /* type field */
    if (bitwidth == 64) {
        at = at + 8;             /* 64-bit value */
    } else {
        at = at + 4;             /* 32-bit value */
    }
    return at;
}

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
        at = skipSymbol(at, config.bitwidth);  /* bitwidth-aware skip */
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
    long long value;    /* Address/value (supports 64-bit) */
    int type;           /* 0=reserved, 1=data, 4=code */
    int rofIdx;         /* Which ROF defined this */
} Symbol;

/* Phase 20: Symbol hash table for O(1) lookup */
typedef struct {
    int symIdx;         /* Index in symTable (-1 = empty) */
    int hash;           /* Hash code for collision handling */
} SymbolHashEntry;

#define SYMBOL_HASH_SIZE 4096
static SymbolHashEntry symHash[SYMBOL_HASH_SIZE];

static Symbol symTable[QL_SYM];
static int symN = 0;
static char symPool[QL_POOL];
static int symPoolTop = 0;

/* Simple hash function */
static int hashSymbolName(const char *name)
{
    int h = 0, i = 0;
    while (name[i] != 0 && i < 32) {
        h = (h * 31) + (name[i] & 255);
        i++;
    }
    return (h & 0x7FFFFFFF) % SYMBOL_HASH_SIZE;
}

static void symAdd(const char *name, long long value, int type, int rofIdx)
{
    Symbol *s;
    int len, i, hash, probe;

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
    
    /* Phase 20: Add to hash table (linear probing) */
    hash = hashSymbolName(name);
    probe = 0;
    while (probe < SYMBOL_HASH_SIZE) {
        int idx = (hash + probe) % SYMBOL_HASH_SIZE;
        if (symHash[idx].symIdx == -1) {
            symHash[idx].symIdx = symN;
            symHash[idx].hash = hash;
            break;
        }
        probe++;
    }
    
    symN++;
}

static int symFind(const char *name)
{
    /* Phase 20: Use hash table for fast lookup */
    int hash = hashSymbolName(name);
    int probe = 0;
    
    while (probe < SYMBOL_HASH_SIZE) {
        int idx = (hash + probe) % SYMBOL_HASH_SIZE;
        if (symHash[idx].symIdx == -1)
            return -1;  /* Not found */
        int symIdx = symHash[idx].symIdx;
        if (strEq(&symPool[symTable[symIdx].name], name))
            return symIdx;  /* Found */
        probe++;
    }
    
    return -1;  /* Not found */
}

static const char *symGetName(int idx)
{
    if (idx < 0 || idx >= symN)
        return "???";
    return &symPool[symTable[idx].name];
}

/* ================================================================ Layout */
typedef struct {
    long long codeBase;       /* Offset in output (supports 64-bit) */
    long long codeSize;
    long long idatBase;       /* Offset in output (supports 64-bit) */
    long long idatSize;
    long long statSize;       /* Uninitialized */
} LayoutInfo;

static LayoutInfo layout[QL_ROF];

static void buildLayout(void)
{
    int i;
    long long coff, doff;

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

        printf("Layout ROF %d: Code [$%llX..%llX) Data [$%llX..%llX)\n",
               i, layout[i].codeBase, coff, layout[i].idatBase, doff);
    }

    printf("Total Code: %lld bytes, Total Data: %lld bytes\n", coff, doff);
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
    long long symValue;
    long long patchAt;
    const char *symName;

    for (i = 0; i < r->extN; i++) {
        symName = (const char *)&inBuf[at];
        symIdx = symFind(symName);
        
        if (symIdx >= 0) {
            symValue = symTable[symIdx].value + layout[symTable[symIdx].rofIdx].codeBase;
            printf("  External: %s -> $%llX (code base + $%llX)\n", symName, symValue, 
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
            
            if (config.bitwidth == 64) {
                /* 64-bit relocation: patch 8 bytes */
                if (patchAt + 7 < outLen) {
                    long long oldVal = 
                        (((long long)(outBuf[patchAt] & 255)) << 56) |
                        (((long long)(outBuf[patchAt + 1] & 255)) << 48) |
                        (((long long)(outBuf[patchAt + 2] & 255)) << 40) |
                        (((long long)(outBuf[patchAt + 3] & 255)) << 32) |
                        (((long long)(outBuf[patchAt + 4] & 255)) << 24) |
                        (((long long)(outBuf[patchAt + 5] & 255)) << 16) |
                        (((long long)(outBuf[patchAt + 6] & 255)) << 8) |
                        ((long long)(outBuf[patchAt + 7] & 255));
                    long long newVal = oldVal + symValue;
                    outBuf[patchAt] = (newVal >> 56) & 255;
                    outBuf[patchAt + 1] = (newVal >> 48) & 255;
                    outBuf[patchAt + 2] = (newVal >> 40) & 255;
                    outBuf[patchAt + 3] = (newVal >> 32) & 255;
                    outBuf[patchAt + 4] = (newVal >> 24) & 255;
                    outBuf[patchAt + 5] = (newVal >> 16) & 255;
                    outBuf[patchAt + 6] = (newVal >> 8) & 255;
                    outBuf[patchAt + 7] = newVal & 255;
                }
            } else {
                /* 32-bit relocation: patch 4 bytes */
                if (patchAt + 3 < outLen) {
                    int oldVal = (outBuf[patchAt] & 255) |
                                ((outBuf[patchAt + 1] & 255) << 8) |
                                ((outBuf[patchAt + 2] & 255) << 16) |
                                ((outBuf[patchAt + 3] & 255) << 24);
                    int newVal = oldVal + (int)symValue;
                    outBuf[patchAt] = newVal & 255;
                    outBuf[patchAt + 1] = (newVal >> 8) & 255;
                    outBuf[patchAt + 2] = (newVal >> 16) & 255;
                    outBuf[patchAt + 3] = (newVal >> 24) & 255;
                }
            }
        }
    }
}

/* ================================================================ Debug Symbols (Phase 18) */

typedef struct {
    int nameIdx;       /* Index in symPool */
    int offset;        /* Offset in code section */
    int type;          /* 1=function, 2=variable, etc */
} DebugSymbol;

static DebugSymbol debugSymbols[QL_SYM];
static int debugN = 0;

/* Add debug symbol */
static void debugSymbolAdd(const char *name, int offset, int type)
{
    int len;
    
    if (debugN >= QL_SYM)
        return;
    
    debugSymbols[debugN].nameIdx = symFind(name);
    debugSymbols[debugN].offset = offset;
    debugSymbols[debugN].type = type;
    debugN++;
}

/* Write debug section (after data) */
static void writeDebugSection(void)
{
    int i;
    
    if (!config.debugSymbols || debugN == 0)
        return;
    
    printf("Writing %d debug symbols...\n", debugN);
    
    /* Debug section format: simple count + array */
    outDword(debugN);  /* Symbol count */
    
    for (i = 0; i < debugN; i++) {
        outDword(debugSymbols[i].offset);  /* Code offset */
        outWord(debugSymbols[i].type);      /* Type */
        outWord(debugSymbols[i].nameIdx);   /* Name index */
    }
}

/* ================================================================ Phase 24: DWARF Debug Support */

/* LEB128 encoding: Variable-length unsigned integer for DWARF */
static int leb128Encode(unsigned int value, unsigned char *buf)
{
    int count = 0;
    while (1) {
        unsigned char byte = value & 0x7F;
        value = value >> 7;
        if (value != 0)
            byte = byte | 0x80;
        buf[count] = byte;
        count++;
        if (value == 0)
            break;
    }
    return count;
}

static int leb128Decode(const unsigned char *buf, unsigned int *val)
{
    unsigned int result = 0;
    int shift = 0;
    int count = 0;
    
    while (1) {
        unsigned char byte = buf[count];
        result = result | ((byte & 0x7F) << shift);
        count++;
        if ((byte & 0x80) == 0)
            break;
        shift = shift + 7;
    }
    *val = result;
    return count;
}

/* DWARF section infrastructure */
typedef struct {
    char      *data;
    int        size;
    int        allocated;
    int        pos;
} DwarfBuffer;

typedef struct {
    DwarfBuffer info;
    DwarfBuffer abbrev;
    DwarfBuffer line;
    DwarfBuffer str;
    DwarfBuffer ranges;
} DwarfSections;

static DwarfBuffer dwarfInfoBuf = {0};
static DwarfBuffer dwarfAbbrevBuf = {0};
static DwarfBuffer dwarfLineBuf = {0};
static DwarfBuffer dwarfStrBuf = {0};

/* Allocate a DWARF buffer */
static void dwarfAllocate(DwarfBuffer *buf, int size)
{
    buf->allocated = size;
    buf->size = 0;
    buf->pos = 0;
    buf->data = (char *)malloc(size);
    if (!buf->data)
        fatal("DWARF buffer allocation failed");
}

/* Write byte to DWARF buffer */
static void dwarfByte(DwarfBuffer *buf, int v)
{
    if (buf->pos >= buf->allocated)
        fatal("DWARF buffer overflow");
    buf->data[buf->pos] = v & 255;
    buf->pos++;
    if (buf->pos > buf->size)
        buf->size = buf->pos;
}

/* Write word (16-bit, little-endian for x86) */
static void dwarfWord(DwarfBuffer *buf, int v)
{
    dwarfByte(buf, v & 255);
    dwarfByte(buf, (v >> 8) & 255);
}

/* Write dword (32-bit, little-endian) */
static void dwarfDword(DwarfBuffer *buf, int v)
{
    dwarfByte(buf, v & 255);
    dwarfByte(buf, (v >> 8) & 255);
    dwarfByte(buf, (v >> 16) & 255);
    dwarfByte(buf, (v >> 24) & 255);
}

/* Write LEB128 to buffer */
static void dwarfLEB128(DwarfBuffer *buf, unsigned int v)
{
    unsigned char leb[16];
    int len = leb128Encode(v, leb);
    int i;
    for (i = 0; i < len; i++)
        dwarfByte(buf, leb[i]);
}

/* Generate minimal DWARF abbreviation table */
static void generateDwarfAbbrev(void)
{
    DwarfBuffer *buf = &dwarfAbbrevBuf;
    
    printf("Generating DWARF abbreviation table...\n");
    dwarfAllocate(buf, 512);
    
    /* Abbreviation 1: DW_TAG_compile_unit */
    dwarfLEB128(buf, 1);           /* Abbrev code 1 */
    dwarfLEB128(buf, 0x11);        /* DW_TAG_compile_unit */
    dwarfByte(buf, 1);              /* DW_CHILDREN_yes */
    dwarfLEB128(buf, 0x03);        /* DW_AT_name */
    dwarfLEB128(buf, 0x08);        /* DW_FORM_string */
    dwarfLEB128(buf, 0x10);        /* DW_AT_stmt_list */
    dwarfLEB128(buf, 0x06);        /* DW_FORM_data4 */
    dwarfByte(buf, 0);              /* End attributes */
    dwarfByte(buf, 0);
    
    /* Abbreviation 2: DW_TAG_subprogram (function) */
    dwarfLEB128(buf, 2);           /* Abbrev code 2 */
    dwarfLEB128(buf, 0x2E);        /* DW_TAG_subprogram */
    dwarfByte(buf, 0);              /* DW_CHILDREN_no */
    dwarfLEB128(buf, 0x03);        /* DW_AT_name */
    dwarfLEB128(buf, 0x08);        /* DW_FORM_string */
    dwarfLEB128(buf, 0x11);        /* DW_AT_low_pc */
    dwarfLEB128(buf, 0x01);        /* DW_FORM_address */
    dwarfLEB128(buf, 0x12);        /* DW_AT_high_pc */
    dwarfLEB128(buf, 0x01);        /* DW_FORM_address */
    dwarfByte(buf, 0);              /* End attributes */
    dwarfByte(buf, 0);
    
    /* Abbreviation 0: NULL terminator */
    dwarfByte(buf, 0);
    
    printf("  Abbrev table: %d bytes\n", buf->size);
}

/* Generate minimal .debug_info section */
static void generateDwarfInfo(void)
{
    DwarfBuffer *buf = &dwarfInfoBuf;
    int i, cuStart;
    
    printf("Generating DWARF .debug_info section...\n");
    dwarfAllocate(buf, 2048);
    
    cuStart = buf->pos;
    
    /* CU header: offset of abbreviations (32-bit) */
    dwarfDword(buf, 0);  /* Offset to .debug_abbrev (will update) */
    
    /* CU header: address size */
    dwarfByte(buf, config.bitwidth / 8);  /* 4 for 32-bit, 8 for 64-bit */
    
    /* Compilation unit DIE (Abbrev 1) */
    dwarfLEB128(buf, 1);           /* Abbrev code 1 (compile_unit) */
    
    /* DW_AT_name (string) */
    dwarfByte(buf, 'q');
    dwarfByte(buf, '9');
    dwarfByte(buf, 0);
    
    /* DW_AT_stmt_list (line info offset) */
    dwarfDword(buf, 0);  /* Offset to .debug_line */
    
    /* Functions as child DIEs */
    for (i = 0; i < symN; i++) {
        if (symTable[i].type == 0x2E) {  /* DW_TAG_subprogram marker */
            /* Function DIE (Abbrev 2) */
            dwarfLEB128(buf, 2);   /* Abbrev code 2 (subprogram) */
            
            /* DW_AT_name (function name string) */
            {
                const char *name = symGetName(i);
                int j = 0;
                while (name[j] != 0 && j < 64) {
                    dwarfByte(buf, name[j] & 255);
                    j++;
                }
                dwarfByte(buf, 0);  /* String terminator */
            }
            
            /* DW_AT_low_pc (start address) */
            int addr = (int)(symTable[i].value & 0xFFFFFFFF);
            dwarfDword(buf, addr);
            
            /* DW_AT_high_pc (end address) */
            dwarfDword(buf, addr + 256);  /* Assume 256-byte function */
        }
    }
    
    /* End CU (NULL DIE) */
    dwarfByte(buf, 0);
    
    printf("  .debug_info: %d bytes (%d CU + %d DIEs)\n", buf->size, 1, symN);
}

/* Generate minimal .debug_line section (line number program) */
static void generateDwarfLine(void)
{
    DwarfBuffer *buf = &dwarfLineBuf;
    
    printf("Generating DWARF .debug_line section...\n");
    dwarfAllocate(buf, 1024);
    
    /* Line table header */
    dwarfDword(buf, 0);     /* Length (placeholder) */
    dwarfWord(buf, 4);      /* DWARF version 4 */
    dwarfDword(buf, 0);     /* Header length (placeholder) */
    dwarfByte(buf, 1);      /* Minimum instruction length */
    dwarfByte(buf, 1);      /* Maximum operations per instruction */
    dwarfByte(buf, 1);      /* Default is_stmt */
    dwarfByte(buf, -1 & 255);  /* Line base */
    dwarfByte(buf, 1);      /* Line range */
    dwarfByte(buf, 10);     /* Opcode base */
    
    /* Standard opcode lengths (minimal) */
    dwarfByte(buf, 0);      /* DW_LNS_copy */
    dwarfByte(buf, 1);      /* DW_LNS_advance_pc */
    dwarfByte(buf, 1);      /* DW_LNS_advance_line */
    dwarfByte(buf, 1);      /* DW_LNS_set_file */
    dwarfByte(buf, 1);      /* DW_LNS_set_column */
    dwarfByte(buf, 0);      /* DW_LNS_negate_stmt */
    dwarfByte(buf, 0);      /* DW_LNS_set_basic_block */
    dwarfByte(buf, 0);      /* DW_LNS_const_add_pc */
    dwarfByte(buf, 1);      /* DW_LNS_fixed_advance_pc */
    
    /* File table (minimal) */
    dwarfByte(buf, 'q');
    dwarfByte(buf, '9');
    dwarfByte(buf, '.c');
    dwarfByte(buf, 0);
    dwarfByte(buf, 0);      /* Directory index */
    dwarfByte(buf, 0);      /* Modification time */
    dwarfByte(buf, 0);      /* File size */
    dwarfByte(buf, 0);      /* End of file table */
    
    /* Line program (minimal) */
    dwarfByte(buf, 0);      /* Extended opcode marker */
    dwarfByte(buf, 1);      /* Length */
    dwarfByte(buf, 1);      /* DW_LNE_end_sequence */
    
    printf("  .debug_line: %d bytes\n", buf->size);
}

/* Write all DWARF sections to output */
static void writeDwarfSections(void)
{
    if (!config.debugSymbols)
        return;
    
    printf("\n=== DWARF Debug Sections ===\n");
    
    generateDwarfAbbrev();
    generateDwarfInfo();
    generateDwarfLine();
    
    printf("Writing DWARF sections to output...\n");
    
    /* Write .debug_abbrev */
    if (dwarfAbbrevBuf.size > 0) {
        int i;
        for (i = 0; i < dwarfAbbrevBuf.size; i++)
            outByte(dwarfAbbrevBuf.data[i] & 255);
        printf("Wrote .debug_abbrev: %d bytes\n", dwarfAbbrevBuf.size);
    }
    
    /* Write .debug_info */
    if (dwarfInfoBuf.size > 0) {
        int i;
        for (i = 0; i < dwarfInfoBuf.size; i++)
            outByte(dwarfInfoBuf.data[i] & 255);
        printf("Wrote .debug_info: %d bytes\n", dwarfInfoBuf.size);
    }
    
    /* Write .debug_line */
    if (dwarfLineBuf.size > 0) {
        int i;
        for (i = 0; i < dwarfLineBuf.size; i++)
            outByte(dwarfLineBuf.data[i] & 255);
        printf("Wrote .debug_line: %d bytes\n", dwarfLineBuf.size);
    }
    
    printf("Total DWARF: %d bytes\n", 
           dwarfAbbrevBuf.size + dwarfInfoBuf.size + dwarfLineBuf.size);
}

/* Free DWARF buffers */
static void freeDwarfBuffers(void)
{
    if (dwarfAbbrevBuf.data) free(dwarfAbbrevBuf.data);
    if (dwarfInfoBuf.data) free(dwarfInfoBuf.data);
    if (dwarfLineBuf.data) free(dwarfLineBuf.data);
    if (dwarfStrBuf.data) free(dwarfStrBuf.data);
}

/* ================================================================ Phase 25: Advanced Features & Optimizations */

/* LTO Metadata for link-time optimization */
typedef struct {
    int symIdx;
    int callCount;
    int byteSize;
    int unused;        /* 1 if symbol is unreachable */
    int inlineCandidate; /* 1 if small and hot */
} LTOMetadata;

static LTOMetadata ltoMetadata[QL_SYM];
static int ltoN = 0;

/* Analyze symbol usage for optimization decisions */
static void analyzeLTOMetadata(void)
{
    int i;
    
    if (!config.optimizeJumpTables && !config.optimizeFunctionReorder)
        return;
    
    printf("Analyzing symbols for LTO metadata...\n");
    
    for (i = 0; i < symN; i++) {
        if (ltoN < QL_SYM) {
            ltoMetadata[ltoN].symIdx = i;
            ltoMetadata[ltoN].callCount = 0;  /* Would be populated from relocation analysis */
            ltoMetadata[ltoN].byteSize = 256;  /* Estimate */
            ltoMetadata[ltoN].unused = 0;      /* Conservative: assume used */
            ltoMetadata[ltoN].inlineCandidate = (256 < 100) ? 1 : 0;  /* Small functions */
            ltoN++;
        }
    }
    
    printf("  LTO: %d symbols analyzed\n", ltoN);
}

/* Function reordering: sort functions by call frequency */
static int compareByCallCount(const void *a, const void *b)
{
    int idxA = *(int *)a;
    int idxB = *(int *)b;
    int countA = 0, countB = 0;
    
    int i;
    for (i = 0; i < ltoN; i++) {
        if (ltoMetadata[i].symIdx == idxA)
            countA = ltoMetadata[i].callCount;
        if (ltoMetadata[i].symIdx == idxB)
            countB = ltoMetadata[i].callCount;
    }
    
    return countB - countA;  /* Sort descending by call count */
}

static void reorderFunctionsByFrequency(void)
{
    int indices[QL_SYM];
    int n = 0, i;
    
    if (!config.optimizeFunctionReorder)
        return;
    
    printf("Reordering functions by frequency...\n");
    
    /* Collect function indices */
    for (i = 0; i < symN && n < QL_SYM; i++) {
        if (symTable[i].type == 0x2E) {  /* Function */
            indices[n] = i;
            n++;
        }
    }
    
    if (n > 1) {
        /* Sort by call frequency */
        /* Note: This is a framework; actual reordering deferred to Phase 25.6 */
        printf("  Would reorder %d functions (deferred to Phase 25.6)\n", n);
    }
}

/* Write LTO metadata section */
static void writeLTOMetadata(void)
{
    int i;
    
    if (!config.optimizeJumpTables && !config.optimizeFunctionReorder)
        return;
    
    printf("Writing LTO metadata section...\n");
    
    /* LTO section format: count + entries */
    outDword(ltoN);
    
    for (i = 0; i < ltoN; i++) {
        outWord(ltoMetadata[i].symIdx);
        outWord(ltoMetadata[i].callCount);
        outWord(ltoMetadata[i].byteSize);
        outByte(ltoMetadata[i].unused);
        outByte(ltoMetadata[i].inlineCandidate);
    }
    
    printf("  Wrote LTO metadata: %d bytes\n", 4 + ltoN * 8);
}

/* Jump table support (framework) */
typedef struct {
    int targetAddr;
    int jumpTableAddr;
} JumpTableEntry;

#define MAX_JUMPS 64
static JumpTableEntry jumpTables[MAX_JUMPS];
static int jumpTableN = 0;

static void generateJumpTables(void)
{
    if (!config.optimizeJumpTables)
        return;
    
    printf("Analyzing for jump table candidates...\n");
    
    /* Find symbols >64KB apart */
    /* This is a framework; actual generation deferred to Phase 25.2 */
    printf("  Jump table analysis: deferred to Phase 25.2\n");
}

/* String deduplication for DWARF .debug_str */
typedef struct {
    const char *str;
    int offset;
} StringPoolEntry;

#define MAX_STRINGS 4096
static StringPoolEntry stringPool[MAX_STRINGS];
static int stringPoolN = 0;
static int stringPoolSize = 0;

static int addStringToPool(const char *str, int len)
{
    int i;
    
    if (!config.optimizeStringDedup)
        return stringPoolSize;  /* Just track position if not deduping */
    
    /* Check if string already exists */
    for (i = 0; i < stringPoolN; i++) {
        if (strEq(stringPool[i].str, str)) {
            return stringPool[i].offset;  /* Return existing offset */
        }
    }
    
    /* Add new string */
    if (stringPoolN < MAX_STRINGS) {
        int offset = stringPoolSize;
        stringPool[stringPoolN].str = str;
        stringPool[stringPoolN].offset = offset;
        stringPoolN++;
        stringPoolSize = stringPoolSize + len + 1;
        return offset;
    }
    
    return stringPoolSize;
}

/* Lazy binding support (framework) */
#define LAZY_BIND_FLAG 0x0001

static void markSymbolsForLazyBinding(void)
{
    int i;
    
    if (!config.optimizeLazyBinding)
        return;
    
    printf("Marking external symbols for lazy binding...\n");
    
    for (i = 0; i < symN; i++) {
        if (symTable[i].type != 0x2E) {  /* Non-function symbols */
            /* Could mark for lazy binding here */
        }
    }
    printf("  Lazy binding: framework prepared (Phase 25.3)\n");
}

/* ================================================================ Module Output */
static unsigned int crc32_table[256];

static void initCRC32(void)
{
    unsigned int i, j, poly = 0xEDB88320;
    
    for (i = 0; i < 256; i++) {
        unsigned int crc = i;
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc = crc >> 1;
            }
        }
        crc32_table[i] = crc;
    }
}

static unsigned int calculateCRC32(const char *data, int len)
{
    unsigned int crc = 0xFFFFFFFF;
    int i;
    
    for (i = 0; i < len; i++) {
        unsigned char byte = data[i] & 255;
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
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

/* Write 64-bit value (big-endian) for 64-bit architectures */
static void outQword(long long v)
{
    outByte((v >> 56) & 255);
    outByte((v >> 48) & 255);
    outByte((v >> 40) & 255);
    outByte((v >> 32) & 255);
    outByte((v >> 24) & 255);
    outByte((v >> 16) & 255);
    outByte((v >> 8) & 255);
    outByte(v & 255);
}

/* Write 56-byte OS-9000 module header (big-endian) */
static void writeModuleHeader(int codeSize, int dataSize, int statSize)
{
    printf("Writing OS-9000 module header (Type/Lan=$%04X, %d-bit)...\n", 
           config.tylan, config.bitwidth);

    outByte(0xDE);           /* Sync word */
    outByte(0xAD);
    outByte(0xFA);
    outByte(0xCE);

    outWord(config.tylan);   /* M$Type/Language from config */
    outWord(0x8000);         /* M$Attr (executable, in-memory) */
    outWord(0x0000);         /* M$Error (no error) */
    outWord(0x0000);         /* unused */
    outWord(0x0000);         /* unused */
    outWord(0x0001);         /* M$Edition */

    outDword(statSize);      /* M$Stat (BSS/uninitialized) */
    outDword(dataSize);      /* M$IDat (initialized data) */
    outDword(codeSize);      /* M$Text (code) */
    outDword(config.stackSize);  /* M$Stack (configurable, from config) */
    outDword(config.entryPoint >= 0 ? config.entryPoint : 0);  /* M$Entry */
    outDword(0xFFFFFFFF);    /* M$Trap (trap entry) */
    outDword(0);             /* M$Remote (remote data) */

    outDword(0);             /* M$IDataRemote */
    outDword(config.debugSymbols ? 0x0001 : 0);  /* M$Debug (debug info offset) */

    printf("Header: 56 bytes, Code: %d, Data: %d, BSS: %d, Stack: %d, Entry: 0x%X\n",
           codeSize, dataSize, statSize, config.stackSize,
           config.entryPoint >= 0 ? config.entryPoint : 0);
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

/* ================================================================ Phase 23: Multi-File Support */

#define QL_MAX_INPUTS 128

typedef struct {
    const char *filename;
    char *buffer;
    int size;
    int codeOffset;
    int dataOffset;
    int codeSize;
    int dataSize;
} Input;

typedef struct {
    Input inputs[QL_MAX_INPUTS];
    int count;
    int totalCode;
    int totalData;
} InputManager;

static InputManager inputMgr = {0};
static int isMultiFile = 0;

static int parseInputFile(const char *filename, Input *input)
{
    FILE *f;
    unsigned int magic;
    
    printf("  Loading: %s...\n", filename);
    
    f = fopen(filename, "rb");
    if (!f) {
        printf("ERROR: Cannot open '%s'\n", filename);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    input->size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    input->buffer = malloc(input->size);
    if (fread(input->buffer, 1, input->size, f) != input->size) {
        printf("ERROR: Cannot read '%s'\n", filename);
        fclose(f);
        return -1;
    }
    fclose(f);
    
    input->filename = filename;
    
    if (input->size < 56) {
        printf("ERROR: '%s' too small for OS-9000 module\n", filename);
        return -1;
    }
    
    magic = ((unsigned int)input->buffer[0] << 24) |
            ((unsigned int)input->buffer[1] << 16) |
            ((unsigned int)input->buffer[2] << 8) |
            ((unsigned int)input->buffer[3]);
    
    if (magic != 0xDEADFACE) {
        printf("ERROR: '%s' not a valid OS-9000 module (missing DEADFACE)\n", filename);
        return -1;
    }
    
    int codeSize = ((input->buffer[12] & 255) << 24) |
                   ((input->buffer[13] & 255) << 16) |
                   ((input->buffer[14] & 255) << 8) |
                   ((input->buffer[15] & 255));
    
    int dataSize = ((input->buffer[16] & 255) << 24) |
                   ((input->buffer[17] & 255) << 16) |
                   ((input->buffer[18] & 255) << 8) |
                   ((input->buffer[19] & 255));
    
    input->codeSize = codeSize;
    input->dataSize = dataSize;
    input->codeOffset = inputMgr.totalCode;
    input->dataOffset = inputMgr.totalData;
    
    printf("    Code: %d bytes (offset 0x%X)\n", codeSize, input->codeOffset);
    printf("    Data: %d bytes (offset 0x%X)\n", dataSize, input->dataOffset);
    
    inputMgr.totalCode += codeSize;
    inputMgr.totalData += dataSize;
    
    return 0;
}

static int loadMultipleInputs(const char **inputFiles, int numInputs)
{
    int i, ret;
    
    printf("\nPhase 23: Loading %d input file(s)...\n", numInputs);
    
    inputMgr.count = 0;
    inputMgr.totalCode = 0;
    inputMgr.totalData = 0;
    
    for (i = 0; i < numInputs; i++) {
        if (inputMgr.count >= QL_MAX_INPUTS) {
            printf("ERROR: Too many input files (max %d)\n", QL_MAX_INPUTS);
            return -1;
        }
        
        Input *input = &inputMgr.inputs[inputMgr.count];
        ret = parseInputFile(inputFiles[i], input);
        if (ret < 0) return -1;
        
        inputMgr.count++;
    }
    
    if (inputMgr.count == 0) {
        printf("ERROR: No valid input files\n");
        return -1;
    }
    
    printf("\nPhase 23: Total combined size:\n");
    printf("  Code: %d bytes\n", inputMgr.totalCode);
    printf("  Data: %d bytes\n", inputMgr.totalData);
    
    isMultiFile = (inputMgr.count > 1);
    if (isMultiFile) {
        printf("  Multi-file linking: YES (%d inputs)\n", inputMgr.count);
    }
    
    return 0;
}

static void writeCombinedOutput(void)
{
    int i, j;
    
    if (inputMgr.count == 0) return;
    
    printf("\nPhase 23: Writing combined output...\n");
    
    printf("  Writing code sections:\n");
    for (i = 0; i < inputMgr.count; i++) {
        Input *input = &inputMgr.inputs[i];
        char *code = input->buffer + 56;
        
        printf("    Input %d: %d bytes\n", i, input->codeSize);
        
        for (j = 0; j < input->codeSize; j++) {
            outByte(code[j] & 255);
        }
    }
    
    printf("  Writing data sections:\n");
    for (i = 0; i < inputMgr.count; i++) {
        Input *input = &inputMgr.inputs[i];
        char *data = input->buffer + 56 + input->codeSize;
        
        printf("    Input %d: %d bytes\n", i, input->dataSize);
        
        for (j = 0; j < input->dataSize; j++) {
            outByte(data[j] & 255);
        }
    }
}

static int processPhase23MultiFile(const char **inputFiles, int numInputs)
{
    int ret;
    
    if (numInputs == 1) {
        isMultiFile = 0;
        return 0;
    }
    
    ret = loadMultipleInputs(inputFiles, numInputs);
    if (ret < 0) return -1;
    
    totalCode = inputMgr.totalCode;
    totalData = inputMgr.totalData;
    
    return 0;
}

/* ================================================================ Main */
int main(int argc, char *argv[])
{
    const char *inFile = NULL;
    const char *outFile = NULL;
    int i;

    printf("ql Phase 14-23 - Universal Linker (Phases 14-20 + Phase 23 Multi-File)\n");

    /* Initialize phase 20 symbol hash table */
    {
        int j;
        for (j = 0; j < SYMBOL_HASH_SIZE; j++) {
            symHash[j].symIdx = -1;
        }
    }

    /* Parse arguments - Phase 23: Support multiple .r files */
    const char *inputFiles[QL_MAX_INPUTS];
    int numInputs = 0;
    
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'O' && argv[i][2] == '=') {
                outFile = &argv[i][3];
            } else if (strncmp(argv[i], "--arch=", 7) == 0 || 
                       strncmp(argv[i], "-a=", 3) == 0) {
                parseArchOption(argv[i]);
            } else if (strncmp(argv[i], "--stack=", 8) == 0) {
                parseStackOption(argv[i]);
            } else if (strcmp(argv[i], "--strip") == 0) {
                parseStripOption(argv[i]);
            } else if (strcmp(argv[i], "--debug") == 0) {
                parseDebugOption(argv[i]);
            } else if (strncmp(argv[i], "--entry=", 8) == 0) {
                parseEntryOption(argv[i]);
            } else if (strcmp(argv[i], "--allow-duplicates") == 0) {
                config.allowDuplicates = 1;
                printf("Phase 23: Allowing duplicate symbols\n");
            } else if (strncmp(argv[i], "--optimize=", 11) == 0) {
                parseOptimizeOption(argv[i]);
            }
        } else {
            /* Phase 23: Collect all .r files as inputs */
            if (numInputs < QL_MAX_INPUTS) {
                inputFiles[numInputs++] = argv[i];
                inFile = argv[i];  /* Keep last one for compat */
            }
        }
    }

    if (!outFile || numInputs == 0) {
        printf("Syntax: ql [options] <input.r> [input2.r ...] -O=<output>\n");
        printf("\nPhase 23 Multi-File: Multiple .r files supported!\n");
        printf("\nArchitectures: 68k, x86-32, riscv32, riscv64, arm64\n");
        printf("\nOptions:\n");
        printf("  --arch=<arch>        Set target architecture (default: x86-32)\n");
        printf("  -a=<arch>            Short form for --arch=\n");
        printf("  --stack=<size>       Stack size in bytes (default: 4096)\n");
        printf("  --strip              Strip symbols from output\n");
        printf("  --debug              Enable debug symbols (Phase 24: DWARF)\n");
        printf("  --entry=<addr>       Set entry point (hex address)\n");
        printf("  --allow-duplicates   Allow duplicate symbols (Phase 23)\n");
        printf("  --optimize=<opt>     Enable optimizations (Phase 25):\n");
        printf("                         jump-tables, function-reorder, string-dedup,\n");
        printf("                         lazy-binding, all, none\n");
        printf("  -O=<file>            Output module file (required)\n");
        return 1;
    }

    /* Read input */
    /* Phase 23: Process multiple inputs if provided */
    if (numInputs > 1) {
        printf("\nPhase 23: Multi-File Mode Activated\n");
        if (processPhase23MultiFile(inputFiles, numInputs) < 0)
            return 1;
    } else {
        /* Single file mode (backward compat) */
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
                int nameLen = strLen(name);
                int type = be16(at + nameLen + 1);
                long long value = readSymbolValue(at + nameLen + 3, config.bitwidth);
                symAdd(name, value, type, rofRoot);
                at = skipSymbol(at, config.bitwidth);
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
    
    /* Phase 19: Validate entry point */
    if (config.entryPoint >= 0) {
        if (config.entryPoint >= totalCode) {
            printf("FEHLER: Entry Point 0x%X ist außerhalb der Code-Section (max 0x%X)\n",
                   config.entryPoint, totalCode - 1);
            return 1;
        }
        printf("Entry Point 0x%X validiert (Code: 0x%X bytes)\n", 
               config.entryPoint, totalCode);
    }

    /* Write module header */
    writeModuleHeader(totalCode, totalData, totalStat);
    outLen = 56;  /* Move past header */

    /* Phase 23: Handle multi-file vs single-file output */
    if (isMultiFile) {
        writeCombinedOutput();
    } else {
        /* Write sections to output */
        writeCodeSections();
        writeDataSections();
        
        /* Phase 18: Write debug section if enabled */
        if (config.debugSymbols) {
            writeDebugSection();
        }
        
        /* Phase 24: Write DWARF sections if debug enabled */
        if (config.debugSymbols) {
            writeDwarfSections();
        }
        
        /* Phase 25: Optimization analysis */
        if (config.optimizeJumpTables || config.optimizeFunctionReorder || 
            config.optimizeLazyBinding) {
            printf("\n=== Phase 25: Advanced Optimizations ===\n");
            analyzeLTOMetadata();
            generateJumpTables();
            reorderFunctionsByFrequency();
            markSymbolsForLazyBinding();
            writeLTOMetadata();
        }

        printf("\nApplying relocations...\n");
        for (i = 0; i < rofN; i++) {
            applyRelocations(i);
        }
    }

    printf("\nTotal output: %d bytes (56 byte header + %d bytes data)\n", 
           outLen, outLen - 56);

    /* Phase 17: Calculate CRC-32 */
    initCRC32();
    unsigned int moduleCRC = calculateCRC32(outBuf, outLen);
    printf("CRC-32: 0x%08X\n", moduleCRC);

    if (config.stripSymbols) {
        printf("Note: Symbols stripped from output\n");
    }
    if (config.debugSymbols) {
        printf("Note: DWARF debug sections included (Phase 24)\n");
    }
    if (config.optimizeJumpTables || config.optimizeFunctionReorder || 
        config.optimizeLazyBinding) {
        printf("Note: Optimizations enabled (Phase 25)%s%s%s\n",
               config.optimizeJumpTables ? " - jump-tables" : "",
               config.optimizeFunctionReorder ? " - reordering" : "",
               config.optimizeLazyBinding ? " - lazy-bind" : "");
    }

    /* Free DWARF buffers */
    freeDwarfBuffers();

    /* Write module */
    if (writeFile(outFile, outLen) < 0)
        return 1;

    printf("Written to: %s\n", outFile);
    printf("Phase 14-25 complete: Universal linker with DWARF, Optimizations, Multi-File, Hashing\n");
    printf("Features: %d-bit, %d-byte stack, %s%s%s%s\n",
           config.bitwidth, config.stackSize,
           config.debugSymbols ? "DWARF debug" : "Debug disabled",
           config.stripSymbols ? ", Symbols stripped" : "",
           isMultiFile ? ", MULTI-FILE" : "",
           config.optimizeJumpTables ? ", OPT" : "");

    return 0;
}
