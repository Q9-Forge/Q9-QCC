*
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
*
*
* ======================================================================
* SECTION: 1. SYSTEM CALLS (kernel)
*   Request codes for the kernel calls, issued as a trap followed by a code word.
*   Input/output registers are documented per call in the manual, not repeated here.
* ======================================================================
*
Q$AProc          equ     $2c        * put a process into the active queue (system)
Q$Alarm          equ     $56        * set, cancel or query an alarm
Q$AllBit         equ     $13        * mark bits as allocated in a bitmap
Q$AllPD          equ     $30        * allocate a process/path descriptor (system)
Q$AllPrc         equ     $4b        * allocate a process descriptor (system)
Q$AllTsk         equ     $3f        * prepare protection hardware for a task (memory-protection unit)
Q$CCtl           equ     $5a        * cache control
Q$CRC            equ     $17        * compute a CRC over a data block
Q$Chain          equ     $05        * replace the calling process by a new primary module (no return)
Q$ChkMem         equ     $58        * check access rights to a memory area (memory-protection unit)
Q$CmpNam         equ     $11        * compare two names
Q$CpyMem         equ     $1b        * copy memory of another process
Q$DExec          equ     $23        * run a debugged child for some instructions
Q$DExit          equ     $24        * terminate a debugged child
Q$DFork          equ     $22        * create a process under debugger control
Q$DatMod         equ     $25        * create a data module
Q$DelBit         equ     $14        * mark bits as free in a bitmap
Q$DelPrc         equ     $4c        * release a process descriptor (system)
Q$DelTsk         equ     $40        * release protection structures of a task (memory-protection unit)
Q$Event          equ     $53        * create, use or delete an event (sub-code QEv$...)
Q$Exit           equ     $06        * terminate the calling process
Q$FModul         equ     $4e        * module directory lookup (listed in the call table only)
Q$FindPD         equ     $2f        * find a process/path descriptor (system)
Q$Fork           equ     $03        * create a child process
Q$GBlkMp         equ     $19        * get a map of free memory blocks
Q$GModDr         equ     $1a        * copy the module directory
Q$GPrDBT         equ     $1f        * copy the process descriptor block table
Q$GPrDsc         equ     $18        * copy a process descriptor
Q$GProcP         equ     $37        * get a process descriptor pointer (system)
Q$GSPUMp         equ     $5b        * get access-rights status (memory-protection unit)
Q$Gregor         equ     $54        * Julian to Gregorian date conversion
Q$ID             equ     $0c        * get process id, user id and priority
Q$IODel          equ     $33        * check whether an I/O module is in use (system)
Q$IOQu           equ     $2b        * enter a process into an I/O queue (system)
Q$IRQ            equ     $2a        * add or remove an interrupt polling entry (system)
Q$Icpt           equ     $09        * set up a signal intercept routine
Q$Julian         equ     $20        * Gregorian to Julian date conversion
Q$Link           equ     $00        * link to a module in memory by name
Q$Load           equ     $01        * load modules from a file
Q$Mem            equ     $07        * grow or shrink the data area
Q$Move           equ     $38        * copy data between address spaces (system)
Q$NProc          equ     $2d        * start the next process (system, no return)
Q$PErr           equ     $0f        * print an error message
Q$Panic          equ     $5e        * report a system catastrophe (system)
Q$Permit         equ     $3a        * grant a process access to memory
Q$Protect        equ     $3b        * revoke access to memory
Q$PrsNam         equ     $10        * parse a path name
Q$RTE            equ     $1e        * return from an interrupt exception
Q$RetPD          equ     $31        * return a process/path descriptor (system)
Q$SPrior         equ     $0d        * set a process priority
Q$SRqCMem        equ     $5c        * request coloured system memory
Q$SRqMem         equ     $28        * request system memory
Q$SRtMem         equ     $29        * return system memory
Q$SSpd           equ     $0b        * suspend a process
Q$SSvc           equ     $32        * install service-request table entries (system)
Q$STime          equ     $16        * set system date and time
Q$STrap          equ     $0e        * set the error-trap handler
Q$SUser          equ     $1c        * set the user id
Q$SchBit         equ     $12        * search a bitmap for a free area
Q$Send           equ     $08        * send a signal
Q$SetCRC         equ     $26        * write a valid CRC into a module
Q$SetSys         equ     $27        * set or read a system global variable
Q$Sleep          equ     $0a        * put the process to sleep
Q$SysDbg         equ     $52        * call the system debugger
Q$SysID          equ     $55        * get system identification data
Q$TLink          equ     $21        * install a user-trap handler module
Q$Time           equ     $15        * get system date and time
Q$Trans          equ     $60        * translate a memory address
Q$UAcct          equ     $59        * user accounting hook
Q$UnLink         equ     $02        * unlink a module by address
Q$UnLoad         equ     $1d        * unload a module by name
Q$VModul         equ     $2e        * validate a module (system)
Q$Wait           equ     $04        * wait for a child process
*
* Names without a value (the manual gives none, no number table entry): kernel calls
*   Q$Attach Q$FIRQ Q$Mbuf Q$OSCall Q$Sema Q$Service Q$SigReset Q$Sigmask
*   Q$SysTrap
*
* ======================================================================
* SECTION: 2. I/O CALLS
*   Request codes for the I/O calls (handled by the I/O manager layer).
* ======================================================================
*
IQ$Attach        equ     $80        * attach a device to the system
IQ$ChgDir        equ     $86        * change the working directory
IQ$Close         equ     $8f        * close a path
IQ$Create        equ     $83        * create a new file
IQ$Delete        equ     $87        * delete a file
IQ$Detach        equ     $81        * detach a device from the system
IQ$Dup           equ     $82        * duplicate a path
IQ$GetStt        equ     $8d        * get file/device status
IQ$MakDir        equ     $85        * create a directory
IQ$Open          equ     $84        * open a path
IQ$Read          equ     $89        * read data
IQ$ReadLn        equ     $8b        * read a text line with editing
IQ$SGetSt        equ     $92        * listed in the call table only (no description)
IQ$Seek          equ     $88        * position the file pointer
IQ$SetStt        equ     $8e        * set file/device status
IQ$WritLn        equ     $8c        * write a text line with editing
IQ$Write         equ     $8a        * write data
*
* ======================================================================
* SECTION: 3. EVENT AND ALARM CODES
*   Sub-function codes of the event call, and the alarm function names.
* ======================================================================
*
*
* -- event functions (d1.w of the event call)
QEv$Link         equ     $00        * link to an existing event by name
QEv$UnLnk        equ     $01        * release an event (link count)
QEv$Creat        equ     $02        * create an event
QEv$Delet        equ     $03        * delete an event
QEv$Wait         equ     $04        * wait for an event value in a range
QEv$WaitR        equ     $05        * wait relative to the current value
QEv$Read         equ     $06        * read the value without waiting
QEv$Info         equ     $07        * get event information (copy of the record)
QEv$Signl        equ     $08        * signal an event
QEv$Pulse        equ     $09        * pulse an event
QEv$Set          equ     $0a        * set the value and signal
QEv$SetR         equ     $0b        * add an increment and signal
*
* Names without a value (the manual gives none, no number table entry): alarm functions (no numeric value in the documentation)
*   QA$AtDate QA$AtJul QA$Cycle QA$Delete QA$Set
*
* Names without a value (the manual gives none, no number table entry): event errors
*   EQ$EvFull
*
* ======================================================================
* SECTION: 4. ERROR CODES
*   16-bit codes: high byte = main number, low byte = sub number.
*   Main 0 = kernel/processor/I/O, 1 = compiler library, 6 = graphics/audio subsystem,
*   7 = network, 8 = ISDN.  Processor exception codes are the vector number + 100.
* ======================================================================
*
*
* -- errors, main 0: kernel, processor exceptions, I/O
QE_SIGABRT       equ     $0020      * abort signal
QE_SIGFPE        equ     $0021      * erroneous math operation
QE_SIGILL        equ     $0022      * illegal function image
QE_SIGSEGV       equ     $0023      * segment violation (bus error)
QE_SIGTERM       equ     $0024      * termination request
QE_SIGALRM       equ     $0025      * alarm time elapsed
QE_SIGPIPE       equ     $0026      * write to a pipe without reader
QE_SIGUSR1       equ     $0027      * user signal 1
QE_SIGUSR2       equ     $0028      * user signal 2
QE_SIGADDR       equ     $0029      * address error
QE_SIGCHK        equ     $002a      * CHK instruction
QE_SIGTRAPV      equ     $002b      * TRAPV instruction
QE_SIGPRIV       equ     $002c      * privilege violation
QE_SIGTRACE      equ     $002d      * trace exception
QE_SIG1010       equ     $002e      * line-A exception
QE_SIG1111       equ     $002f      * line-F exception
EQ$IllFnc        equ     $0040      * illegal function code (math trap handler)
EQ$FmtErr        equ     $0041      * format error in ASCII-to-number conversion
EQ$NotNum        equ     $0042      * no number found
EQ$IllArg        equ     $0043      * illegal argument
EQ$BusErr        equ     $0066      * bus error
EQ$AdrErr        equ     $0067      * address error
EQ$IllIns        equ     $0068      * illegal instruction
EQ$ZerDiv        equ     $0069      * division by zero
EQ$Chk           equ     $006a      * CHK/CHK2 exception
EQ$TrapV         equ     $006b      * TRAPV/TRAPcc exception
EQ$Violat        equ     $006c      * privilege violation
EQ$Trace         equ     $006d      * uninitialised trace exception
EQ$1010          equ     $006e      * line-A emulator exception
EQ$1111          equ     $006f      * line-F emulator exception
EQ$Resrvd        equ     $0070      * reserved exception
EQ$CProto        equ     $0071      * coprocessor protocol violation
EQ$StkFmt        equ     $0072      * stack frame format error
EQ$UnIRQ         equ     $0073      * uninitialised interrupt
EQ$FPUnordC      equ     $0094      * FPU: unordered condition
EQ$FPInxact      equ     $0095      * FPU: inexact result
EQ$FPDivZer      equ     $0096      * FPU: division by zero
EQ$FPUndrFl      equ     $0097      * FPU: underflow
EQ$FPOprErr      equ     $0098      * FPU: operand error
EQ$FPOverFl      equ     $0099      * FPU: overflow
EQ$FPNotNum      equ     $009a      * FPU: not a number
EQ$UnData        equ     $009b      * FPU: unimplemented data type
EQ$MMUConf       equ     $009c      * memory-management unit: configuration error
EQ$MMUIlleg      equ     $009d      * memory-management unit: illegal operation
EQ$MMUAcces      equ     $009e      * memory-management unit: access level violation
EQ$Permit        equ     $00a4      * no permission (superuser needed)
EQ$Differ        equ     $00a5      * arguments differ (memory-check call)
EQ$StkOvf        equ     $00a6      * stack overflow
EQ$EvntID        equ     $00a7      * invalid event id
EQ$EvNF          equ     $00a8      * event name not found
EQ$EvBusy        equ     $00a9      * event busy
EQ$EvParm        equ     $00aa      * impossible event parameter
EQ$Damage        equ     $00ab      * system data structure damaged
EQ$BadRev        equ     $00ac      * incompatible revision
EQ$PthLost       equ     $00ad      * path lost
EQ$BadPart       equ     $00ae      * bad or no active partition
EQ$Hardware      equ     $00af      * hardware damage detected
EQ$SectSize      equ     $00b0      * invalid sector size
EQ$BSig          equ     $00b1      * unexpected or invalid signal
EQ$PthFul        equ     $00c8      * path table full
EQ$BPNum         equ     $00c9      * bad path number
EQ$Poll          equ     $00ca      * interrupt polling table full
EQ$BMode         equ     $00cb      * bad mode
EQ$DevOvf        equ     $00cc      * device table full
EQ$BMID          equ     $00cd      * bad module header
EQ$DirFul        equ     $00ce      * module directory full
EQ$MemFul        equ     $00cf      * memory full
EQ$UnkSvc        equ     $00d0      * unknown service code
EQ$ModBsy        equ     $00d1      * module busy
EQ$BPAddr        equ     $00d2      * bad memory address (boundary error)
EQ$EOF           equ     $00d3      * end of file
EQ$VctBsy        equ     $00d4      * interrupt vector busy
EQ$NES           equ     $00d5      * non-existing segment
EQ$FNA           equ     $00d6      * file not accessible
EQ$BPNam         equ     $00d7      * bad path name
EQ$PNNF          equ     $00d8      * path name not found
EQ$SLF           equ     $00d9      * segment list full
EQ$CEF           equ     $00da      * file already exists
EQ$IBA           equ     $00db      * illegal block address
EQ$HangUp        equ     $00dc      * modem carrier lost
EQ$MNF           equ     $00dd      * module not found
EQ$NoClk         equ     $00de      * no clock
EQ$DelSP         equ     $00df      * stack memory release requested
EQ$IPrcID        equ     $00e0      * illegal process id
EQ$Param         equ     $00e1      * illegal parameter
EQ$NoChld        equ     $00e2      * no child processes
EQ$ITrap         equ     $00e3      * illegal trap code
EQ$PrcAbt        equ     $00e4      * process aborted
EQ$PrcFul        equ     $00e5      * process table full
EQ$IForkP        equ     $00e6      * illegal parameter area
EQ$KwnMod        equ     $00e7      * module already known
EQ$BMCRC         equ     $00e8      * bad module CRC
EQ$USigP         equ     $00e9      * unprocessed signal pending
EQ$NEMod         equ     $00ea      * module not executable
EQ$BNam          equ     $00eb      * bad name
EQ$BMHP          equ     $00ec      * bad module header parity
EQ$NoRAM         equ     $00ed      * no free system RAM
EQ$DNE           equ     $00ee      * directory not empty
EQ$NoTask        equ     $00ef      * no free task number
EQ$Unit          equ     $00f0      * illegal drive number
EQ$Sect          equ     $00f1      * bad sector
EQ$WP            equ     $00f2      * write protected
EQ$CRC           equ     $00f3      * CRC error
EQ$Read          equ     $00f4      * read error
EQ$Write         equ     $00f5      * write error
EQ$NotRdy        equ     $00f6      * not ready
EQ$Seek          equ     $00f7      * seek error
EQ$Full          equ     $00f8      * medium full
EQ$BTyp          equ     $00f9      * bad type
EQ$DevBsy        equ     $00fa      * device busy
EQ$DIDC          equ     $00fb      * disk id changed
EQ$Lock          equ     $00fc      * record locked
EQ$Share         equ     $00fd      * non-sharable file busy
EQ$DeadLk        equ     $00fe      * I/O deadlock
EQ$Format        equ     $00ff      * device format-protected
*
* -- errors, main 1: compiler library
QERANGE          equ     $0100      * number out of range (ANSI C)
QEDOM            equ     $0101      * number not in domain (ANSI C)
*
* -- errors, main 6: graphics/audio subsystem
EQ$IllPrm        equ     $0600      * graphics/audio subsystem: illegal parameter
EQ$IdFull        equ     $0601      * id table full
EQ$BadSiz        equ     $0602      * bad size
EQ$RgFull        equ     $0603      * region definition full
EQ$UnID          equ     $0604      * unallocated id number
EQ$NullRg        equ     $0605      * null region
EQ$BadMod        equ     $0606      * bad drawmap or pattern mode
EQ$NoFont        equ     $0607      * no active font
EQ$NoDM          equ     $0608      * no drawmap
EQ$NoPlay        equ     $0609      * no audio playback active
EQ$Abort         equ     $060a      * audio record/playback aborted
EQ$QFull         equ     $060b      * audio queue full
EQ$Busy          equ     $060c      * audio processor busy
QE_RES_NOSLOT    equ     $0664      * no free resource slot
QE_RES_BADSLOT   equ     $0665      * bad resource slot
QE_RES_NOSHARE   equ     $0666      * resource not sharable
QE_RES_NOTYPE    equ     $0667      * wrong resource type
QE_RES_NORES     equ     $0668      * wrong resource id
QE_REQ_NOITEMS   equ     $066e      * no items for request
QE_REQ_BADITEM   equ     $066f      * item number out of range
QE_REQ_BADCOLS   equ     $0670      * column number out of range
QE_REQ_BADPTR    equ     $0671      * bad item array pointer
QE_REQ_NOCREATE  equ     $0672      * request cannot be created
QE_REQ_TIMEOUT   equ     $0673      * modal request timed out
QE_REQ_NOSEL     equ     $0674      * no selection made
QE_REQ_DEFID     equ     $0675      * bad definition function id
QE_REQ_DEFACT    equ     $0676      * bad definition action code
QE_REQ_STATE     equ     $0677      * bad item state
QE_REQ_BADRECT   equ     $0678      * bad request rectangle
QE_CNT_BHVID     equ     $0682      * bad standard behaviour id
QE_CNT_DEFID     equ     $0683      * bad standard definition id
QE_CNT_DEFACT    equ     $0684      * bad action for definition function
QE_CNT_BHVACT    equ     $0685      * bad action for behaviour function
QE_CNT_STATE     equ     $0686      * bad control state
QE_CNT_PART      equ     $0687      * bad control part code
QE_CNT_FLAGS     equ     $0688      * bad flags
QE_CNT_MINMAX    equ     $0689      * bad minimum/maximum/value
QE_CNT_TYPE      equ     $068a      * bad control type
QE_CLIP_DEV      equ     $068c      * clipboard device not in preferences
QE_CLIP_FULL     equ     $068d      * clipboard full
QE_CLIP_TYPE     equ     $068e      * type not in clipboard
QE_CLIP_ACC      equ     $068f      * clipboard not open for access
QE_CLIP_CNT      equ     $0690      * type offset larger than type count
QE_CLIP_OPEN     equ     $0691      * clipboard not open
QE_CLIP_INIT     equ     $0692      * clipboard not initialised
QE_CLIP_CLOSE    equ     $0693      * clipboard not closed
QE_CLIP_RW       equ     $0694      * rewrite impossible, type missing
QE_HNDLR_UNKNOWN equ     $0696      * unknown handler
QE_ATABL_NOENTRY equ     $069b      * no entry found
QE_BOX_TABLE     equ     $06a0      * line table overflows
QE_BOX_COUNT     equ     $06a1      * text too long (max 65535)
QE_BOX_TYPE      equ     $06a2      * bad or unimplemented type
QE_BOX_MAXL      equ     $06a3      * line too long
QE_BOX_NOTAB     equ     $06a4      * line table required
QE_BOX_NOFONT    equ     $06a5      * font not set in drawmap
QE_BOX_RECT      equ     $06a6      * bad rectangle
QE_INIT_VARERROR equ     $06b4      * error in global variable
QE_INTER_NOMOD   equ     $06b9      * no preference module
QE_INTER_ILLARG  equ     $06ba      * illegal argument
QE_OVL_BADRECT   equ     $06be      * bad overlay rectangle
QE_OVL_NOTTOP    equ     $06bf      * overlay not on top of stack
QE_OVL_UNKNOWN   equ     $06c0      * unknown overlay
QE_IND_DEFID     equ     $06c8      * bad definition id
QE_IND_DEFACT    equ     $06c9      * bad definition action
QE_IND_MINMAX    equ     $06ca      * bad minimum/maximum/value
QE_IND_BADCOORDS equ     $06cb      * bad coordinates
QE_IND_NOCREATE  equ     $06cc      * indicator not created
QE_IND_BADFLAGS  equ     $06cd      * bad flags
QE_IND_BADPTR    equ     $06ce      * bad pointer
QE_IND_BADDISP   equ     $06cf      * bad displacement
*
* -- errors, main 7: network
QEWOULDBLOCK     equ     $0701      * I/O would block
QEINPROGRESS     equ     $0702      * I/O in progress
QEALREADY        equ     $0703      * operation already in progress
QEDESTADDRREQ    equ     $0704      * destination address required
QEMSGSIZE        equ     $0705      * message too long
QEPROTOTYPE      equ     $0706      * wrong protocol for socket
QENOPROTOOPT     equ     $0707      * bad protocol option
QEPROTONOSUPPORT equ     $0708      * protocol not supported
QESOCKNOSUPPORT  equ     $0709      * socket type not supported
QEOPNOTSUPPORT   equ     $070a      * operation not supported on socket
QEPFNOSUPP       equ     $070b      * protocol family not supported
QEAFNOSUPPORT    equ     $070c      * address family not supported
QEADDRINUSE      equ     $070d      * address in use
QEADDRNOTAVAIL   equ     $070e      * address not assignable
QENETDOWN        equ     $070f      * network down
QENETUNREACH     equ     $0710      * network unreachable
QENETRESET       equ     $0711      * network dropped connection on reset
QECONNABORTED    equ     $0712      * connection aborted by software
QECONNRESET      equ     $0713      * connection reset by peer
QENOBUFS         equ     $0714      * no buffer space
QEISCONN         equ     $0715      * socket already connected
QENOTCONN        equ     $0716      * socket not connected
QESHUTDOWN       equ     $0717      * cannot send after shutdown
QETOOMANYREFS    equ     $0718      * too many references
QETIMEDOUT       equ     $0719      * connection timed out
QECONNREFUSED    equ     $071a      * connection refused
QEBUFTOOSMALL    equ     $071b      * buffer too small (memory-buffer call)
QESMODEXISTS     equ     $071c      * socket module already attached
QENOTSOCK        equ     $071d      * path is not a socket
QEHOSTUNREACH    equ     $071e      * no route to host
QEHOSTDOWN       equ     $071f      * host down
*
* -- errors, main 8: ISDN
EQ$LnkDwn        equ     $0801      * link down / layer-1 error on attach
EQ$Conn          equ     $0802      * connection error
EQ$RxThread      equ     $0803      * error in receive thread
EQ$ME            equ     $0804      * management entity error
EQ$SAPI          equ     $0805      * unknown SAPI
EQ$TEI           equ     $0806      * TEI error
EQ$Max_TEI       equ     $0807      * maximum terminal endpoints in use
EQ$TState        equ     $0808      * illegal layer-2 state
EQ$TEI_Denied    equ     $0809      * TEI initialisation denied
EQ$Primitive     equ     $080a      * unknown primitive
EQ$L2In          equ     $080b      * layer-2 error on incoming message
EQ$Peer_Busy     equ     $080c      * peer busy
EQ$K             equ     $080d      * maximum outstanding messages
EQ$MaxCRef       equ     $080e      * maximum call references in use
EQ$CRef          equ     $080f      * call reference does not exist
EQ$CallProg      equ     $0810      * call progress error
EQ$Rcvr          equ     $0811      * receiver allocation/removal error
EQ$REQDENIED     equ     $0812      * request denied by peer
EQ$RXSTART       equ     $0813      * receive thread not started
EQ$NOSTACK       equ     $0814      * last driver in path stack
EQ$BTMSTK        equ     $0815      * attempt to remove last driver
EQ$NPBNULL       equ     $0816      * notify parameter block is NULL
EQ$PPS_NOTFND    equ     $0817      * per-path storage not found
EQ$STKFULL       equ     $0818      * path stack full
EQ$MBNOTINST     equ     $0819      * memory-buffer module not installed
EQ$TMRNTFND      equ     $081a      * timer not found
EQ$GETIME        equ     $081b      * time error
EQ$TIMERINT      equ     $081c      * timer interrupt
EQ$RXMB_NODEVENTRY equ     $081d      * no device entry in buffer
EQ$PGM_TBLBSY    equ     $081e      * program/service table in use  (documentation writes a plus sign in the name)
EQ$TBLOVF        equ     $081f      * too many tables
EQ$PGM_TBLNFND   equ     $0820      * table not found
EQ$PGM_NFND      equ     $0821      * program not found
EQ$NOPLAY_ISDN   equ     $0822      * no program running
EQ$NODNDRVR      equ     $0823      * no down driver
EQ$RXMB_ERR      equ     $0828      * receive data error (base)
*
* Unnamed error ranges in the documentation: 0:001 (process aborted),
* 0:116-0:123 reserved, 0:124 spurious interrupt, 0:133-0:147 uninitialised
* user trap 1-15 (vector+100), 0:159-0:163 invalid exception.  Main numbers 0 to 63 are
* reserved for the system.  The documentation also uses E$SectSiz, E$Bmode, E$POLL,
* E$IsDull, E$BadId in running text without a table entry: not defined.
*
* ======================================================================
* SECTION: 5. MODULE HEADER AND CONFIGURATION MODULE FIELDS
*   Byte offsets from the start of a module.  The first block is common to all modules;
*   the other blocks are type dependent and overlay each other from offset $30 on.
* ======================================================================
*
*
* -- standard header, all module types
QM$ID            equ     $00        * sync bytes (word)
QM$SysRev        equ     $02        * format revision of the module (word)
QM$Size          equ     $04        * total size incl. header and CRC (long)
QM$Owner         equ     $08        * group/user of the owner (long)
QM$Name          equ     $0c        * offset of the NUL-terminated name (long)
QM$Accs          equ     $10        * access permissions (word)
QM$Type          equ     $12        * module type (byte)
QM$Lang          equ     $13        * module language (byte)
QM$Attr          equ     $14        * attribute bits (byte)
QM$Revs          equ     $15        * revision level (byte)
QM$Edit          equ     $16        * edition number (word)
QM$Usage         equ     $18        * offset of a usage comment (long)
QM$Symbol        equ     $1c        * symbol table offset, reserved (long)
QM$Ident         equ     $20        * ident code, unused (word)
QM$HdExt         equ     $28        * offset of the header extension (long)
QM$HdExtSz       equ     $2c        * size of the header extension (word)
QM$Parity        equ     $2e        * header parity: complement of the XOR of the previous header words (word)
*
* -- header extension for executable/trap/driver modules (offsets from $30)
QM$Exec          equ     $30        * execution entry offset (data module: offset of the data)
QM$Excpt         equ     $34        * default entry for an uninitialised user trap
QM$Mem           equ     $38        * required data area size
QM$Stack         equ     $3c        * minimum stack size
QM$IData         equ     $40        * offset of the initialised data (first long: target offset, second: count)
QM$IRefs         equ     $44        * offset of the table of initialised-pointer references
QM$Init          equ     $48        * offset of the trap initialisation entry
QM$Term          equ     $4c        * offset of the trap termination entry (reserved)
*
* -- device descriptor module (from $30)
QM$Port          equ     $30        * port address (physical address of the controller, long)
QM$Vector        equ     $34        * interrupt vector number (byte)
QM$IRQLvl        equ     $35        * physical interrupt level (byte)
QM$Prior         equ     $36        * polling priority at the vector (byte)
QM$Mode          equ     $37        * mode capabilities of the device (byte)
QM$FMgr          equ     $38        * offset of the file manager name (word)
QM$PDev          equ     $3a        * offset of the driver name (word)
QM$DevCon        equ     $3c        * offset of the optional configuration table (word)
QM$DevFlags      equ     $40        * device flags, reserved
QM$Opt           equ     $46        * size of the initialisation table (word)
QM$DTyp          equ     $48        * device class, first byte of the initialisation table
*
* -- configuration (init) module
QM$PollSz        equ     $34        * size of the interrupt polling table
QM$DevCnt        equ     $36        * size of the device table
QM$Procs         equ     $38        * initial process table size
QM$Paths         equ     $3a        * initial path table size
QM$SParam        equ     $3c        * offset of the parameter string for the first module
QM$SysGo         equ     $3e        * offset of the name of the first module to run
QM$SysDev        equ     $40        * offset of the default device name
QM$Consol        equ     $42        * offset of the console path name
QM$Extens        equ     $44        * offset of the list of extension module names
QM$Clock         equ     $46        * offset of the clock module name
QM$Slice         equ     $48        * ticks per time slice
QM$Site          equ     $4c        * installation site code
QM$Instal        equ     $50        * offset of the installation name
QM$CPUTyp        equ     $52        * CPU type
QM$OSLvl         equ     $56        * level (byte), version (word), edition (byte) of the system
QM$OSRev         equ     $5a        * offset of the level/revision string
QM$SysPri        equ     $5c        * start priority of the first module
QM$MinPty        equ     $5e        * minimum executable priority
QM$MaxAge        equ     $60        * maximum natural age
QM$MDirSz        equ     $62        * number of module directory entries
QM$Events        equ     $66        * initial event table size
QM$Compat        equ     $68        * compatibility flag byte
QM$Compat2       equ     $69        * second compatibility flag byte (cache snooping)
QM$MemList       equ     $6a        * offset of the list of coloured memory areas
QM$IRQStk        equ     $6c        * size of the kernel interrupt stack in longwords
QM$ColdTrys      equ     $6e        * retry count for the first start step
QM$CacheList     equ     $74        * offset of the cache list (ends with long -1)
QM$IOMan         equ     $76        * offset of the list of I/O manager module names
QM$PreIO         equ     $78        * offset of the list of pre-I/O module names
QM$SysConf       equ     $7a        * system configuration flags
QM$PrcDescStack  equ     $7e        * stack size inside the process descriptor
*
* -- configuration flag bits: compatibility byte
QSlowIRQ         equ     $01        * save all registers on interrupt (obsolete)
QNoStop          equ     $02        * no STOP instruction in the idle loop
QNoGhost         equ     $04        * ignore the sticky bit
QNoBurst         equ     $08        * cache burst off
QZapMem          equ     $10        * pattern-fill memory
QNoClock         equ     $20        * kernel does not start the system clock
QSpurIRQ         equ     $40        * ignore spurious interrupts
QPrivAlm         equ     $80        * only the creator may delete an alarm
*
* -- configuration flag bits: second compatibility byte (cache snooping)
QExtC_I          equ     $01        * external instruction cache snoops
QExtC_D          equ     $02        * external data cache snoops
QOnC_D           equ     $08        * on-chip data cache snoops
ExtCache         equ     QExtC_I+QExtC_D * both external caches
*
* -- configuration flag bits: system configuration byte
QNoTblExp        equ     $01        * table overflow is an error instead of an expansion
QCRCDis          equ     $04        * no CRC check when validating a module
QSysTSDis        equ     $08        * no time slicing in system state
*
* -- cache mode names (memory list / cache list), [alias] = same value as another name here
QCM_WrtProt      equ     $04        * write-protect
QCM_CI           equ     $40        * cache inhibit
QCM_NotSer       equ     $20        * not serialised
QCM_CB           equ     $20        * copy-back [alias]
QWritProt        equ     $04        * write-protect [alias]
QWrtThru         equ     $00        * write-through
QCopyBack        equ     $20        * copy-back [alias]
QCISer           equ     $40        * cache-inhibit, serialised [alias]
QCINotSer        equ     $60        * cache-inhibit, not serialised
*
* Names without a value (the manual gives none, no number table entry): module fields
*   QOnC_I QSSM_NoProt QSSM_SysPT
*   (SSM_SysPT: the documentation gives $20 in a listing but bit 4 in its table: contradictory, left undefined; OnC_I: value not stated literally)
*
* ======================================================================
* SECTION: 6. MODULE TYPES, LANGUAGES, ATTRIBUTES
*   Values of the type byte, the language byte and the attribute bits of the module header.
*   Type and language are usually combined as a word: (type<<8)+language.
* ======================================================================
*
*
* -- module types (QM$Type)
QPrgm            equ     $01        * program module
QSbrtn           equ     $02        * subroutine module
QMulti           equ     $03        * multi-module (reserved)
QData            equ     $04        * data module
QCSDData         equ     $05        * configuration status descriptor
QTrapLib         equ     $0b        * user trap library
QSystm           equ     $0c        * system module
QFlMgr           equ     $0d        * file manager
QDrivr           equ     $0e        * device driver
QDevic           equ     $0f        * device descriptor
Prgrm            equ     QPrgm      * spelling variant used in the documentation examples [alias]
*   (types 0 and 6-10 unnamed: 0 wildcard, 6-10 reserved, 16 and up user-defined; the case variant FlMgr/Flmgr is one name)
*
* -- module languages (QM$Lang)
QObjct           equ     $01        * machine code
QICode           equ     $02        * BASIC intermediate code
QPCode           equ     $03        * Pascal intermediate code
QCCode           equ     $04        * C intermediate code (reserved)
QCblCode         equ     $05        * COBOL intermediate code
QFrtnCode        equ     $06        * FORTRAN intermediate code
*
* -- attribute bits (QM$Attr), high byte of the attr/revision word
QReEnt           equ     $80        * reentrant module (bit 7) (inferred from listing)
QSupstat         equ     $20        * system-state module (bit 5) (inferred from bit number)
QSticky          equ     $40        * sticky module (bit 6) (inferred from bit number; the documentation names the bit but not the constant)
*
* ======================================================================
* SECTION: 7. MODE AND ACCESS BITS
*   Access mode bits for open/create/attach, memory-list access bits and memory types.
* ======================================================================
*
*
* -- access mode bits (open, create, make-directory, attach)
QRead_           equ     $01        * read (bit 0) (inferred from listing sums)
QWrite_          equ     $02        * write (bit 1) (inferred from listing sums)
QUpdat_          equ     $03        * read and write (inferred from listing sums)
QExec_           equ     $04        * execute (bit 2) (inferred from listing sums)
QAppend_         equ     $10        * append (bit 4) (inferred from listing sums)
QISize_          equ     $20        * initial size given (bit 5) (inferred from listing sums)
QShare_          equ     $40        * single user / not sharable (bit 6) (inferred from listing sums)
QDir_            equ     $80        * directory (bit 7) (inferred from listing sums)
*
* -- memory list access bits
QB_USER          equ     $01        * user processes may allocate (ignored with ROM)
QB_PARITY        equ     $02        * parity memory, initialised by the kernel
QB_ROM           equ     $04        * ROM, searched for modules
QB_NVRAM         equ     $08        * non-volatile RAM, searched for modules
QB_SHARE         equ     $10        * shared memory, control structure inside the block
*
* -- memory types
QSYSRAM          equ     $01        * system memory
QVIDEO1          equ     $80        * video plane A
QVIDEO2          equ     $81        * video plane B
*   (file attribute bits 0-7: owner r/w/e, public r/w/e, single user, directory: unnamed in the documentation)
*
* ======================================================================
* SECTION: 8. DESCRIPTOR AND DISK STRUCTURE FIELDS
*   Path descriptor (256 bytes: universal part, manager part, 128-byte option table at $80),
*   device class numbers, disk identification sector, file descriptor, directory entry, driver statics.
*   PD_ offsets are path descriptor offsets; the same option field inside a device descriptor
*   is at (QM$DTyp + (QPD_x - QPD_OPT)).
* ======================================================================
*
*
* -- device classes (value of the class field)
QDT_SCF          equ     $00        * sequential character stream manager
QDT_RBF          equ     $01        * random block manager
QDT_Pipe         equ     $02        * pipe manager
QDT_SBF          equ     $03        * sequential block manager
QDT_NFM          equ     $04        * network file manager
QDT_CDFM         equ     $05        * compact disc file manager
QDT_UCM          equ     $06        * user communications manager
QDT_SOCK         equ     $07        * socket manager
QDT_PTTY         equ     $08        * pseudo-keyboard manager
QDT_INET         equ     $09        * internet interface manager
QDT_NRF          equ     $0a        * non-volatile RAM file manager
QDT_GFM          equ     $0b        * graphics file manager
QDT_ISDN         equ     $0c        * ISDN file manager
QDT_MPFM         equ     $0d        * MPEG file manager
*
* -- path descriptor, universal part
QPD_PD           equ     $00        * path number
QPD_MOD          equ     $02        * access mode
QPD_CNT          equ     $03        * number of paths (obsolete)
QPD_DEV          equ     $04        * device table entry address
QPD_CPR          equ     $08        * process id of the requester
QPD_RGS          equ     $0a        * address of the caller register stack
QPD_BUF          equ     $0e        * address of the data buffer
QPD_USER         equ     $12        * group/user id of the original owner
QPD_PATHS        equ     $16        * list of open paths on the device
QPD_COUNT        equ     $1a        * number of paths using this descriptor
QPD_LProc        equ     $1c        * process id of the last activity
QPD_ErrNo        equ     $20        * error number for C file managers
QPD_SysGlob      equ     $24        * system global pointer for C file managers
QPD_FST          equ     $2a        * file-manager-specific work area
QPD_OPT          equ     $80        * option table (128 bytes)
*
* -- path descriptor option field shared by all managers
QPD_DTP          equ     $80        * device class (first option byte)
*
* -- path descriptor options, random block manager
QPD_DRV          equ     $81        * drive number
QPD_STP          equ     $82        * step rate
QPD_TYP          equ     $83        * disk type / descriptor revision
QPD_DNS          equ     $84        * density
QPD_CYL          equ     $86        * logical cylinders (word)
QPD_SID          equ     $88        * heads/sides
QPD_VFY          equ     $89        * write verify flag
QPD_SCT          equ     $8a        * default sectors per track (word)
QPD_T0S          equ     $8c        * sectors per track on track 0 (word)
QPD_SAS          equ     $8e        * segment allocation size (sectors)
QPD_ILV          equ     $90        * sector interleave
QPD_TFM          equ     $91        * DMA transfer mode
QPD_TOffs        equ     $92        * track base offset
QPD_SOffs        equ     $93        * sector base offset
QPD_SSize        equ     $94        * physical sector size (word)
QPD_Cntl         equ     $96        * control word
QPD_Trys         equ     $98        * retry count
QPD_LUN          equ     $99        * SCSI logical unit
QPD_WPC          equ     $9a        * first write-precompensation cylinder (word)
QPD_RWR          equ     $9c        * first reduced-write-current cylinder (word)
QPD_Park         equ     $9e        * park cylinder (word)
QPD_LSNOffs      equ     $a0        * logical sector offset (long)
QPD_TotCyls      equ     $a4        * physical cylinders (word)
QPD_CtrlrID      equ     $a6        * SCSI controller id
QPD_Rate         equ     $a7        * rotation and transfer rate
QPD_ScsiOpt      equ     $a8        * SCSI options (long)
QPD_MaxCnt       equ     $ac        * maximum transfer length per call (long)
QPD_ATT          equ     $b5        * file attributes
QPD_FD           equ     $b6        * sector number of the file descriptor (3 bytes)
QPD_DFD          equ     $ba        * sector number of the directory file descriptor
QPD_DCP          equ     $be        * directory entry position
QPD_DVT          equ     $c2        * copy of the device table pointer
QPD_SctSiz       equ     $c8        * logical sector size (0 = 256)
QPD_NAME         equ     $e0        * file name (also used by the pipe manager)
PD_TOS           equ     QPD_T0S    * spelling variant in the option table listing [alias]
*
* -- random block manager: density values
QSingle          equ     $00        * density: single (FM)
QDouble          equ     $01        * density: double (MFM)
QQuad            equ     $02        * density: double track density
QOctal           equ     $04        * density: quad track density
*
* -- random block manager: disk type values
QFive            equ     $00        * type: 5.25 inch (descriptor listing name)
QEight           equ     $01        * type: 8 inch (descriptor listing name)
QSizeOld         equ     $00        * type: old size code
QSize8           equ     $02        * type: 8 inch
QSize5           equ     $04        * type: 5.25 inch
QSize3           equ     $06        * type: 3.5 inch
QHRemov          equ     $40        * type: removable hard disk
QHard            equ     $80        * type: hard disk
*
* -- random block manager: rotation speed values
Qrpm300          equ     $00        * 300 rpm
Qrpm360          equ     $01        * 360 rpm
Qrpm600          equ     $02        * 600 rpm
*
* -- random block manager: transfer rate values
Qxfr125K         equ     $00        * 125K bit/s
Qxfr250K         equ     $10        * 250K bit/s
Qxfr300K         equ     $20        * 300K bit/s
Qxfr500K         equ     $30        * 500K bit/s
Qxfr1M           equ     $40        * 1M bit/s
Qxfr2M           equ     $50        * 2M bit/s
Qxfr5M           equ     $60        * 5M bit/s
*
* -- random block manager: control word bits
QFmtEnabl        equ     $00        * control: formatting enabled
QFmtDsabl        equ     $01        * control: formatting disabled
QMultDsabl       equ     $00        * control: multi-sector I/O disabled
QMultEnabl       equ     $02        * control: multi-sector I/O enabled
QStabDsabl       equ     $00        * control: stable id off
QStabEnabl       equ     $04        * control: stable id on
QAutoDsabl       equ     $00        * control: size not autodetected
QAutoEnabl       equ     $08        * control: size from status call
QFTrkDsabl       equ     $00        * control: any track format
QFTrkEnabl       equ     $10        * control: single-track format only
QWritEnab        equ     $00        * control: writing allowed
QWritDsabl       equ     $20        * control: write-protected by the manager
*
* -- SCSI option bits
Qscsi_atn        equ     $01        * SCSI: disconnect allowed
Qscsi_target     equ     $02        * SCSI: target capable
Qscsi_synchr     equ     $04        * SCSI: synchronous
Qscsi_parity     equ     $08        * SCSI: parity
*   (within the above constant groups, zero values repeat by design: they belong to different fields or to the "off" state)
*
* -- path descriptor options, character stream manager
QPD_UPC          equ     $81        * force upper case
QPD_BSO          equ     $82        * backspace option
QPD_DLO          equ     $83        * delete-line option
QPD_EKO          equ     $84        * echo on/off
QPD_ALF          equ     $85        * auto line feed after CR
QPD_NUL          equ     $86        * null count after end of line
QPD_PAU          equ     $87        * page pause on/off
QPD_PAG          equ     $88        * page length (lines)
QPD_BSP          equ     $89        * backspace input character
QPD_DEL          equ     $8a        * delete-line character
QPD_EOR          equ     $8b        * end-of-record character
QPD_EOF          equ     $8c        * end-of-file character
QPD_RPR          equ     $8d        * reprint-line character
QPD_DUP          equ     $8e        * duplicate-line character
QPD_PSC          equ     $8f        * pause character
QPD_INT          equ     $90        * keyboard interrupt character
QPD_QUT          equ     $91        * keyboard quit character
QPD_BSE          equ     $92        * backspace echo character
QPD_OVF          equ     $93        * line overflow (bell) character
QPD_PAR          equ     $94        * parity, stop bits, bits per character
QPD_BAU          equ     $95        * baud rate code
QPD_D2P          equ     $96        * offset of the output device name (word)
QPD_XON          equ     $98        * X-ON character
QPD_XOFF         equ     $99        * X-OFF character
QPD_TAB          equ     $9a        * tab character
QPD_TABS         equ     $9b        * tab width
QPD_TBL          equ     $9c        * visible copy of the device table entry (long)
QPD_Col          equ     $a0        * current column
QPD_Err          equ     $a2        * last I/O error status
*
* -- path descriptor options, sequential block manager
QPD_TDrv         equ     $81        * tape drive number
QPD_SBF          equ     $82        * reserved
QPD_NumBlk       equ     $83        * maximum number of buffers (0 = unbuffered)
QPD_BlkSiz       equ     $84        * logical block size (long)
QPD_Prior        equ     $88        * priority of the helper process
QPD_SBFFlags     equ     $8a        * path flags (word)
QPD_DrivFlag     equ     $8b        * driver flags
QPD_DMAMode      equ     $8c        * DMA mode
QPD_ScsiID       equ     $8e        * SCSI controller id
QPD_ScsiLUN      equ     $8f        * SCSI logical unit
QPD_ScsiOpts     equ     $90        * SCSI options
PD_Flags         equ     QPD_SBFFlags * the documentation uses both names for the flag word [alias]
Qf_rest_b        equ     $00        * flag bit: rewind on close
Qf_offl_b        equ     $01        * flag bit: drive offline
Qf_eras_b        equ     $02        * flag bit: erase to end of tape
*
* -- path descriptor options, pipe manager (the name field is the same as in the block manager option list)
QPD_BufSz        equ     $82        * default FIFO buffer size
QPD_IOBuf        equ     $86        * small default I/O buffer
*
* -- identification sector (logical sector 0) of a random block medium
QDD_TOT          equ     $00        * total sectors (3 bytes)
QDD_TKS          equ     $03        * sectors per track
QDD_MAP          equ     $04        * bytes in the allocation map (word)
QDD_BIT          equ     $06        * sectors per bit (word)
QDD_DIR          equ     $08        * sector of the root directory descriptor (3 bytes)
QDD_OWN          equ     $0b        * owner id (word)
QDD_ATT          equ     $0d        * attributes
QDD_DSK          equ     $0e        * disk id (word)
QDD_FMT          equ     $10        * format flags
QDD_SPT          equ     $11        * sectors per track (word)
QDD_RES          equ     $13        * reserved (word)
QDD_BT           equ     $15        * boot sector (3 bytes, 0 = none)
QDD_BSZ          equ     $18        * boot size (word)
QDD_DAT          equ     $1a        * creation date (5 bytes)
QDD_NAM          equ     $1f        * volume name (32 bytes)
QDD_OPT          equ     $3f        * path descriptor options (32 bytes)
QDD_SYNC         equ     $60        * media integrity code (long)
QDD_MapLSN       equ     $64        * first sector of the allocation map (long, 0 = sector 1)
QDD_LSNSize      equ     $68        * logical sector size (word, 0 = 256)
QDD_VersID       equ     $6a        * version id of sector 0 (word)
*
* -- identification sector constant
QDD_SIZ          equ     $15        * bytes copied from sector 0 into the drive table (a size, not an offset)
*
* -- file descriptor sector and directory entry
QFD_ATT          equ     $00        * attributes
QFD_OWN          equ     $01        * owner id (word)
QFD_DAT          equ     $03        * last-modified date (5 bytes)
QFD_LNK          equ     $08        * link count
QFD_SIZ          equ     $09        * file size (long)
QFD_CREAT        equ     $0d        * creation date (3 bytes)
QFD_SEG          equ     $10        * segment list (240 bytes, 5-byte entries)
QDIR_NM          equ     $00        * file name (28 bytes, last character has bit 7 set)
QDIR_FD          equ     $1d        * sector of the file descriptor (3 bytes)
*
* -- random block manager drive table (driver/manager maintained fields)
QV_TRAK          equ     $16        * current track
QV_FileHd        equ     $18        * list of open files
QV_DiskID        equ     $1c        * disk id
QV_BMapSz        equ     $1e        * bitmap size
QV_MapSct        equ     $20        * lowest bitmap sector to search
QV_BMB           equ     $22        * bitmap in use flag
QV_ScZero        equ     $24        * pointer to sector 0
QV_ZeroRd        equ     $28        * sector 0 read flag
QV_Init          equ     $29        * drive initialised
QV_Resbit        equ     $2a        * reserved bitmap sector number
QV_SoftEr        equ     $2c        * recoverable error count
QV_HardEr        equ     $30        * unrecoverable error count
QV_Cache         equ     $34        * cache queue head
QV_DText         equ     $38        * pointer to a drive table extension
QV_MapMax        equ     $3c        * highest bitmap sector
QV_MapOffs       equ     $3e        * bitmap sector offset
*
* -- driver static storage, common start of all managers
QV_PORT          equ     $00        * port address
QV_LPRC          equ     $04        * last active process id
QV_BUSY          equ     $06        * active process id
QV_WAKE          equ     $08        * process to wake on completion
QV_Paths         equ     $0a        * list of open paths
*
* -- driver static storage, character stream manager
QV_DEV2          equ     $2e        * address of the attached output static storage
QV_TYPE          equ     $32        * device type
QV_LINE          equ     $33        * lines left on page
QV_PAUS          equ     $34        * pause request
QV_INTR          equ     $35        * interrupt character
QV_QUIT          equ     $36        * quit character
QV_PCHR          equ     $37        * pause character
QV_ERR           equ     $38        * accumulated errors
QV_XON           equ     $39        * X-ON character
QV_XOFF          equ     $3a        * X-OFF character
QV_Hangup        equ     $46        * hang-up flag
*
* -- driver static storage, sequential block manager
QSBF_NDRV        equ     $30        * number of drives
QSBF_Flag        equ     $32        * flags
QSBF_Drvr        equ     $34        * driver address
QSBF_DPrc        equ     $38        * driver process
QSBF_IPrc        equ     $3c        * input process
*
* -- sequential block manager drive table
QSBF_DFlg        equ     $00        * drive flags
QSBF_NBuf        equ     $02        * number of buffers
QSBF_IBH         equ     $04        * input buffer head
QSBF_IBT         equ     $08        * input buffer tail
QSBF_OBH         equ     $0c        * output buffer head
QSBF_OBT         equ     $10        * output buffer tail
QSBF_Wait        equ     $14        * wait state
QSBF_SErr        equ     $18        * soft errors
QSBF_HErr        equ     $1c        * hard errors
*
* Names without a value (the manual gives none, no number table entry): status codes for the get/set status calls (no numeric value anywhere)
*   QSS_CDFD QSS_DevNam QSS_EOF QSS_FD QSS_FDInf QSS_Free QSS_Opt QSS_Pos
*   QSS_Ready QSS_Size QSS_VarSect QSS_DSize QSS_ELog QSS_Attr QSS_Close
*   QSS_DCOff QSS_DCOn QSS_DsRTS QSS_EnRTS QSS_Feed QSS_Lock QSS_Open
*   QSS_Relea QSS_Reset QSS_RFM QSS_Skip QSS_SSig QSS_Ticks QSS_WFM QSS_WTrk
*   QSS_Break QSS_SQD QSS_Reten QSS_RsBit
*
* Names without a value (the manual gives none, no number table entry): process descriptor fields and system globals (no offsets given)
*   QP$ID QP$State QP$Signal QP$DIO QP$Preempt QP$sp QD_MinPty QD_MaxAge
*   QD_Proc QD_SysPrc QD_SnoopD QD_TSlice QD_SysMin QR$cc QR$d1 QR$d2 QR$PC
*   QR$a7 QPD_DTB QV_NDRV QV_DRVBEG
*
* ======================================================================
* SECTION: 9. SIGNALS, VECTORS, LIMITS
*   Signal codes; vector numbering and limits are described below, but the documentation gives no names for them.
* ======================================================================
*
*
* -- signals
QS$Kill          equ     $00        * unconditional kill (cannot be caught or masked)
QS$Wake          equ     $01        * wake up (not caught, not queued)
QS$Abort         equ     $02        * keyboard abort
QS$Intrpt        equ     $03        * keyboard interrupt
QS$HangUp        equ     $04        * modem hang-up
QS$Deadly        equ     $20        * signals below this value are deadly to I/O
S$Intrp          equ     QS$Intrpt  * spelling variant in the driver manual [alias]
*
* Names without a value (the manual gives none, no number table entry): signal/panic/trap/character names
*   QS$Quit QK$Idle QK$PFail QT_TRAPV QT_CHK QC$Bsp QC$Del QC$CR QC$EOF
*   QC$Rprt QC$Rpet QC$Paus QC$Intr QC$Quit QC$Bell QC$XOn QC$XOff QC$Tab
*
* Facts without names (documented numbers only):
*   signals 5-31 reserved/deadly to I/O (26-31 user-defined), 32-255 reserved, 256-65535 user-defined.
*   vectors: 0 reset SSP, 1 reset PC, 2-11 exceptions, 24 spurious, 25-31 autovectors level 1-7,
*   32 = user trap 0 (system call), 33-47 = user traps 1-15, 57-63 on-chip autovectors (one CPU variant),
*   64-255 vectored interrupts.  Processor exception error code = vector + 100.
*   Limits: event name 11 characters, event record 32 bytes, path descriptor 256 bytes (options 128),
*   option table max 128 bytes, priorities 0-65535, service request codes 0-255, module CRC 24 bit.
*
