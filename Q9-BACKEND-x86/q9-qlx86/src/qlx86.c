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
#define ROF_PROFILE_QCC       1
#define ROF_PROFILE_MWOS_16   2
#define ROF_PROFILE_MWOS_92   3

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
    if (at < 0 || at > inLen - 2)
        fatal("be16: buffer underrun");
    return ((inBuf[at] & 255) << 8) | (inBuf[at + 1] & 255);
}

static int be32(int at)
{
    if (at < 0 || at > inLen - 4)
        fatal("be32: buffer underrun");
    if ((inBuf[at] & 255) == 0xFF && (inBuf[at + 1] & 255) == 0xFF &&
        (inBuf[at + 2] & 255) == 0xFF && (inBuf[at + 3] & 255) == 0xFF)
        return -1;
    if ((inBuf[at] & 255) >= 0x80)
        fatal("32-Bit-Wert ist groesser als 2 GiB");
    return ((inBuf[at] & 255) << 24) | ((inBuf[at + 1] & 255) << 16) |
           ((inBuf[at + 2] & 255) << 8) | (inBuf[at + 3] & 255);
}

static int le16(int at)
{
    if (at < 0 || at + 1 >= inLen)
        fatal("le16: buffer underrun");
    return (inBuf[at] & 255) | ((inBuf[at + 1] & 255) << 8);
}

static int le32(int at)
{
    if (at < 0 || at > inLen - 4)
        fatal("le32: buffer underrun");
    if ((inBuf[at] & 255) == 0xFF && (inBuf[at + 1] & 255) == 0xFF &&
        (inBuf[at + 2] & 255) == 0xFF && (inBuf[at + 3] & 255) == 0xFF)
        return -1;
    if ((inBuf[at + 3] & 255) >= 0x80)
        fatal("32-Bit-Wert ist groesser als 2 GiB");
    return (inBuf[at] & 255) | ((inBuf[at + 1] & 255) << 8) |
           ((inBuf[at + 2] & 255) << 16) | ((inBuf[at + 3] & 255) << 24);
}

/* Skip NUL-terminated name and return offset after it */
static int skipName(int at)
{
    if (at < 0 || at >= inLen)
        fatal("Name ausserhalb der ROF-Datei");
    while (at < inLen && inBuf[at] != 0)
        at++;
    if (at >= inLen)
        fatal("nicht abgeschlossener Name in ROF-Datei");
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
    if (n == QL_IN && fgetc(fp) != EOF) {
        fclose(fp);
        fatal("Eingabedatei ist groesser als der Eingabepuffer");
    }
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
    int remIdat;        /* Remote initialized data */
    int debug;          /* Debug information */
    int profile;        /* On-disk format profile */
    int codeInfo;       /* Edition 9.2 Code Information word */
    int headerExpand;   /* Edition 9.2 Header Expansion word */
    
    int nameAt;         /* Offset in inBuf: PSECT name */
    int codeAt;         /* Offset: code section */
    int idatAt;         /* Offset: initialized data */
    int globAt;         /* Offset: globals list */
    int globN;          /* Global symbol count */
    int extAt;          /* Offset: external refs */
    int extN;           /* External symbol count */
    int extRefN;        /* Total external reference records */
    int localAt;        /* Offset: local refs */
    int localN;         /* Local symbol count */
    int endAt;          /* Offset after this PSECT in the input stream */
} RofInfo;

static RofInfo rof[QL_ROF];
static int rofN = 0;
static int rofRoot = -1;

static int readCount(int at, int profile)
{
    if (profile == ROF_PROFILE_QCC)
        return be32(at);
    if (profile == ROF_PROFILE_MWOS_16)
        return le16(at);
    return le32(at);
}

static int countWidth(int profile)
{
    return profile == ROF_PROFILE_MWOS_16 ? 2 : 4;
}

static int readSize(int at, int profile)
{
    if (profile == ROF_PROFILE_QCC)
        return be32(at);
    return le32(at);
}

static int rofParse(int at0)
{
    RofInfo *r;
    int i, at, width, refCount, tailAt;

    if (rofN >= QL_ROF)
        fatal("zu viele ROFs");

    r = &rof[rofN];
    rofN++;

    if (at0 < 0 || at0 > inLen - 56)
        fatal("ROF zu kurz fuer Header (56 bytes)");

    /* Check sync word (0xDEADFACE big-endian) */
    if ((inBuf[at0] & 255) != 0xDE || (inBuf[at0 + 1] & 255) != 0xAD ||
        (inBuf[at0 + 2] & 255) != 0xFA || (inBuf[at0 + 3] & 255) != 0xCE)
        fatal("kein ROF: Sync ist nicht $DEADFACE");

    /* QCC prototype files use the explicit BE series marker 999. The image
       samples carry LE series 9 and 16-bit counts. */
    if (be16(at0 + 10) == 999)
        r->profile = ROF_PROFILE_QCC;
    else if (le16(at0 + 10) == 9)
        r->profile = ROF_PROFILE_MWOS_16;
    else if (le16(at0 + 10) == 0x00E9)
        r->profile = ROF_PROFILE_MWOS_92;
    else
        fatal("nicht unterstuetzte ROF-Serie / unbekanntes Format");
    if (r->profile == ROF_PROFILE_MWOS_92 && at0 > inLen - 60)
        fatal("ROF 9.2-shaped Header abgeschnitten");
    width = countWidth(r->profile);

    /* The observed x86 profile stores sizes and edition little-endian. The
       type/language and attribute/revision fields are retained as words. */
    r->tylan = be16(at0 + 4);
    r->attrev = be16(at0 + 6);
    if (be16(at0 + 8) != 0)
        fatal("ROF mit Fehler gekennzeichnet");
    r->edition = r->profile == ROF_PROFILE_QCC ? be16(at0 + 18) : le16(at0 + 18);
    r->stat = readSize(at0 + 20, r->profile);
    r->idat = readSize(at0 + 24, r->profile);
    r->cod = readSize(at0 + 28, r->profile);
    r->stk = readSize(at0 + 32, r->profile);
    r->entry = readSize(at0 + 36, r->profile);
    r->trap = readSize(at0 + 40, r->profile);
    r->rem = readSize(at0 + 44, r->profile);
    r->remIdat = readSize(at0 + 48, r->profile);
    r->debug = readSize(at0 + 52, r->profile);

    /* Edition 9.2-shaped files add Code Information and Header Expansion. */
    r->codeInfo = 0;
    r->headerExpand = 0;
    at = at0 + 56;
    if (r->profile == ROF_PROFILE_MWOS_92) {
        r->codeInfo = le16(at0 + 56);
        r->headerExpand = le16(at0 + 58);
        at = at0 + 60;
    }

    /* Parse PSECT name (after fixed header) */
    r->nameAt = at;
    at = skipName(at);

    /* Parse globals */
    r->globN = readCount(at, r->profile);
    r->globAt = at + width;
    at = r->globAt;
    if (r->globN < 0 || r->globN > inLen - at)
        fatal("Unmoegliche Anzahl globaler Symbole");
    for (i = 0; i < r->globN; i++) {
        if (at >= inLen)
            fatal("Globale Symboltabelle abgeschnitten");
        at = skipName(at);       /* symbol name */
        if (at > inLen - 6)
            fatal("Globales Symbol abgeschnitten");
        at = at + 6;             /* type (2) + value (4) */
    }

    /* Sections */
    r->codeAt = at;
    if (r->cod < 0 || r->cod > inLen - at)
        fatal("Code-Sektion ausserhalb der Datei");
    at = at + r->cod;
    r->idatAt = at;
    if (r->idat < 0 || r->idat > inLen - at)
        fatal("Daten-Sektion ausserhalb der Datei");
    at = at + r->idat;
    if (r->remIdat < 0 || r->debug < 0 || r->remIdat > inLen - at ||
        r->debug > inLen - at - r->remIdat)
        fatal("Zusatzsektionen ausserhalb der Datei");
    at = at + r->remIdat + r->debug;
    if (at < r->idatAt || at > inLen)
        fatal("ROF-Sektionen ausserhalb der Datei");

    /* External references */
    r->extN = readCount(at, r->profile);
    r->extAt = at + width;
    at = r->extAt;
    r->extRefN = 0;
    if (r->extN < 0 || r->extN > inLen - at)
        fatal("Unmoegliche Anzahl externer Symbole");
    for (i = 0; i < r->extN; i++) {
        at = skipName(at);           /* symbol name */
        refCount = readCount(at, r->profile);
        at = at + width;
        if (refCount < 0 || at > inLen || refCount > (inLen - at) / 6)
            fatal("Externe Relocationstabelle abgeschnitten");
        if (r->extRefN > 2147483647 - refCount)
            fatal("Zu viele externe Relokationen");
        r->extRefN += refCount;
        at = at + refCount * 6;
    }

    /* Local references */
    r->localN = readCount(at, r->profile);
    r->localAt = at + width;
    if (r->localN < 0 || r->localAt > inLen || r->localN > (inLen - r->localAt) / 6)
        fatal("Lokale Relocationstabelle abgeschnitten");
    tailAt = r->localAt + r->localN * 6;

    if (r->profile != ROF_PROFILE_QCC) {
        /* The observed v2.2 and v4.7 Microware profiles have a 16-byte zero
           trailer after the local-reference table. */
        if (tailAt + 16 > inLen)
            fatal("Microware-ROF ohne vollstaendigen 16-Byte-Abschluss");
        for (i = 0; i < 16; i++) {
            if (inBuf[tailAt + i] != 0)
                fatal("Unbekannter Inhalt im Microware-ROF-Abschluss");
        }
        r->endAt = tailAt + 16;
    } else {
        int commonN;
        if (tailAt > inLen - 4)
            fatal("QCC-ROF ohne Common-Block-Zaehler");
        commonN = be32(tailAt);
        if (commonN != 0)
            fatal("QCC-ROF mit nicht unterstuetzten Common-Bloecken");
        tailAt += 4;
        r->endAt = tailAt;
    }

    /* Determine root psect: first ROF, or first with type/language set */
    if (rofRoot == -1)
        rofRoot = rofN - 1;
    if (r->tylan != 0 && rofRoot == 0)
        rofRoot = rofN - 1;

    printf("[ROF %d] PSECT: %s  Type/Lan: $%04X  Code: %d  Init: %d  Stat: %d  ",
           rofN - 1, &inBuf[r->nameAt], r->tylan, r->cod, r->idat, r->stat);
    printf("Globals: %d  External: %d names/%d refs  Local refs: %d\n",
           r->globN, r->extN, r->extRefN, r->localN);
    if (r->profile == ROF_PROFILE_MWOS_92)
        printf("  Code Information: $%04X  Header Expansion: $%04X\n",
               r->codeInfo, r->headerExpand);
    return r->endAt;
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

/* ================================================================ Layout */
typedef struct {
    int codeBase;       /* Offset in output */
    int codeSize;       /* PSECT code size including alignment padding */
    int idatBase;       /* Offset in output */
    int idatSize;       /* PSECT initialized-data size including padding */
    int statBase;       /* Offset in the implicit BSS area */
    int statSize;       /* Uninitialized size including padding */
} LayoutInfo;

static LayoutInfo layout[QL_ROF];

static int alignAreaSize(int size, int alignment)
{
    if (size < 0 || size > 0x7FFFFFFF - (alignment - 1))
        fatal("PSECT-Bereichsgroesse ist ungueltig oder zu gross");
    return (size + alignment - 1) & ~(alignment - 1);
}

static int addLayoutSize(int total, int size)
{
    if (size < 0 || total > 0x7FFFFFFF - size)
        fatal("Gesamtgroesse des PSECT-Layouts ist zu gross");
    return total + size;
}

static void buildLayout(int alignment)
{
    int i, coff, doff, boff;

    coff = 0;
    doff = 0;
    boff = 0;

    for (i = 0; i < rofN; i++) {
        layout[i].codeBase = coff;
        layout[i].codeSize = alignAreaSize(rof[i].cod, alignment);
        coff = addLayoutSize(coff, layout[i].codeSize);

        layout[i].idatBase = doff;
        layout[i].idatSize = alignAreaSize(rof[i].idat, alignment);
        doff = addLayoutSize(doff, layout[i].idatSize);

        layout[i].statSize = alignAreaSize(rof[i].stat, alignment);
        layout[i].statBase = boff;
        boff = addLayoutSize(boff, layout[i].statSize);

        printf("Layout ROF %d: Code [$%X..%X) Init [$%X..%X) Stat %d\n",
               i, layout[i].codeBase, coff, layout[i].idatBase, doff,
               layout[i].statSize);
    }

    {
        int totalStat = 0;
        for (i = 0; i < rofN; i++)
            totalStat = addLayoutSize(totalStat, layout[i].statSize);
        printf("Total Stat: %d bytes\n", totalStat);
    }
    printf("Total Code: %d bytes, Total Data: %d bytes\n", coff, doff);
}

/* ================================================================ Relocation */

static void patchLeDword(int at, int value);

/* Apply relocations from a ROF's external references */
static void applyRelocations(int rofIdx, int totalCode, int totalData)
{
    RofInfo *r = &rof[rofIdx];
    int at = r->extAt;
    int i, j, refCount, symIdx, symValue, type, site, outSite, sourceBase;
    const char *symName;

    for (i = 0; i < r->extN; i++) {
        symName = (const char *)&inBuf[at];
        symIdx = symFind(symName);
        if (symIdx < 0 && strncmp(symName, "__offset_", 9) == 0)
            symIdx = symFind(symName + 9);
        
        if (symIdx < 0) {
            fprintf(stderr, "FEHLER: externes Symbol '%s' ist nicht definiert\n", symName);
            exit(1);
        }

        at = skipName(at);
        refCount = be32(at);
        at = at + 4;
        for (j = 0; j < refCount; j++, at += 6) {
            type = be16(at);
            site = be32(at + 2);
            if (symTable[symIdx].type == 4)
                symValue = layout[symTable[symIdx].rofIdx].codeBase + symTable[symIdx].value;
            else if (symTable[symIdx].type == 1)
                symValue = totalCode + layout[symTable[symIdx].rofIdx].idatBase + symTable[symIdx].value;
            else if (symTable[symIdx].type == 2)
                symValue = totalCode + totalData + layout[symTable[symIdx].rofIdx].statBase + symTable[symIdx].value;
            else
                fatal("externe Referenz auf unerkannten Symboltyp wird nicht unterstuetzt");

            if (type == 0x00B8) {
                if (symTable[symIdx].type != 4 || site < 0 || site > r->cod - 4)
                    fatal("ungueltiger externer PC-relativer Codeverweis");
                sourceBase = layout[rofIdx].codeBase;
                outSite = 56 + sourceBase + site;
                patchLeDword(outSite, symValue - (sourceBase + site + 4));
            } else if (type == 0x00B9) {
                if ((symTable[symIdx].type != 1 && symTable[symIdx].type != 2 && symTable[symIdx].type != 4) ||
                    site < 3 || site > r->cod - 4)
                    fatal("ungueltiger externer EDX/PIC-Datenverweis");
                outSite = 56 + layout[rofIdx].codeBase + site;
                patchLeDword(outSite, symValue - (layout[rofIdx].codeBase + site - 3));
            } else if (type == 0x0038 || type == 0x0018) {
                if (type == 0x0038) {
                    if (site < 0 || site > r->cod - 4)
                        fatal("externer Codeverweis ausserhalb der Sektion");
                    outSite = 56 + layout[rofIdx].codeBase + site;
                } else {
                    if (site < 0 || site > r->idat - 4)
                        fatal("externer Datenverweis ausserhalb der Sektion");
                    outSite = 56 + totalCode + layout[rofIdx].idatBase + site;
                }
                patchLeDword(outSite, symValue);
            } else {
                fatal("externer Relokationstyp im QCC-Profil wird nicht unterstuetzt");
            }
            printf("  External: %s type=$%04X site=$%X -> $%X\n", symName, type, site, symValue);
        }
    }

    /* Local references rebase section-relative values across PSECTs. */
    at = r->localAt;
    for (i = 0; i < r->localN; i++, at += 6) {
        int targetType = be16(at);
        int site = be32(at + 2);
        int targetValue;
        int inCode = (targetType & 0x20) != 0;
        int sectionBase = 0, sectionSize = 0, relative = 0;
        int sourceBase;
        if (inCode) {
            if (site < 0 || site > r->cod - 4)
                fatal("lokaler Code-Relokationsort ausserhalb der Sektion");
            sourceBase = 56 + layout[rofIdx].codeBase + site;
        } else {
            if (site < 0 || site > r->idat - 4)
                fatal("lokaler Daten-Relokationsort ausserhalb der Sektion");
            sourceBase = 56 + totalCode + layout[rofIdx].idatBase + site;
        }
        targetValue = (outBuf[sourceBase] & 255) | ((outBuf[sourceBase + 1] & 255) << 8) |
                      ((outBuf[sourceBase + 2] & 255) << 16) | ((outBuf[sourceBase + 3] & 255) << 24);
        if (targetType == 0x003C || targetType == 0x001C || targetType == 0x00BC) {
            sectionBase = layout[rofIdx].codeBase;
            sectionSize = r->cod;
            relative = (targetType & 0x80) != 0;
        } else if (targetType == 0x0039 || targetType == 0x0019 || targetType == 0x00B9) {
            sectionBase = totalCode + layout[rofIdx].idatBase;
            sectionSize = r->idat;
            relative = (targetType & 0x80) != 0;
        } else if (targetType == 0x003A || targetType == 0x001A || targetType == 0x00BA) {
            sectionBase = totalCode + totalData + layout[rofIdx].statBase;
            sectionSize = r->stat;
            relative = (targetType & 0x80) != 0;
        } else {
            fatal("lokaler PIC-/unbekannter Relokationstyp wird nicht unterstuetzt");
        }
        if (relative) {
            if (!inCode || site < 3)
                fatal("lokale PIC-Relokation hat ungueltigen Code-Ort");
            targetValue -= 7; /* QRX86 stores the original local value plus its call/pop bias. */
            if (targetValue < 0 || targetValue > sectionSize)
                fatal("lokales PIC-Relokationsziel ausserhalb seiner Sektion");
            patchLeDword(sourceBase, sectionBase + targetValue -
                         (layout[rofIdx].codeBase + site - 3));
        } else {
            if (targetValue < 0 || targetValue > sectionSize)
                fatal("lokales Relokationsziel ausserhalb seiner Sektion");
            patchLeDword(sourceBase, sectionBase + targetValue);
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

static void outLeWord(int v)
{
    outByte(v & 255);
    outByte((v >> 8) & 255);
}

static void outLeDword(int v)
{
    outByte(v & 255);
    outByte((v >> 8) & 255);
    outByte((v >> 16) & 255);
    outByte((v >> 24) & 255);
}

static void patchLeWord(int at, int value)
{
    if (at < 0 || at > outLen - 2)
        fatal("Modulkopf-Patch ausserhalb der Ausgabe");
    outBuf[at] = value & 255;
    outBuf[at + 1] = (value >> 8) & 255;
}

static void patchLeDword(int at, int value)
{
    if (at < 0 || at > outLen - 4)
        fatal("Modulkopf-Patch ausserhalb der Ausgabe");
    outBuf[at] = value & 255;
    outBuf[at + 1] = (value >> 8) & 255;
    outBuf[at + 2] = (value >> 16) & 255;
    outBuf[at + 3] = (value >> 24) & 255;
}

static int moduleHeaderParity(void)
{
    int at, parity;

    parity = 0;
    for (at = 0; at < 0x56; at += 2)
        parity ^= (outBuf[at] & 255) | ((outBuf[at + 1] & 255) << 8);
    return (~parity) & 0xFFFF;
}

static int moduleCrc(int len)
{
    int crc, i, bit;

    crc = 0xFFFFFF;
    for (i = 0; i < len; i++) {
        crc ^= (outBuf[i] & 255) << 16;
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x800000)
                crc = ((crc << 1) ^ 0x800063) & 0xFFFFFF;
            else
                crc = (crc << 1) & 0xFFFFFF;
        }
    }
    return crc ^ 0xFFFFFF;
}

static void writeCodeSections(void);
static void writeDataSections(void);

/* Emit the OS-9000 container only for flat QCC inputs with no unresolved
   symbol/relocation records. This does not claim general l386 compatibility. */
static void writeModuleCandidate(const char *name, int codeSize, int dataSize,
                                 int statSize, int stackSize, int entry,
                                 int trap, int typeLanguage, int attrRevision,
                                 int edition)
{
    int nameAt, codeAt, idataAt, idrefAt, parity, crc, i;
    int moduleType, dataBias, totalDataMemory;

    if (dataSize < 0 || statSize < 0 || dataSize > 0x7FFFFFFF - statSize)
        fatal("Gesamtgroesse des Moduldatenbereichs ist zu gross");
    totalDataMemory = dataSize + statSize;

    moduleType = (typeLanguage >> 8) & 255;
    dataBias = 0x80;
    if (moduleType == 4 || moduleType == 12 || moduleType == 13 ||
        moduleType == 14 || moduleType == 15)
        dataBias = 0;

    outLeWord(0x4AFC);          /* x86 module sync */
    outLeWord(2);               /* system revision */
    outLeDword(0);               /* total size, patched below */
    outLeDword(1);               /* l386 fallback if GRPUSER is unavailable */
    outLeDword(0x58);            /* module name offset */
    outLeWord(0x0555);           /* owner/group/world read and execute */
    outLeWord(typeLanguage);
    outLeWord(attrRevision);
    outLeWord(edition);
    outLeDword(0);               /* hardware requirements */
    outLeDword(0);               /* shared data offset */
    outLeDword(0);               /* symbol table offset */
    outLeDword(0);               /* execution entry, patched below */
    outLeDword(0);               /* no exception entry unless ROF supplies one */
    outLeDword(totalDataMemory);
    outLeDword(stackSize);
    outLeDword(0);               /* initialized-data descriptor offset */
    outLeDword(0);               /* relocation-list offset */
    outLeDword(0);               /* init routine */
    outLeDword(0);               /* termination routine */
    outLeDword(dataBias);        /* target-specific symbol-bias convention */
    outLeDword(0);               /* code bias */
    outLeWord(0);                /* ident */
    for (i = 0; i < 8; i++)
        outByte(0);              /* reserved */
    outLeWord(0);                /* header parity */
    if (outLen != 0x58)
        fatal("interner x86-Modulkopf hat falsche Groesse");

    nameAt = outLen;
    for (i = 0; name[i] != 0; i++)
        outByte(name[i]);
    outByte(0);
    while (outLen & 7)
        outByte(0);
    codeAt = outLen;
    if (entry < 0 || entry >= codeSize)
        fatal("Programmeinstieg liegt ausserhalb des Codebereichs");
    if (trap != -1 && (trap < 0 || trap >= codeSize))
        fatal("Trap-Einstieg liegt ausserhalb des Codebereichs");
    writeCodeSections();
    while (outLen & 3)
        outByte(0);
    idataAt = outLen;
    outLeDword(statSize);        /* zero-fill range */
    outLeDword(dataSize);        /* initialized-data range */
    writeDataSections();
    idrefAt = outLen;
    outLeWord(0);                /* first list: high word */
    outLeWord(0);                /* first list: terminating count */
    outLeWord(0);                /* second list: high word */
    outLeWord(0);                /* second list: terminating count */
    if ((outLen + 3) & 1)
        outByte(0);

    patchLeDword(0x04, outLen + 3);
    patchLeDword(0x0C, nameAt);
    patchLeDword(0x24, codeAt + entry);
    if (trap != -1)
        patchLeDword(0x28, codeAt + trap);
    patchLeDword(0x2C, totalDataMemory);
    patchLeDword(0x30, stackSize);
    patchLeDword(0x34, idataAt);
    patchLeDword(0x38, idrefAt);
    parity = moduleHeaderParity();
    patchLeWord(0x56, parity);
    crc = moduleCrc(outLen);
    outByte(crc >> 16);
    outByte(crc >> 8);
    outByte(crc);
    printf("OS-9000 x86 module candidate: header=88 code@%d idata@%d idref@%d size=%d\n",
           codeAt, idataAt, idrefAt, outLen);
}

static int parseAlignmentOption(const char *arg)
{
    if (strEq(arg, "-b=2"))
        return 2;
    if (strEq(arg, "-b=4"))
        return 4;
    if (strEq(arg, "-b=8"))
        return 8;
    if (strEq(arg, "-b=16"))
        return 16;
    fatal("-b muss 2, 4, 8 oder 16 sein");
    return 2;
}

/* Write the private prototype header. This is not an OS-9000 module header. */
static void writePrototypeHeader(int codeSize, int dataSize, int statSize)
{
    printf("Writing private QCC prototype header (not OS-9000)...\n");

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

/* Validate a library reference record against its owning PSECT. */
static void validateLibraryReferenceData(int psect, int dataAt, int count,
                                         unsigned long *flagCounts)
{
    int i;

    for (i = 0; i < count; i++) {
        int at = dataAt + i * 6;
        int flags = le16(at);
        int offset = le32(at + 2);
        int widthCode = (flags >> 3) & 3;
        long width, limit;

        if (widthCode == 1)
            width = 1;
        else if (widthCode == 2)
            width = 2;
        else if (widthCode == 3)
            width = 4;
        else
            fatal("ROF-Bibliotheksreferenz hat ungueltige Feldbreite");
        if (flags & 0x20)
            limit = (unsigned long)le32(psect + 18);
        else if (flags & 0x200)
            limit = (unsigned long)le32(psect + 10) +
                    (unsigned long)le32(psect + 14);
        else
            limit = (unsigned long)le32(psect + 2) +
                    (unsigned long)le32(psect + 6);
        if (offset < 0 || (long)offset > limit || width > limit - offset)
            fatal("ROF-Bibliotheksreferenz liegt ausserhalb ihres PSECT-Bereichs");
        flagCounts[flags]++;
    }
}

/* Local-reference tables start with a count, then six bytes per record. */
static int inspectLibraryLocalReferences(int psect, int localAt, int countSize,
                                         int strictBounds,
                                         unsigned long *flagCounts,
                                         int *outOfArea)
{
    int count, i, recordsAt;

    if (localAt < 0 || localAt > inLen - countSize)
        fatal("Local-Reference-Tabelle ausserhalb der Bibliotheksdatei");
    count = countSize == 2 ? le16(localAt) : le32(localAt);
    if (count < 0 || count > (inLen - localAt - countSize) / 6)
        fatal("Local-Reference-Tabelle unvollstaendig oder zu gross");
    recordsAt = localAt + countSize;
    for (i = 0; i < count; i++) {
        int at = recordsAt + i * 6;
        int flags = le16(at);
        int offset = le32(at + 2);
        int widthCode = (flags >> 3) & 3;
        long width, limit;

        if (widthCode == 1)
            width = 1;
        else if (widthCode == 2)
            width = 2;
        else if (widthCode == 3)
            width = 4;
        else
            fatal("ROF-Local-Referenz hat ungueltige Feldbreite");
        if (flags & 0x20)
            limit = (unsigned long)le32(psect + 18);
        else if (flags & 0x200)
            limit = (unsigned long)le32(psect + 10) +
                    (unsigned long)le32(psect + 14);
        else
            limit = (unsigned long)le32(psect + 2) +
                    (unsigned long)le32(psect + 6);
        if (offset < 0 || (long)offset > limit || width > limit - offset) {
            (*outOfArea)++;
            if (strictBounds)
                fatal("ROF-Local-Referenz liegt ausserhalb ihres PSECT-Bereichs");
        }
        flagCounts[flags]++;
    }
    return count;
}

/* Inspect the fixed type-1 library index header documented for ROF 9.x. */
static void inspectLibrary(int listSymbols)
{
    enum { LIB_GLOBAL_ENTRY_SIZE = 22 };
    static unsigned long flagCounts[65536];
    static unsigned long localFlagCounts[65536];
    static const char *sectionNames[] = {
        "global-definition hash", "global definitions", "strings",
        "psects", "internal references", "external references"
    };
    int sizes[6];
    int offsets[6];
    int at, i, end, headerSize, psectEntrySize, refEntrySize;
    int psectCodeAt, psectLocalAt, psectInternalHead, psectExternalHead;
    int psectNameAt, psectGlobalHead, refCountSize, refAtOffset, refNextOffset;
    int refTargetOffset, nameLen, psectN, globalN, internalN, externalN, hashN;
    int refMax, localOutOfArea, localSites, hashMismatches;
    int *globalBucket;
    unsigned char *seenReference;
    unsigned int ident;
    int format, edition, nameOffset;

    if (inLen < 42)
        fatal("Bibliothek zu kurz fuer den Typ-1-Kopf");
    memset(flagCounts, 0, sizeof(flagCounts));
    memset(localFlagCounts, 0, sizeof(localFlagCounts));
    hashMismatches = 0;
    localOutOfArea = 0;
    localSites = 0;

    ident = ((unsigned int)(inBuf[0] & 255) << 24) |
            ((unsigned int)(inBuf[1] & 255) << 16) |
            ((unsigned int)(inBuf[2] & 255) << 8) |
            (unsigned int)(inBuf[3] & 255);
    format = le16(4);
    edition = le16(12);
    nameOffset = le32(14);
    for (i = 0; i < 6; i++)
        sizes[i] = le32(18 + i * 4);

    if (format != 1 && format != 0xff01)
        fatal("nur ROF-Bibliotheksformat Typ 1 und dessen beobachtete $FF01-Variante werden erkannt");
    headerSize = format == 1 ? 42 : 46;
    psectEntrySize = format == 1 ? 62 : 66;
    refEntrySize = format == 1 ? 14 : 16;
    psectCodeAt = format == 1 ? 30 : 34;
    psectLocalAt = format == 1 ? 34 : 38;
    psectInternalHead = format == 1 ? 46 : 50;
    psectExternalHead = format == 1 ? 50 : 54;
    psectNameAt = format == 1 ? 54 : 58;
    psectGlobalHead = format == 1 ? 58 : 62;
    refCountSize = format == 1 ? 2 : 4;
    refAtOffset = refCountSize;
    refNextOffset = refCountSize + 4;
    refTargetOffset = refCountSize + 8;
    if ((sizes[0] & 3) != 0)
        fatal("Bibliotheks-Hash-Tabelle ist nicht durch vier teilbar");
    for (i = 0; i < 6; i++) {
        if (sizes[i] < 0)
            fatal("Bibliotheksabschnitt ist groesser als 2 GiB");
    }
    if (nameOffset < 0 || nameOffset >= sizes[2])
        fatal("Bibliotheksname ausserhalb der String-Tabelle");
    if (sizes[1] % LIB_GLOBAL_ENTRY_SIZE != 0 ||
        sizes[3] % psectEntrySize != 0 ||
        sizes[4] % refEntrySize != 0 ||
        sizes[5] % refEntrySize != 0)
        fatal("Typ-1-Bibliotheksindex hat unvollstaendige Tabelleneintraege");
    hashN = sizes[0] / 4;
    globalN = sizes[1] / LIB_GLOBAL_ENTRY_SIZE;
    psectN = sizes[3] / psectEntrySize;
    internalN = sizes[4] / refEntrySize;
    externalN = sizes[5] / refEntrySize;

    at = headerSize;
    for (i = 0; i < 6; i++) {
        offsets[i] = at;
        if (sizes[i] > inLen - at)
            fatal("Typ-1-Bibliothekskopf verweist ausserhalb der Datei");
        at += sizes[i];
    }
    end = at;
    nameLen = 0;
    while (nameOffset + nameLen < sizes[2] &&
           inBuf[offsets[2] + nameOffset + nameLen] != 0)
        nameLen++;
    if (nameLen == 0)
        fatal("Bibliotheksname ist leer oder nicht abgeschlossen");
    if (nameOffset + nameLen >= sizes[2])
        fatal("Bibliotheksname endet ausserhalb der String-Tabelle");

    printf("ROF library: format $%04X, edition %d, identifier $%08X\n",
           format, edition, ident);
    printf("File: %d bytes, fixed index header: %d bytes\n", inLen, headerSize);
    if (headerSize == 46)
        printf("Header expansion: $%08X\n", (unsigned int)le32(42));
    printf("Library name: %.*s\n", nameLen, &inBuf[offsets[2] + nameOffset]);
    for (i = 0; i < 6; i++)
        printf("  %-26s offset %d size %d\n", sectionNames[i], offsets[i], sizes[i]);
    printf("  ROF payload / remaining sections offset %d size %d\n", end, inLen - end);
    printf("PSECT index: %d record(s), %d bytes each\n", psectN, psectEntrySize);
    for (i = 0; i < psectN; i++) {
        int p = offsets[3] + i * psectEntrySize;
        int rofType = le16(p);
        int stat = le32(p + 2);
        int idat = le32(p + 6);
        int remoteStat = le32(p + 10);
        int remoteIdat = le32(p + 14);
        int code = le32(p + 18);
        int debug = le32(p + 22);
        int stack = le32(p + 26);
        int psectField1 = format == 0xff01 ? le16(p + 30) : -1;
        int psectField2 = format == 0xff01 ? le16(p + 32) : -1;
        int codeAt = le32(p + psectCodeAt);
        int localAt = le32(p + psectLocalAt);
        int internalHead = le32(p + psectInternalHead);
        int externalHead = le32(p + psectExternalHead);
        int psectName = le32(p + psectNameAt);
        int globalHead = le32(p + psectGlobalHead);
        int psectNameLen = 0;
        int localN;
        int payloadSize;

        if (stat < 0 || idat < 0 || remoteStat < 0 || remoteIdat < 0 ||
            code < 0 || debug < 0 || stack < 0)
            fatal("PSECT-Groesse ist groesser als 2 GiB");
        if (psectName < 0 || psectName >= sizes[2])
            fatal("PSECT-Name ausserhalb der String-Tabelle");
        while (psectName + psectNameLen < sizes[2] &&
               inBuf[offsets[2] + psectName + psectNameLen] != 0)
            psectNameLen++;
        if (psectNameLen == 0 || psectName + psectNameLen >= sizes[2])
            fatal("PSECT-Name leer oder nicht abgeschlossen");
        if (codeAt < 0 || localAt < 0 || localAt > inLen)
            fatal("ungueltiger Code- oder Local-Reference-Offset im PSECT");
        payloadSize = code;
        if (idat > 0x7fffffff - payloadSize ||
            remoteIdat > 0x7fffffff - payloadSize - idat ||
            debug > 0x7fffffff - payloadSize - idat - remoteIdat)
            fatal("PSECT-Payloadgroesse ist groesser als 2 GiB");
        payloadSize += idat + remoteIdat + debug;
        if (payloadSize > 0 &&
            (codeAt > inLen || payloadSize > inLen - codeAt))
            fatal("PSECT-Payloadbereiche ausserhalb der Bibliotheksdatei");
        if ((internalHead != -1 && internalHead >= internalN) ||
            (externalHead != -1 && externalHead >= externalN) ||
            (globalHead != -1 && globalHead >= globalN))
            fatal("PSECT-Listenanfang ausserhalb der Bibliothekstabellen");
        localN = inspectLibraryLocalReferences(p, localAt, refCountSize,
                                               format != 1, localFlagCounts,
                                               &localOutOfArea);
        if (localN > 0x7fffffff - localSites)
            fatal("zu viele ROF-Local-References in einer Bibliothek");
        localSites += localN;

        printf("  [%d] ROF $%04X %-24.*s stat=%d init=%d rstat=%d rinit=%d "
               "code=%d debug=%d stack=%d",
               i, rofType, psectNameLen, &inBuf[offsets[2] + psectName],
               stat, idat, remoteStat, remoteIdat, code, debug, stack);
        if (format == 0xff01)
            printf(" field1=$%04X field2=$%04X", psectField1,
                   psectField2);
        printf(" code@%d localrefs@%d\n", codeAt, localAt);
    }
    if (listSymbols)
        printf("Global definitions: %d record(s), 22 bytes each\n", globalN);
    globalBucket = (int *)calloc(globalN > 0 ? globalN : 1, sizeof(int));
    if (!globalBucket)
        fatal("kein Speicher fuer Bibliotheks-Hash-Pruefung");
    for (i = 0; i < globalN; i++) {
        int g = offsets[1] + i * LIB_GLOBAL_ENTRY_SIZE;
        int type = le16(g);
        int value = le32(g + 2);
        int symbolName = le32(g + 6);
        int nextHash = le32(g + 10);
        int nextPsect = le32(g + 14);
        int psectIndex = le32(g + 18);
        int symbolNameLen = 0;

        if (symbolName < 0 || symbolName >= sizes[2])
            fatal("Globaler Symbolname ausserhalb der String-Tabelle");
        while (symbolName + symbolNameLen < sizes[2] &&
               inBuf[offsets[2] + symbolName + symbolNameLen] != 0)
            symbolNameLen++;
        if (symbolNameLen == 0 || symbolName + symbolNameLen >= sizes[2])
            fatal("Globaler Symbolname leer oder nicht abgeschlossen");
        if ((nextHash != -1 && nextHash >= globalN) ||
            (nextPsect != -1 && nextPsect >= globalN) ||
            (psectIndex != -1 && psectIndex >= psectN))
            fatal("Globaler Symbolverweis ausserhalb der Bibliothekstabellen");
        if (listSymbols)
            printf("  [%d] type=$%04X value=$%08X psect=%d %-.*s\n",
                   i, type, (unsigned int)value, psectIndex, symbolNameLen,
                   &inBuf[offsets[2] + symbolName]);
    }
    for (i = 0; i < hashN; i++) {
        int symbol = le32(offsets[0] + i * 4);

        while (symbol != -1) {
            int next;

            if (symbol < 0 || symbol >= globalN || globalBucket[symbol] != 0)
                fatal("ungueltige oder zyklische Bibliotheks-Hash-Kette");
            globalBucket[symbol] = i + 1;
            next = le32(offsets[1] + symbol * LIB_GLOBAL_ENTRY_SIZE + 10);
            symbol = next;
        }
    }
    for (i = 0; i < globalN; i++) {
        if (globalBucket[i] == 0)
            fatal("Globaldefinition fehlt in der Hash-Tabelle");
    }
    if (hashN == 378) {
        for (i = 0; i < globalN; i++) {
            int g = offsets[1] + i * LIB_GLOBAL_ENTRY_SIZE;
            int symbolName = le32(g + 6);
            int symbolNameLen = 0;
            unsigned int hash = 0;

            while (symbolName + symbolNameLen < sizes[2] &&
                   inBuf[offsets[2] + symbolName + symbolNameLen] != 0) {
                hash = (hash + (inBuf[offsets[2] + symbolName + symbolNameLen] & 255)) % 377;
                symbolNameLen++;
            }
            if (symbolNameLen == 0 || symbolName + symbolNameLen >= sizes[2])
                fatal("Globaler Symbolname endet ausserhalb der Hash-Pruefung");
            if ((int)hash != globalBucket[i] - 1)
                hashMismatches++;
        }
    }
    free(globalBucket);
    for (i = 0; i < internalN; i++) {
        int r = offsets[4] + i * refEntrySize;
        int refCount = refCountSize == 2 ? le16(r) : le32(r);
        int refAt = le32(r + refAtOffset);
        int next = le32(r + refNextOffset);
        int symbol = le32(r + refTargetOffset);

        if (refCount < 0 || (next != -1 && next >= internalN) ||
            (symbol != -1 && symbol >= globalN))
            fatal("interner Bibliotheksverweis ausserhalb des Indexes");
        if (refCount > 0 && (refAt < 0 || refAt > inLen ||
                            refCount > (inLen - refAt) / 6))
            fatal("interne Referenzdaten ausserhalb der Bibliotheksdatei");
    }
    for (i = 0; i < externalN; i++) {
        int r = offsets[5] + i * refEntrySize;
        int refCount = refCountSize == 2 ? le16(r) : le32(r);
        int refAt = le32(r + refAtOffset);
        int next = le32(r + refNextOffset);
        int symbolName = le32(r + refTargetOffset);
        int symbolNameLen = 0;

        if (refCount < 0 || (next != -1 && next >= externalN))
            fatal("externe Bibliotheksreferenzkette ausserhalb des Indexes");
        if (refCount > 0 && (refAt < 0 || refAt > inLen ||
                            refCount > (inLen - refAt) / 6))
            fatal("externe Referenzdaten ausserhalb der Bibliotheksdatei");
        if (symbolName < 0 || symbolName >= sizes[2])
            fatal("externer Symbolname ausserhalb der String-Tabelle");
        while (symbolName + symbolNameLen < sizes[2] &&
               inBuf[offsets[2] + symbolName + symbolNameLen] != 0)
            symbolNameLen++;
        if (symbolNameLen == 0 || symbolName + symbolNameLen >= sizes[2])
            fatal("externer Symbolname leer oder nicht abgeschlossen");
    }
    refMax = internalN > externalN ? internalN : externalN;
    seenReference = (unsigned char *)calloc(refMax > 0 ? refMax : 1, 1);
    if (!seenReference)
        fatal("kein Speicher fuer Bibliotheks-Referenzpruefung");
    for (i = 0; i < psectN; i++) {
        int p = offsets[3] + i * psectEntrySize;
        int ref = le32(p + psectInternalHead);

        while (ref != -1) {
            int record;

            if (ref < 0 || ref >= internalN || seenReference[ref])
                fatal("ungueltige oder zyklische interne PSECT-Referenzliste");
            seenReference[ref] = 1;
            record = offsets[4] + ref * refEntrySize;
            validateLibraryReferenceData(p, le32(record + refAtOffset),
                                         refCountSize == 2 ? le16(record) : le32(record),
                                         flagCounts);
            ref = le32(record + refNextOffset);
        }
    }
    for (i = 0; i < internalN; i++) {
        if (!seenReference[i])
            fatal("interner Referenzeintrag ist keinem PSECT zugeordnet");
    }
    memset(seenReference, 0, refMax > 0 ? refMax : 1);
    for (i = 0; i < psectN; i++) {
        int p = offsets[3] + i * psectEntrySize;
        int ref = le32(p + psectExternalHead);

        while (ref != -1) {
            int record;

            if (ref < 0 || ref >= externalN || seenReference[ref])
                fatal("ungueltige oder zyklische externe PSECT-Referenzliste");
            seenReference[ref] = 1;
            record = offsets[5] + ref * refEntrySize;
            validateLibraryReferenceData(p, le32(record + refAtOffset),
                                         refCountSize == 2 ? le16(record) : le32(record),
                                         flagCounts);
            ref = le32(record + refNextOffset);
        }
    }
    for (i = 0; i < externalN; i++) {
        if (!seenReference[i])
            fatal("externer Referenzeintrag ist keinem PSECT zugeordnet");
    }
    free(seenReference);
    printf("Library index: %d hash slots, %d definitions, %d internal and %d external reference entries\n",
           hashN, globalN, internalN, externalN);
    if (hashN == 378 && hashMismatches == 0)
        printf("Global-name hash: byte sum modulo 377 matches all %d definitions\n",
               globalN);
    else if (hashN == 378)
        printf("Global-name hash candidate (byte sum modulo 377): %d/%d definitions match\n",
               globalN - hashMismatches, globalN);
    else
        printf("Global-name hash: not checked (observed rule applies to 378-slot tables)\n");
    printf("Reference sites by flags:");
    for (i = 0; i < 65536; i++) {
        if (flagCounts[i] != 0)
            printf(" $%04X=%lu", i, flagCounts[i]);
    }
    printf("\n");
    printf("Local reference sites: %d", localSites);
    for (i = 0; i < 65536; i++) {
        if (localFlagCounts[i] != 0)
            printf(" $%04X=%lu", i, localFlagCounts[i]);
    }
    printf("\n");
    if (localOutOfArea != 0)
        printf("Legacy local-reference locations beyond decoded PSECT sections: %d\n",
               localOutOfArea);
    printf("Note: reference sites were decoded; complete ROF objects were not reconstructed.\n");
}

/* Write code sections */
static void writeCodeSections(void)
{
    int i, at, startLen, len;

    startLen = outLen;

    for (i = 0; i < rofN; i++) {
        at = rof[i].codeAt;
        len = rof[i].cod;
        while (len > 0) {
            outByte(inBuf[at] & 255);
            at++;
            len--;
        }
        len = layout[i].codeSize - rof[i].cod;
        while (len > 0) {
            outByte(0);
            len--;
        }
    }
    printf("Wrote %d bytes of code\n", outLen - startLen);
}

/* Write data sections */
static void writeDataSections(void)
{
    int startLen = outLen;
    int i, at, len;

    for (i = 0; i < rofN; i++) {
        at = rof[i].idatAt;
        len = rof[i].idat;
        while (len > 0) {
            outByte(inBuf[at] & 255);
            at++;
            len--;
        }
        len = layout[i].idatSize - rof[i].idat;
        while (len > 0) {
            outByte(0);
            len--;
        }
    }
    printf("Wrote %d bytes of initialized data\n", outLen - startLen);
}
int main(int argc, char *argv[])
{
    const char *inFile = NULL;
    const char *outFile = NULL;
    char moduleName[256];
    int i, inspect = 0, inspectLib = 0, listLibSymbols = 0;
    int prototypeOutput = 0, moduleOutput = 0, nextAt, profile;
    int linkAlignment = 2, alignmentSet = 0;

    printf("qlx86 ROF reader / linker prototype\n");

    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (strEq(argv[i], "--inspect"))
            inspect = 1;
        else if (strEq(argv[i], "--inspect-library"))
            inspectLib = 1;
        else if (strEq(argv[i], "--library-symbols"))
            listLibSymbols = 1;
        else if (strEq(argv[i], "--emit-prototype"))
            prototypeOutput = 1;
        else if (strEq(argv[i], "--emit-module-candidate"))
            moduleOutput = 1;
        else if (argv[i][0] == '-' && argv[i][1] == 'b' && argv[i][2] == '=') {
            linkAlignment = parseAlignmentOption(argv[i]);
            alignmentSet = 1;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'O' && argv[i][2] == '=')
            outFile = &argv[i][3];
        else if (!inFile)
            inFile = argv[i];
        else
            fatal("mehr als eine Eingabedatei angegeben");
    }

    if (!inFile || (inspect && (inspectLib || listLibSymbols || outFile || prototypeOutput || moduleOutput)) ||
        (inspectLib && (inspect || outFile || prototypeOutput || moduleOutput)) ||
        (listLibSymbols && !inspectLib) ||
        ((prototypeOutput || moduleOutput) && !outFile) ||
        (prototypeOutput && moduleOutput) ||
        (alignmentSet && !moduleOutput)) {
        printf("Syntax: qlx86 --inspect <input.r> | --inspect-library [--library-symbols] <input.l> |\n"
               "        qlx86 --emit-prototype <input.r> -O=<file> |\n"
               "        qlx86 --emit-module-candidate [-b=2|4|8|16] <input.r> -O=<file>\n");
        return 1;
    }

    /* Read input */
    inLen = readFile(inFile);
    if (inLen < 0)
        return 1;

    printf("Input: %d bytes\n", inLen);

    if (inspectLib) {
        inspectLibrary(listLibSymbols);
        return 0;
    }

    /* A ROF file may contain concatenated PSECTs. Require exact coverage. */
    profile = -1;
    nextAt = 0;
    while (nextAt < inLen) {
        nextAt = rofParse(nextAt);
        if (profile == -1)
            profile = rof[rofN - 1].profile;
        else if (profile != rof[rofN - 1].profile)
            fatal("gemischte ROF-Profile in einer Eingabedatei");
    }
    if (nextAt != inLen || rofN == 0)
        fatal("ROF-Datei nicht vollstaendig geparst");

    if (inspect) {
        printf("Inspect: %d PSECT(s), Profil: %s\n", rofN,
               profile == ROF_PROFILE_QCC ? "QCC-Prototyp" :
               profile == ROF_PROFILE_MWOS_16 ? "Microware altes 16-bit-count Image-Profil" :
               "Microware ROF Edition 9.2 (Series $E9)");
        return 0;
    }
    if (!prototypeOutput && !moduleOutput)
        fatal("Modulausgabe ist auf explizite Diagnoseoptionen beschraenkt");
    if (profile != ROF_PROFILE_QCC)
        fatal("Microware-ROF erkannt; Linken/Relokationen dieses Profils sind noch nicht implementiert");

    if (rofRoot == -1)
        fatal("kein Root PSECT gefunden");

    if (moduleOutput) {
        /* Multi-PSECT candidates remain restricted to flat, relocation-free
           QCC inputs; Microware ROF link semantics are not implemented. */
        for (i = 0; i < rofN; i++) {
            if (rof[i].globN != 0 || rof[i].extRefN != 0 || rof[i].localN != 0 ||
                rof[i].rem != 0 || rof[i].remIdat != 0 || rof[i].debug != 0)
                fatal("Modulkandidat verlangt keine Symbole, Referenzen, Remote-Daten oder Debugdaten");
        }
        if (rof[rofRoot].entry < 0)
            fatal("Modulkandidat braucht einen gesetzten Programmeinstieg");
        if (rof[rofRoot].entry >= rof[rofRoot].cod)
            fatal("Programmeinstieg liegt ausserhalb des Root-PSECT-Codebereichs");
        if (rof[rofRoot].trap != -1 &&
            (rof[rofRoot].trap < 0 || rof[rofRoot].trap >= rof[rofRoot].cod))
            fatal("Trap-Einstieg liegt ausserhalb des Root-PSECT-Codebereichs");
    }

    /* Load globals from every PSECT. */
    {
        int ri;
        for (ri = 0; ri < rofN; ri++) {
            RofInfo *obj = &rof[ri];
            int at = obj->globAt;
            int gi;
            for (gi = 0; gi < obj->globN; gi++) {
                const char *name = (const char *)&inBuf[at];
                int type = profile == ROF_PROFILE_QCC ? be16(at + strLen(name) + 1) :
                                                        le16(at + strLen(name) + 1);
                int value = readSize(at + strLen(name) + 3, profile);
                if (symFind(name) >= 0)
                    fatal("doppelte globale Definition im QCC-Eingabestrom");
                symAdd(name, value, type, ri);
                at = skipName(at) + 6;
            }
        }
    }

    printf("Symbols loaded: %d\n\n", symN);

    /* Build memory layout */
    buildLayout(moduleOutput ? linkAlignment : 1);
    printf("\n");

    /* Calculate totals */
    int totalCode = 0, totalData = 0, totalStat = 0;
    for (i = 0; i < rofN; i++) {
        totalCode = addLayoutSize(totalCode, layout[i].codeSize);
        totalData = addLayoutSize(totalData, layout[i].idatSize);
        totalStat = addLayoutSize(totalStat, layout[i].statSize);
    }

    if (moduleOutput) {
        int stackSize = 0;
        const char *base = inFile;
        const char *p = inFile;
        int n = 0, entry;

        while (*p != 0) {
            if (*p == '/' || *p == '\\')
                base = p + 1;
            p++;
        }
        while (base[n] != 0 && base[n] != '.' && n < 255) {
            moduleName[n] = base[n];
            n++;
        }
        moduleName[n] = 0;
        if (n == 0)
            fatal("Modulname aus Eingabepfad ist leer");
        for (i = 0; i < rofN; i++) {
            if (rof[i].stk < 0 || rof[i].stk > 0x7FFFFFFF - stackSize)
                fatal("Stackgroesse der PSECTs ist ungueltig oder zu gross");
            stackSize = stackSize + rof[i].stk;
        }
        if ((totalData & 3) != 0)
            fatal("Modulkandidat braucht auf vier Bytes ausgerichtete Initialdaten");
        entry = layout[rofRoot].codeBase + rof[rofRoot].entry;
        if (rof[rofRoot].trap != -1 &&
            layout[rofRoot].codeBase > 0x7FFFFFFF - rof[rofRoot].trap)
            fatal("Trap-Einstieg ist zu gross");
        writeModuleCandidate(moduleName, totalCode, totalData, totalStat,
                             stackSize, entry,
                             rof[rofRoot].trap == -1 ? -1 :
                                 layout[rofRoot].codeBase + rof[rofRoot].trap,
                             rof[rofRoot].tylan, rof[rofRoot].attrev,
                             rof[rofRoot].edition);
        printf("\nModulkandidat: %d bytes, einschliesslich CRC\n", outLen);
    } else {
        writePrototypeHeader(totalCode, totalData, totalStat);
        outLen = 56;
        writeCodeSections();
        writeDataSections();
        printf("\nApplying relocations...\n");
        for (i = 0; i < rofN; i++)
            applyRelocations(i, totalCode, totalData);
        printf("\nPrototype output: %d bytes\n", outLen);
    }

    /* Write module */
    if (writeFile(outFile, outLen) < 0)
        return 1;

    printf("Written to: %s\n", outFile);
    if (moduleOutput)
        printf("Candidate file written; verify before using with OS-9000\n");
    else
        printf("Prototype-Datei geschrieben; kein OS-9000-Modul\n");

    return 0;
}
