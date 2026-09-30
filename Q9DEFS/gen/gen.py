import re, sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from calls import CALLS, IOCALLS
from errs import ERR_EN
D = os.environ.get('Q9DEFS_SPEC_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'spec')) + '/'  # SPEC_DEFS.md / NUMS.md, not part of this repo
def q(n):
    for a, b in (('F$','Q$'),('I$','IQ$'),('E$','EQ$'),('M$','QM$'),('Ev$','QEv$'),('A$','QA$'),
                 ('S$','QS$'),('SS_','QSS_'),('P$','QP$'),('D_','QD_')):
        if n.startswith(a): return b + n[len(a):]
    return 'Q' + n
out = []; stats = {}; cur = [None]; W16 = [False]
def sec(title, text):
    out.append('*'); out.append('* ' + '=' * 70); out.append('* SECTION: ' + title)
    for l in text.strip().splitlines(): out.append('*   ' + l)
    out.append('* ' + '=' * 70); out.append('*')
    cur[0] = title; stats.setdefault(title, 0)
def grp(t):
    out.append('*'); out.append('* -- ' + t)
def ln(name, val, cm, raw=False):
    nm = name if raw else q(name)
    v = val if isinstance(val, str) else ('$%04x' % val if W16[0] else '$%02x' % val if val < 256 else ('$%04x' % val if val < 65536 else '$%08x' % val))
    out.append('%-16s equ     %-10s * %s' % (nm, v, cm)); stats[cur[0]] += 1
def novalue(title, names):
    out.append('*'); out.append('* Names without a value (the manual gives none, no number table entry): ' + title)
    txt = ' '.join(q(n) for n in names)
    import textwrap
    for l in textwrap.wrap(txt, 72): out.append('*   ' + l)

# ---------------- header
out += ["""*
* q9sys.d -- system definitions for the q9 system (numbers, offsets, codes)
*
* Origin: derived from SPEC_DEFS.md (tables extracted from the vendor
* documentation: names, values, meaning) and NUMS.md (call numbers from the
* public name table of an open-source disassembler (GPLv3), numbers only).
* No template file was used.  Values that the documentation does not state
* directly but that follow from listings or bit numbers are marked
* "(inferred)".  Names for which no value is known are NOT defined; they are
* listed as comments at the end of the respective section.
*
* Naming scheme (mechanical, from the names used in the documentation):
*   F$Xxx  -> Q$Xxx    kernel calls           I$Xxx  -> IQ$Xxx   I/O calls
*   E$Xxx  -> EQ$Xxx   error codes (16 bit)   M$Xxx  -> QM$Xxx   module header fields
*   Ev$Xxx -> QEv$Xxx  event functions        A$Xxx  -> QA$Xxx   alarm functions
*   S$Xxx  -> QS$Xxx   signals                SS_Xxx -> QSS_Xxx  status codes
*   P$Xxx  -> QP$Xxx   process fields         D_Xxx  -> QD_Xxx   system globals
*   every other name gets a leading 'Q' (module types, languages, mode bits,
*   PD_/DD_/V_/FD_/DT_ fields, ...): Prgrm -> QPrgrm, PD_DTP -> QPD_DTP.
*
* Conventions: offsets are in bytes; error codes are 16 bit
* (main number * 256 + sub number); call numbers are the request codes.
* Names that contain the vendor's product number were renamed (QM$OSLvl,
* QM$OSRev, Q$OSCall) to keep this file free of product names.
*
* Name decisions (collisions and spelling variants):
*   - The documentation has E$NoPlay (audio, $0609) and E$NOPLAY (ISDN, $0822), which differ
*     only in letter case; the second one is defined as EQ$NOPLAY_ISDN.
*   - E$PGM+TBLBSY (plus sign in the documentation) is defined as EQ$PGM_TBLBSY.
*   - Two spellings of one thing in the documentation are defined once and
*     the variant is an alias (equ to the first name), e.g. QPrgm/QPrgrm.
*   - S$Abort/S$Intrpt/S$HangUp appear in the documentation's error table
*     with values 2/3/4; they are signals and are defined only in the
*     signal section.
*   - Structures that overlay each other (module header extras, device
*     descriptor, init module; per-manager path descriptor options) reuse
*     the same offsets on purpose; names are unique, offsets are unique
*     only inside one structure.
*"""]

# ---------------- 1 calls
nums = {}
for l in open(D + 'NUMS.md'):
    m = re.match(r'\| (\S+) \| \$([0-9a-f]+) \|', l)
    if m: nums[m.group(1)] = int(m.group(2), 16)
sec('1. SYSTEM CALLS (kernel)', 'Request codes for the kernel calls, issued as a trap followed by a code word.\nInput/output registers are documented per call in the manual, not repeated here.')
for l in CALLS:
    n, c = l.split('|'); n = 'F$' + n
    ln(n, nums.pop(n), c)
novalue('kernel calls', ['F$Attach','F$FIRQ','F$Mbuf','F$OSCall','F$Sema','F$Service','F$SigReset','F$Sigmask','F$SysTrap'])
# fix names with product number in listing
# ---------------- 2 I/O
sec('2. I/O CALLS', 'Request codes for the I/O calls (handled by the I/O manager layer).')
for l in IOCALLS:
    n, c = l.split('|'); n = 'I$' + n
    ln(n, nums.pop(n), c)
assert not nums, nums
# ---------------- 3 events
sec('3. EVENT AND ALARM CODES', 'Sub-function codes of the event call, and the alarm function names.')
grp('event functions (d1.w of the event call)')
for n, v, c in [('Link',0,'link to an existing event by name'),('UnLnk',1,'release an event (link count)'),('Creat',2,'create an event'),
    ('Delet',3,'delete an event'),('Wait',4,'wait for an event value in a range'),('WaitR',5,'wait relative to the current value'),
    ('Read',6,'read the value without waiting'),('Info',7,'get event information (copy of the record)'),
    ('Signl',8,'signal an event'),('Pulse',9,'pulse an event'),('Set',10,'set the value and signal'),('SetR',11,'add an increment and signal')]:
    ln('Ev$' + n, v, c)
novalue('alarm functions (no numeric value in the documentation)', ['A$AtDate','A$AtJul','A$Cycle','A$Delete','A$Set'])
novalue('event errors', ['E$EvFull'])
# ---------------- 4 errors
sec('4. ERROR CODES', '16-bit codes: high byte = main number, low byte = sub number.\nMain 0 = kernel/processor/I/O, 1 = compiler library, 6 = graphics/audio subsystem,\n7 = network, 8 = ISDN.  Processor exception codes are the vector number + 100.')
en = dict(l.split('|') for l in ERR_EN)
W16[0] = True
rows = []
for l in open(D + 'SPEC_DEFS.md'):
    m = re.match(r'\| (\S+)(?: \(.*?\))? \| \d{3}:\d{3} = \d+ / \$([0-9A-Fa-f]{4}) \|', l)
    if m and not m.group(1).startswith('S$') and not m.group(1).startswith('('):
        rows.append((m.group(1), int(m.group(2), 16)))
sub = [('all', 0x0000, 0x00ff, 'main 0: kernel, processor exceptions, I/O'), ('c', 0x0100, 0x05ff, 'main 1: compiler library'),
       ('g', 0x0600, 0x06ff, 'main 6: graphics/audio subsystem'), ('n', 0x0700, 0x07ff, 'main 7: network'), ('i', 0x0800, 0x08ff, 'main 8: ISDN')]
seen_e = set()
for s, lo, hi, t in sub:
    grp('errors, ' + t)
    for n, v in rows:
        if lo <= v <= hi:
            e = en.pop(n)
            nm = n.replace('E$PGM+TBLBSY', 'E$PGM_TBLBSY')
            if n == 'E$NOPLAY': nm = 'E$NOPLAY_ISDN'
            ln(nm, v, e + ('  (documentation writes a plus sign in the name)' if '+' in n else ''))
assert not en, en
out.append('*'); out.append('* Unnamed error ranges in the documentation: 0:001 (process aborted),'); out.append('* 0:116-0:123 reserved, 0:124 spurious interrupt, 0:133-0:147 uninitialised')
out.append('* user trap 1-15 (vector+100), 0:159-0:163 invalid exception.  Main numbers 0 to 63 are')
out.append('* reserved for the system.  The documentation also uses E$SectSiz, E$Bmode, E$POLL,')
out.append('* E$IsDull, E$BadId in running text without a table entry: not defined.')
W16[0] = False
# ---------------- 5 module header
sec('5. MODULE HEADER AND CONFIGURATION MODULE FIELDS', 'Byte offsets from the start of a module.  The first block is common to all modules;\nthe other blocks are type dependent and overlay each other from offset $30 on.')
def rows_(lst): 
    for n, v, c in lst: ln('M$' + n, v, c)
grp('standard header, all module types')
rows_([('ID',0x00,'sync bytes (word)'),('SysRev',0x02,'format revision of the module (word)'),('Size',0x04,'total size incl. header and CRC (long)'),
 ('Owner',0x08,'group/user of the owner (long)'),('Name',0x0c,'offset of the NUL-terminated name (long)'),('Accs',0x10,'access permissions (word)'),
 ('Type',0x12,'module type (byte)'),('Lang',0x13,'module language (byte)'),('Attr',0x14,'attribute bits (byte)'),('Revs',0x15,'revision level (byte)'),
 ('Edit',0x16,'edition number (word)'),('Usage',0x18,'offset of a usage comment (long)'),('Symbol',0x1c,'symbol table offset, reserved (long)'),
 ('Ident',0x20,'ident code, unused (word)'),('HdExt',0x28,'offset of the header extension (long)'),('HdExtSz',0x2c,'size of the header extension (word)'),
 ('Parity',0x2e,'header parity: complement of the XOR of the previous header words (word)')])
grp('header extension for executable/trap/driver modules (offsets from $30)')
rows_([('Exec',0x30,'execution entry offset (data module: offset of the data)'),('Excpt',0x34,'default entry for an uninitialised user trap'),
 ('Mem',0x38,'required data area size'),('Stack',0x3c,'minimum stack size'),('IData',0x40,'offset of the initialised data (first long: target offset, second: count)'),
 ('IRefs',0x44,'offset of the table of initialised-pointer references'),('Init',0x48,'offset of the trap initialisation entry'),('Term',0x4c,'offset of the trap termination entry (reserved)')])
grp('device descriptor module (from $30)')
rows_([('Port',0x30,'port address (physical address of the controller, long)'),('Vector',0x34,'interrupt vector number (byte)'),
 ('IRQLvl',0x35,'physical interrupt level (byte)'),('Prior',0x36,'polling priority at the vector (byte)'),('Mode',0x37,'mode capabilities of the device (byte)'),
 ('FMgr',0x38,'offset of the file manager name (word)'),('PDev',0x3a,'offset of the driver name (word)'),('DevCon',0x3c,'offset of the optional configuration table (word)'),
 ('DevFlags',0x40,'device flags, reserved'),('Opt',0x46,'size of the initialisation table (word)'),('DTyp',0x48,'device class, first byte of the initialisation table')])
grp('configuration (init) module')
rows_([('PollSz',0x34,'size of the interrupt polling table'),('DevCnt',0x36,'size of the device table'),('Procs',0x38,'initial process table size'),
 ('Paths',0x3a,'initial path table size'),('SParam',0x3c,'offset of the parameter string for the first module'),('SysGo',0x3e,'offset of the name of the first module to run'),
 ('SysDev',0x40,'offset of the default device name'),('Consol',0x42,'offset of the console path name'),('Extens',0x44,'offset of the list of extension module names'),
 ('Clock',0x46,'offset of the clock module name'),('Slice',0x48,'ticks per time slice'),('Site',0x4c,'installation site code'),
 ('Instal',0x50,'offset of the installation name'),('CPUTyp',0x52,'CPU type'),('OSLvl',0x56,'level (byte), version (word), edition (byte) of the system'),
 ('OSRev',0x5a,'offset of the level/revision string'),('SysPri',0x5c,'start priority of the first module'),('MinPty',0x5e,'minimum executable priority'),
 ('MaxAge',0x60,'maximum natural age'),('MDirSz',0x62,'number of module directory entries'),('Events',0x66,'initial event table size'),
 ('Compat',0x68,'compatibility flag byte'),('Compat2',0x69,'second compatibility flag byte (cache snooping)'),('MemList',0x6a,'offset of the list of coloured memory areas'),
 ('IRQStk',0x6c,'size of the kernel interrupt stack in longwords'),('ColdTrys',0x6e,'retry count for the first start step'),('CacheList',0x74,'offset of the cache list (ends with long -1)'),
 ('IOMan',0x76,'offset of the list of I/O manager module names'),('PreIO',0x78,'offset of the list of pre-I/O module names'),
 ('SysConf',0x7a,'system configuration flags'),('PrcDescStack',0x7e,'stack size inside the process descriptor')])
grp('configuration flag bits: compatibility byte')
for n, v, c in [('SlowIRQ',1,'save all registers on interrupt (obsolete)'),('NoStop',2,'no STOP instruction in the idle loop'),('NoGhost',4,'ignore the sticky bit'),
    ('NoBurst',8,'cache burst off'),('ZapMem',16,'pattern-fill memory'),('NoClock',32,'kernel does not start the system clock'),('SpurIRQ',64,'ignore spurious interrupts'),
    ('PrivAlm',128,'only the creator may delete an alarm')]: ln(n, v, c)
grp('configuration flag bits: second compatibility byte (cache snooping)')
for n, v, c in [('ExtC_I',1,'external instruction cache snoops'),('ExtC_D',2,'external data cache snoops'),('OnC_D',8,'on-chip data cache snoops')]: ln(n, v, c)
ln('ExtCache', 'QExtC_I+QExtC_D', 'both external caches', raw=True)
grp('configuration flag bits: system configuration byte')
for n, v, c in [('NoTblExp',1,'table overflow is an error instead of an expansion'),('CRCDis',4,'no CRC check when validating a module'),('SysTSDis',8,'no time slicing in system state')]: ln(n, v, c)
grp('cache mode names (memory list / cache list), [alias] = same value as another name here')
for n, v, c in [('CM_WrtProt',4,'write-protect'),('CM_CI',0x40,'cache inhibit'),('CM_NotSer',0x20,'not serialised'),('CM_CB',0x20,'copy-back [alias]'),
    ('WritProt',4,'write-protect [alias]'),('WrtThru',0,'write-through'),('CopyBack',0x20,'copy-back [alias]'),('CISer',0x40,'cache-inhibit, serialised [alias]'),
    ('CINotSer',0x60,'cache-inhibit, not serialised')]: ln(n, v, c)
novalue('module fields', ['OnC_I','SSM_NoProt','SSM_SysPT'])
out.append('*   (SSM_SysPT: the documentation gives $20 in a listing but bit 4 in its table: contradictory, left undefined; OnC_I: value not stated literally)')
# ---------------- 6 types
sec('6. MODULE TYPES, LANGUAGES, ATTRIBUTES', 'Values of the type byte, the language byte and the attribute bits of the module header.\nType and language are usually combined as a word: (type<<8)+language.')
grp('module types (QM$Type)')
for n, v, c in [('Prgm',1,'program module'),('Sbrtn',2,'subroutine module'),('Multi',3,'multi-module (reserved)'),('Data',4,'data module'),
    ('CSDData',5,'configuration status descriptor'),('TrapLib',0x0b,'user trap library'),('Systm',0x0c,'system module'),('FlMgr',0x0d,'file manager'),
    ('Drivr',0x0e,'device driver'),('Devic',0x0f,'device descriptor')]: ln(n, v, c)
ln('Prgrm', 'QPrgm', 'spelling variant used in the documentation examples [alias]', raw=True)
out.append('*   (types 0 and 6-10 unnamed: 0 wildcard, 6-10 reserved, 16 and up user-defined; the case variant FlMgr/Flmgr is one name)')
grp('module languages (QM$Lang)')
for n, v, c in [('Objct',1,'machine code'),('ICode',2,'BASIC intermediate code'),('PCode',3,'Pascal intermediate code'),('CCode',4,'C intermediate code (reserved)'),
    ('CblCode',5,'COBOL intermediate code'),('FrtnCode',6,'FORTRAN intermediate code')]: ln(n, v, c)
grp('attribute bits (QM$Attr), high byte of the attr/revision word')
ln('ReEnt', 0x80, 'reentrant module (bit 7) (inferred from listing)'); ln('Supstat', 0x20, 'system-state module (bit 5) (inferred from bit number)')
ln('Sticky', 0x40, 'sticky module (bit 6) (inferred from bit number; the documentation names the bit but not the constant)')
# ---------------- 7 modes
sec('7. MODE AND ACCESS BITS', 'Access mode bits for open/create/attach, memory-list access bits and memory types.')
grp('access mode bits (open, create, make-directory, attach)')
for n, v, c in [('Read_',1,'read (bit 0)'),('Write_',2,'write (bit 1)'),('Updat_',3,'read and write'),('Exec_',4,'execute (bit 2)'),
    ('Append_',0x10,'append (bit 4)'),('ISize_',0x20,'initial size given (bit 5)'),('Share_',0x40,'single user / not sharable (bit 6)'),('Dir_',0x80,'directory (bit 7)')]:
    ln(n, v, c + ' (inferred from listing sums)')
grp('memory list access bits')
for n, v, c in [('B_USER',1,'user processes may allocate (ignored with ROM)'),('B_PARITY',2,'parity memory, initialised by the kernel'),
    ('B_ROM',4,'ROM, searched for modules'),('B_NVRAM',8,'non-volatile RAM, searched for modules'),('B_SHARE',0x10,'shared memory, control structure inside the block')]: ln(n, v, c)
grp('memory types')
ln('SYSRAM', 1, 'system memory'); ln('VIDEO1', 0x80, 'video plane A'); ln('VIDEO2', 0x81, 'video plane B')
out.append('*   (file attribute bits 0-7: owner r/w/e, public r/w/e, single user, directory: unnamed in the documentation)')
# ---------------- 8 descriptors
sec('8. DESCRIPTOR AND DISK STRUCTURE FIELDS', 'Path descriptor (256 bytes: universal part, manager part, 128-byte option table at $80),\ndevice class numbers, disk identification sector, file descriptor, directory entry, driver statics.\nPD_ offsets are path descriptor offsets; the same option field inside a device descriptor\nis at (QM$DTyp + (QPD_x - QPD_OPT)).')
def P(pref, lst, fixed=None):
    for n, v, c in lst: ln(pref + n, v, c)
grp('device classes (value of the class field)')
P('DT_', [('SCF',0,'sequential character stream manager'),('RBF',1,'random block manager'),('Pipe',2,'pipe manager'),('SBF',3,'sequential block manager'),
  ('NFM',4,'network file manager'),('CDFM',5,'compact disc file manager'),('UCM',6,'user communications manager'),('SOCK',7,'socket manager'),
  ('PTTY',8,'pseudo-keyboard manager'),('INET',9,'internet interface manager'),('NRF',10,'non-volatile RAM file manager'),('GFM',11,'graphics file manager'),
  ('ISDN',12,'ISDN file manager'),('MPFM',13,'MPEG file manager')])
grp('path descriptor, universal part')
P('PD_', [('PD',0x00,'path number'),('MOD',0x02,'access mode'),('CNT',0x03,'number of paths (obsolete)'),('DEV',0x04,'device table entry address'),
  ('CPR',0x08,'process id of the requester'),('RGS',0x0a,'address of the caller register stack'),('BUF',0x0e,'address of the data buffer'),
  ('USER',0x12,'group/user id of the original owner'),('PATHS',0x16,'list of open paths on the device'),('COUNT',0x1a,'number of paths using this descriptor'),
  ('LProc',0x1c,'process id of the last activity'),('ErrNo',0x20,'error number for C file managers'),('SysGlob',0x24,'system global pointer for C file managers'),
  ('FST',0x2a,'file-manager-specific work area'),('OPT',0x80,'option table (128 bytes)')])
grp('path descriptor option field shared by all managers')
P('PD_', [('DTP',0x80,'device class (first option byte)')])
grp('path descriptor options, random block manager')
P('PD_', [('DRV',0x81,'drive number'),('STP',0x82,'step rate'),('TYP',0x83,'disk type / descriptor revision'),('DNS',0x84,'density'),
  ('CYL',0x86,'logical cylinders (word)'),('SID',0x88,'heads/sides'),('VFY',0x89,'write verify flag'),('SCT',0x8a,'default sectors per track (word)'),
  ('T0S',0x8c,'sectors per track on track 0 (word)'),('SAS',0x8e,'segment allocation size (sectors)'),('ILV',0x90,'sector interleave'),('TFM',0x91,'DMA transfer mode'),
  ('TOffs',0x92,'track base offset'),('SOffs',0x93,'sector base offset'),('SSize',0x94,'physical sector size (word)'),('Cntl',0x96,'control word'),
  ('Trys',0x98,'retry count'),('LUN',0x99,'SCSI logical unit'),('WPC',0x9a,'first write-precompensation cylinder (word)'),('RWR',0x9c,'first reduced-write-current cylinder (word)'),
  ('Park',0x9e,'park cylinder (word)'),('LSNOffs',0xa0,'logical sector offset (long)'),('TotCyls',0xa4,'physical cylinders (word)'),('CtrlrID',0xa6,'SCSI controller id'),
  ('Rate',0xa7,'rotation and transfer rate'),('ScsiOpt',0xa8,'SCSI options (long)'),('MaxCnt',0xac,'maximum transfer length per call (long)'),
  ('ATT',0xb5,'file attributes'),('FD',0xb6,'sector number of the file descriptor (3 bytes)'),('DFD',0xba,'sector number of the directory file descriptor'),
  ('DCP',0xbe,'directory entry position'),('DVT',0xc2,'copy of the device table pointer'),('SctSiz',0xc8,'logical sector size (0 = 256)'),('NAME',0xe0,'file name (also used by the pipe manager)')])
ln('PD_TOS', 'QPD_T0S', 'spelling variant in the option table listing [alias]', raw=True)
grp('random block manager: density values')
for n, v, c in [('Single',0,'density: single (FM)'),('Double',1,'density: double (MFM)'),('Quad',2,'density: double track density'),('Octal',4,'density: quad track density')]: ln(n, v, c)
grp('random block manager: disk type values')
for n, v, c in [('Five',0,'type: 5.25 inch (descriptor listing name)'),('Eight',1,'type: 8 inch (descriptor listing name)'),('SizeOld',0,'type: old size code'),
    ('Size8',2,'type: 8 inch'),('Size5',4,'type: 5.25 inch'),('Size3',6,'type: 3.5 inch'),('HRemov',0x40,'type: removable hard disk'),('Hard',0x80,'type: hard disk')]: ln(n, v, c)
grp('random block manager: rotation speed values')
for n, v, c in [('rpm300',0,'300 rpm'),('rpm360',1,'360 rpm'),('rpm600',2,'600 rpm')]: ln(n, v, c)
grp('random block manager: transfer rate values')
for n, v, c in [('xfr125K',0x00,'125K bit/s'),('xfr250K',0x10,'250K bit/s'),('xfr300K',0x20,'300K bit/s'),
    ('xfr500K',0x30,'500K bit/s'),('xfr1M',0x40,'1M bit/s'),('xfr2M',0x50,'2M bit/s'),('xfr5M',0x60,'5M bit/s')]: ln(n, v, c)
grp('random block manager: control word bits')
for n, v, c in [('FmtEnabl',0,'control: formatting enabled'),('FmtDsabl',1,'control: formatting disabled'),('MultDsabl',0,'control: multi-sector I/O disabled'),
    ('MultEnabl',2,'control: multi-sector I/O enabled'),('StabDsabl',0,'control: stable id off'),('StabEnabl',4,'control: stable id on'),
    ('AutoDsabl',0,'control: size not autodetected'),('AutoEnabl',8,'control: size from status call'),('FTrkDsabl',0,'control: any track format'),
    ('FTrkEnabl',0x10,'control: single-track format only'),('WritEnab',0,'control: writing allowed'),('WritDsabl',0x20,'control: write-protected by the manager')]: ln(n, v, c)
grp('SCSI option bits')
for n, v, c in [('scsi_atn',1,'SCSI: disconnect allowed'),('scsi_target',2,'SCSI: target capable'),('scsi_synchr',4,'SCSI: synchronous'),('scsi_parity',8,'SCSI: parity')]: ln(n, v, c)
out.append('*   (within the above constant groups, zero values repeat by design: they belong to different fields or to the "off" state)')
grp('path descriptor options, character stream manager')
P('PD_', [('UPC',0x81,'force upper case'),('BSO',0x82,'backspace option'),('DLO',0x83,'delete-line option'),('EKO',0x84,'echo on/off'),('ALF',0x85,'auto line feed after CR'),
  ('NUL',0x86,'null count after end of line'),('PAU',0x87,'page pause on/off'),('PAG',0x88,'page length (lines)'),('BSP',0x89,'backspace input character'),
  ('DEL',0x8a,'delete-line character'),('EOR',0x8b,'end-of-record character'),('EOF',0x8c,'end-of-file character'),('RPR',0x8d,'reprint-line character'),
  ('DUP',0x8e,'duplicate-line character'),('PSC',0x8f,'pause character'),('INT',0x90,'keyboard interrupt character'),('QUT',0x91,'keyboard quit character'),
  ('BSE',0x92,'backspace echo character'),('OVF',0x93,'line overflow (bell) character'),('PAR',0x94,'parity, stop bits, bits per character'),
  ('BAU',0x95,'baud rate code'),('D2P',0x96,'offset of the output device name (word)'),('XON',0x98,'X-ON character'),('XOFF',0x99,'X-OFF character'),
  ('TAB',0x9a,'tab character'),('TABS',0x9b,'tab width'),('TBL',0x9c,'visible copy of the device table entry (long)'),('Col',0xa0,'current column'),('Err',0xa2,'last I/O error status')])
grp('path descriptor options, sequential block manager')
P('PD_', [('TDrv',0x81,'tape drive number'),('SBF',0x82,'reserved'),('NumBlk',0x83,'maximum number of buffers (0 = unbuffered)'),('BlkSiz',0x84,'logical block size (long)'),
  ('Prior',0x88,'priority of the helper process'),('SBFFlags',0x8a,'path flags (word)'),('DrivFlag',0x8b,'driver flags'),('DMAMode',0x8c,'DMA mode'),
  ('ScsiID',0x8e,'SCSI controller id'),('ScsiLUN',0x8f,'SCSI logical unit'),('ScsiOpts',0x90,'SCSI options')])
ln('PD_Flags', 'QPD_SBFFlags', 'the documentation uses both names for the flag word [alias]', raw=True)
P('f_', [('rest_b',0,'flag bit: rewind on close'),('offl_b',1,'flag bit: drive offline'),('eras_b',2,'flag bit: erase to end of tape')])
grp('path descriptor options, pipe manager (the name field is the same as in the block manager option list)')
P('PD_', [('BufSz',0x82,'default FIFO buffer size'),('IOBuf',0x86,'small default I/O buffer')])
grp('identification sector (logical sector 0) of a random block medium')
P('DD_', [('TOT',0x00,'total sectors (3 bytes)'),('TKS',0x03,'sectors per track'),('MAP',0x04,'bytes in the allocation map (word)'),('BIT',0x06,'sectors per bit (word)'),
  ('DIR',0x08,'sector of the root directory descriptor (3 bytes)'),('OWN',0x0b,'owner id (word)'),('ATT',0x0d,'attributes'),('DSK',0x0e,'disk id (word)'),
  ('FMT',0x10,'format flags'),('SPT',0x11,'sectors per track (word)'),('RES',0x13,'reserved (word)'),('BT',0x15,'boot sector (3 bytes, 0 = none)'),
  ('BSZ',0x18,'boot size (word)'),('DAT',0x1a,'creation date (5 bytes)'),('NAM',0x1f,'volume name (32 bytes)'),('OPT',0x3f,'path descriptor options (32 bytes)'),
  ('SYNC',0x60,'media integrity code (long)'),('MapLSN',0x64,'first sector of the allocation map (long, 0 = sector 1)'),('LSNSize',0x68,'logical sector size (word, 0 = 256)'),
  ('VersID',0x6a,'version id of sector 0 (word)')])
grp('identification sector constant')
P('DD_', [('SIZ',21,'bytes copied from sector 0 into the drive table (a size, not an offset)')])
grp('file descriptor sector and directory entry')
P('FD_', [('ATT',0x00,'attributes'),('OWN',0x01,'owner id (word)'),('DAT',0x03,'last-modified date (5 bytes)'),('LNK',0x08,'link count'),('SIZ',0x09,'file size (long)'),
  ('CREAT',0x0d,'creation date (3 bytes)'),('SEG',0x10,'segment list (240 bytes, 5-byte entries)')])
P('DIR_', [('NM',0x00,'file name (28 bytes, last character has bit 7 set)'),('FD',0x1d,'sector of the file descriptor (3 bytes)')])
grp('random block manager drive table (driver/manager maintained fields)')
P('V_', [('TRAK',0x16,'current track'),('FileHd',0x18,'list of open files'),('DiskID',0x1c,'disk id'),('BMapSz',0x1e,'bitmap size'),('MapSct',0x20,'lowest bitmap sector to search'),
  ('BMB',0x22,'bitmap in use flag'),('ScZero',0x24,'pointer to sector 0'),('ZeroRd',0x28,'sector 0 read flag'),('Init',0x29,'drive initialised'),
  ('Resbit',0x2a,'reserved bitmap sector number'),('SoftEr',0x2c,'recoverable error count'),('HardEr',0x30,'unrecoverable error count'),('Cache',0x34,'cache queue head'),
  ('DText',0x38,'pointer to a drive table extension'),('MapMax',0x3c,'highest bitmap sector'),('MapOffs',0x3e,'bitmap sector offset')])
grp('driver static storage, common start of all managers')
P('V_', [('PORT',0x00,'port address'),('LPRC',0x04,'last active process id'),('BUSY',0x06,'active process id'),('WAKE',0x08,'process to wake on completion'),('Paths',0x0a,'list of open paths')])
grp('driver static storage, character stream manager')
P('V_', [('DEV2',0x2e,'address of the attached output static storage'),('TYPE',0x32,'device type'),('LINE',0x33,'lines left on page'),('PAUS',0x34,'pause request'),
  ('INTR',0x35,'interrupt character'),('QUIT',0x36,'quit character'),('PCHR',0x37,'pause character'),('ERR',0x38,'accumulated errors'),('XON',0x39,'X-ON character'),
  ('XOFF',0x3a,'X-OFF character'),('Hangup',0x46,'hang-up flag')])
grp('driver static storage, sequential block manager')
P('SBF_', [('NDRV',0x30,'number of drives'),('Flag',0x32,'flags'),('Drvr',0x34,'driver address'),('DPrc',0x38,'driver process'),('IPrc',0x3c,'input process')])
grp('sequential block manager drive table')
P('SBF_', [('DFlg',0x00,'drive flags'),('NBuf',0x02,'number of buffers'),('IBH',0x04,'input buffer head'),('IBT',0x08,'input buffer tail'),('OBH',0x0c,'output buffer head'),
  ('OBT',0x10,'output buffer tail'),('Wait',0x14,'wait state'),('SErr',0x18,'soft errors'),('HErr',0x1c,'hard errors')])
ss = ['SS_CDFD','SS_DevNam','SS_EOF','SS_FD','SS_FDInf','SS_Free','SS_Opt','SS_Pos','SS_Ready','SS_Size','SS_VarSect','SS_DSize','SS_ELog','SS_Attr','SS_Close','SS_DCOff','SS_DCOn','SS_DsRTS','SS_EnRTS','SS_Feed','SS_Lock','SS_Open','SS_Relea','SS_Reset','SS_RFM','SS_Skip','SS_SSig','SS_Ticks','SS_WFM','SS_WTrk','SS_Break','SS_SQD','SS_Reten','SS_RsBit']
novalue('status codes for the get/set status calls (no numeric value anywhere)', ss)
novalue('process descriptor fields and system globals (no offsets given)', ['P$ID','P$State','P$Signal','P$DIO','P$Preempt','P$sp','D_MinPty','D_MaxAge','D_Proc','D_SysPrc','D_SnoopD','D_TSlice','D_SysMin','R$cc','R$d1','R$d2','R$PC','R$a7','PD_DTB','V_NDRV','V_DRVBEG'])
# ---------------- 9 signals
sec('9. SIGNALS, VECTORS, LIMITS', 'Signal codes; vector numbering and limits are described below, but the documentation gives no names for them.')
grp('signals')
for n, v, c in [('Kill',0,'unconditional kill (cannot be caught or masked)'),('Wake',1,'wake up (not caught, not queued)'),('Abort',2,'keyboard abort'),
    ('Intrpt',3,'keyboard interrupt'),('HangUp',4,'modem hang-up'),('Deadly',32,'signals below this value are deadly to I/O')]: ln('S$' + n, v, c)
ln('S$Intrp', 'QS$Intrpt', 'spelling variant in the driver manual [alias]', raw=True)
novalue('signal/panic/trap/character names', ['S$Quit','K$Idle','K$PFail','T_TRAPV','T_CHK','C$Bsp','C$Del','C$CR','C$EOF','C$Rprt','C$Rpet','C$Paus','C$Intr','C$Quit','C$Bell','C$XOn','C$XOff','C$Tab'])
out += ['*', '* Facts without names (documented numbers only):',
 '*   signals 5-31 reserved/deadly to I/O (26-31 user-defined), 32-255 reserved, 256-65535 user-defined.',
 '*   vectors: 0 reset SSP, 1 reset PC, 2-11 exceptions, 24 spurious, 25-31 autovectors level 1-7,',
 '*   32 = user trap 0 (system call), 33-47 = user traps 1-15, 57-63 on-chip autovectors (one CPU variant),',
 '*   64-255 vectored interrupts.  Processor exception error code = vector + 100.',
 '*   Limits: event name 11 characters, event record 32 bytes, path descriptor 256 bytes (options 128),',
 '*   option table max 128 bytes, priorities 0-65535, service request codes 0-255, module CRC 24 bit.',
 '*']
open(D + 'q9sys.d', 'w').write('\n'.join(out) + '\n')
for k, v in stats.items(): print(v, k)
