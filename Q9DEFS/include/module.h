/* module.h -- module header structures, type/language/attribute codes and
 * module calls.
 *
 * Derived from the specification, no template used.
 * Status: MT_PROGRAM, MT_DATA, ML_OBJECT and the _os_* prototypes are
 * belegt; everything else is derived (member names follow the analogy
 * of the documented header field names, values from the module format
 * description). Offsets are byte offsets from the start of the module;
 * the target uses 2-byte maximum alignment, so no padding is inserted
 * except by the documented offsets. Members that have no documented C
 * name are covered by unnamed filler.
 */
#ifndef Q9_MODULE_H
#define Q9_MODULE_H

#include <types.h>

/* ---- codes ------------------------------------------------------------- */

#define MODSYNC     0x4AFC   /* derived: sync bytes in the module header */

/* attribute byte */
#define MA_REENT    0x80     /* derived: bit 7, reentrant */
#define MA_SUPER    0x20     /* derived: bit 5, system state module */

/* module type (one byte) */
#define MT_ANY      0        /* derived: unused / wildcard */
#define MT_PROGRAM  1        /* program module */
#define MT_SUBROUT  2        /* derived: subroutine module */
#define MT_MULTI    3        /* derived: multi module */
#define MT_DATA     4        /* data module */
#define MT_CSDDATA  5        /* derived: configuration status descriptor data */
#define MT_TRAPLIB  11       /* derived: user trap library */
#define MT_SYSTEM   12       /* derived: system module */
#define MT_FILEMAN  13       /* derived: file manager */
#define MT_DEVDRVR  14       /* derived: device driver */
#define MT_DEVDESC  15       /* derived: device descriptor */

/* module language (one byte) */
#define ML_ANY      0        /* derived: any language (wildcard) */
#define ML_OBJECT   1        /* object code (machine language) */
#define ML_ICODE    2        /* derived: Basic I-code */
#define ML_PCODE    3        /* derived: Pascal P-code */
#define ML_CCODE    4        /* derived: C I-code */
#define ML_CBLCODE  5        /* derived: Cobol I-code */
#define ML_FRTNCODE 6        /* derived: Fortran I-code */

/* ---- standard module header (48 bytes) ---------------------------------- */

/* derived: members of the common header, offsets 0..47 */
#define Q9_MODHEAD_MEMBERS \
	u_int16       _msync;        /* @0  sync bytes */ \
	u_int16       _msysrev;      /* @2  format revision */ \
	u_int32       _msize;        /* @4  total size including header and CRC */ \
	u_int32       _mowner;       /* @8  owner */ \
	u_int32       _mname;        /* @12 offset of the NUL-terminated name */ \
	u_int16       _maccess;      /* @16 access permissions (4 bits each) */ \
	u_int16       _mtylan;       /* @18 type (high byte) and language (low byte) */ \
	u_int16       _mattrev;      /* @20 attributes (high byte) and revision (low byte) */ \
	u_int16       _medit;        /* @22 edition */ \
	unsigned char _mrsv24[24]    /* @24 .. @47, members without a documented C name */

/* derived: name of the struct follows the documented header name */
struct modhcom {
	Q9_MODHEAD_MEMBERS;
};

typedef struct modhcom mh_com;

/* ---- type specific headers ---------------------------------------------- */

/* derived: header of program-type modules, standard header embedded */
typedef struct {
	mh_com  _mh;             /* @0  */
	u_int32 _mexec;          /* @48 execution offset */
	u_int32 _mexcpt;         /* @52 exception offset */
	u_int32 _mdata;          /* @56 required data area size */
	u_int32 _mstack;         /* @60 minimum stack size */
	u_int32 _midata;         /* @64 offset of initialized data */
	u_int32 _midref;         /* @68 offset of initialized pointer references */
} mod_exec;

/* derived: same members as mod_exec, standard header members inline */
typedef struct {
	Q9_MODHEAD_MEMBERS;
	u_int32 _mexec;          /* @48 */
	u_int32 _mexcpt;         /* @52 */
	u_int32 _mdata;          /* @56 */
	u_int32 _mstack;         /* @60 */
	u_int32 _midata;         /* @64 */
	u_int32 _midref;         /* @68 */
} mh_exec;

/* derived: file manager header */
typedef struct {
	Q9_MODHEAD_MEMBERS;
	u_int32 _mexec;          /* @48 offset of the entry table */
	u_int32 _mexcpt;         /* @52 offset of the default trap entry */
} mh_fman;

/* derived: driver header. The seven routine offsets are documented as a
   table of words (init, read, write, getstat, setstat, term, error) that
   lies behind the header, starting at the offset _mexec; the members are
   placed after the fixed header in that order. */
typedef struct {
	Q9_MODHEAD_MEMBERS;
	u_int32 _mexec;          /* @48 offset of the entry table */
	u_int32 _mexcpt;         /* @52 offset of the default trap entry */
	u_int32 _mdata;          /* @56 size of the static data area */
	u_int16 _mdinit;         /* entry table word 0 (init) */
	u_int16 _mdread;         /* entry table word 1 (read) */
	u_int16 _mdwrite;        /* entry table word 2 (write) */
	u_int16 _mdgetstat;      /* entry table word 3 (getstat) */
	u_int16 _mdsetstt;       /* entry table word 4 (setstat) */
	u_int16 _mdterm;         /* entry table word 5 (term) */
	u_int16 _mderror;        /* entry table word 6 (error handler, 0 = none) */
} mod_driver;

/* derived: device descriptor header */
typedef struct {
	Q9_MODHEAD_MEMBERS;
	u_int32       _mport;        /* @48 port address */
	unsigned char _mvector;      /* @52 interrupt vector */
	unsigned char _mirqlvl;      /* @53 interrupt level */
	unsigned char _mpriority;    /* @54 polling priority */
	unsigned char _mmode;        /* @55 mode capabilities */
	u_int16       _mfmgr;        /* @56 offset of the file manager name */
	u_int16       _mpdev;        /* @58 offset of the driver name */
	u_int16       _mdevcon;      /* @60 offset of the configuration table */
	unsigned char _mrsv62[8];    /* @62 .. @69, reserved / device flags */
	u_int16       _mopt;         /* @70 size of the initialization table */
	unsigned char _mdtype;       /* @72 device class (first byte of the init table) */
} mod_dev;

/* derived: configuration module header; widths from the configuration
   listing, offsets from the field table */
typedef struct {
	Q9_MODHEAD_MEMBERS;
	unsigned char _mrsv48[8];    /* @48 .. @55 */
	u_int16       _mprocs;       /* @56 initial process table size */
	u_int16       _mpaths;       /* @58 initial path table size */
	unsigned char _mrsv60[2];    /* @60 */
	u_int16       _msysgo;       /* @62 offset of first module name */
	u_int16       _msysdrive;    /* @64 offset of default device name */
	u_int16       _mconsol;      /* @66 offset of console path name */
	unsigned char _mrsv68[2];    /* @68 */
	u_int16       _mclock;       /* @70 offset of clock module name */
	u_int16       _mslice;       /* @72 ticks per time slice */
	unsigned char _mrsv74[8];    /* @74 .. @81 */
	u_int32       _mcputyp;      /* @82 CPU type */
	unsigned char _mrsv86[6];    /* @86 .. @91 */
	u_int16       _msyspri;      /* @92 initial priority */
	unsigned char _mrsv94[4];    /* @94 .. @97 */
	u_int16       _mmdirsz;      /* @98 module directory entries */
	unsigned char _mrsv100[8];   /* @100 .. @107 */
	u_int16       _mstacksz;     /* @108 IRQ stack size in longwords */
	unsigned char _mrsv110[8];   /* @110 .. @117 */
	u_int16       _mioman;       /* @118 offset of the I/O manager name list */
	unsigned char _mrsv120[2];   /* @120 */
	u_int16       _msysconf;     /* @122 system configuration flags */
} mod_config;

/* ---- calls --------------------------------------------------------------- */

extern error_code _os_link(char **mod_name, mh_com **mod_head, void **mod_entry,
                           u_int16 *type_lang, u_int16 *attr_rev);
extern error_code _os_load(const char *mod_name, mh_com **mod_head, void **mod_entry,
                           u_int32 mode, u_int16 *type_lang, u_int16 *attr_rev,
                           u_int32 color);
extern error_code _os_get_moddir(void *buffer, u_int32 *count);
extern error_code _os_setcrc(mh_com *mod_head);

#endif /* Q9_MODULE_H */

/* not defined (no documented value):
 *   mod_dir / struct mod_dir (module directory entry) and its members
 *   md_group, md_link, md_mchk, md_mptr, md_static
 *   MA_GHOST
 *   ML_JAVACODE
 */
