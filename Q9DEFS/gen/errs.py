ERR_EN = """
E_SIGABRT|abort signal
E_SIGFPE|erroneous math operation
E_SIGILL|illegal function image
E_SIGSEGV|segment violation (bus error)
E_SIGTERM|termination request
E_SIGALRM|alarm time elapsed
E_SIGPIPE|write to a pipe without reader
E_SIGUSR1|user signal 1
E_SIGUSR2|user signal 2
E_SIGADDR|address error
E_SIGCHK|CHK instruction
E_SIGTRAPV|TRAPV instruction
E_SIGPRIV|privilege violation
E_SIGTRACE|trace exception
E_SIG1010|line-A exception
E_SIG1111|line-F exception
E$IllFnc|illegal function code (math trap handler)
E$FmtErr|format error in ASCII-to-number conversion
E$NotNum|no number found
E$IllArg|illegal argument
E$BusErr|bus error
E$AdrErr|address error
E$IllIns|illegal instruction
E$ZerDiv|division by zero
E$Chk|CHK/CHK2 exception
E$TrapV|TRAPV/TRAPcc exception
E$Violat|privilege violation
E$Trace|uninitialised trace exception
E$1010|line-A emulator exception
E$1111|line-F emulator exception
E$Resrvd|reserved exception
E$CProto|coprocessor protocol violation
E$StkFmt|stack frame format error
E$UnIRQ|uninitialised interrupt
E$FPUnordC|FPU: unordered condition
E$FPInxact|FPU: inexact result
E$FPDivZer|FPU: division by zero
E$FPUndrFl|FPU: underflow
E$FPOprErr|FPU: operand error
E$FPOverFl|FPU: overflow
E$FPNotNum|FPU: not a number
E$UnData|FPU: unimplemented data type
E$MMUConf|memory-management unit: configuration error
E$MMUIlleg|memory-management unit: illegal operation
E$MMUAcces|memory-management unit: access level violation
E$Permit|no permission (superuser needed)
E$Differ|arguments differ (memory-check call)
E$StkOvf|stack overflow
E$EvntID|invalid event id
E$EvNF|event name not found
E$EvBusy|event busy
E$EvParm|impossible event parameter
E$Damage|system data structure damaged
E$BadRev|incompatible revision
E$PthLost|path lost
E$BadPart|bad or no active partition
E$Hardware|hardware damage detected
E$SectSize|invalid sector size
E$BSig|unexpected or invalid signal
E$PthFul|path table full
E$BPNum|bad path number
E$Poll|interrupt polling table full
E$BMode|bad mode
E$DevOvf|device table full
E$BMID|bad module header
E$DirFul|module directory full
E$MemFul|memory full
E$UnkSvc|unknown service code
E$ModBsy|module busy
E$BPAddr|bad memory address (boundary error)
E$EOF|end of file
E$VctBsy|interrupt vector busy
E$NES|non-existing segment
E$FNA|file not accessible
E$BPNam|bad path name
E$PNNF|path name not found
E$SLF|segment list full
E$CEF|file already exists
E$IBA|illegal block address
E$HangUp|modem carrier lost
E$MNF|module not found
E$NoClk|no clock
E$DelSP|stack memory release requested
E$IPrcID|illegal process id
E$Param|illegal parameter
E$NoChld|no child processes
E$ITrap|illegal trap code
E$PrcAbt|process aborted
E$PrcFul|process table full
E$IForkP|illegal parameter area
E$KwnMod|module already known
E$BMCRC|bad module CRC
E$USigP|unprocessed signal pending
E$NEMod|module not executable
E$BNam|bad name
E$BMHP|bad module header parity
E$NoRAM|no free system RAM
E$DNE|directory not empty
E$NoTask|no free task number
E$Unit|illegal drive number
E$Sect|bad sector
E$WP|write protected
E$CRC|CRC error
E$Read|read error
E$Write|write error
E$NotRdy|not ready
E$Seek|seek error
E$Full|medium full
E$BTyp|bad type
E$DevBsy|device busy
E$DIDC|disk id changed
E$Lock|record locked
E$Share|non-sharable file busy
E$DeadLk|I/O deadlock
E$Format|device format-protected
ERANGE|number out of range (ANSI C)
EDOM|number not in domain (ANSI C)
E$IllPrm|graphics/audio subsystem: illegal parameter
E$IdFull|id table full
E$BadSiz|bad size
E$RgFull|region definition full
E$UnID|unallocated id number
E$NullRg|null region
E$BadMod|bad drawmap or pattern mode
E$NoFont|no active font
E$NoDM|no drawmap
E$NoPlay|no audio playback active
E$Abort|audio record/playback aborted
E$QFull|audio queue full
E$Busy|audio processor busy
E_RES_NOSLOT|no free resource slot
E_RES_BADSLOT|bad resource slot
E_RES_NOSHARE|resource not sharable
E_RES_NOTYPE|wrong resource type
E_RES_NORES|wrong resource id
E_REQ_NOITEMS|no items for request
E_REQ_BADITEM|item number out of range
E_REQ_BADCOLS|column number out of range
E_REQ_BADPTR|bad item array pointer
E_REQ_NOCREATE|request cannot be created
E_REQ_TIMEOUT|modal request timed out
E_REQ_NOSEL|no selection made
E_REQ_DEFID|bad definition function id
E_REQ_DEFACT|bad definition action code
E_REQ_STATE|bad item state
E_REQ_BADRECT|bad request rectangle
E_CNT_BHVID|bad standard behaviour id
E_CNT_DEFID|bad standard definition id
E_CNT_DEFACT|bad action for definition function
E_CNT_BHVACT|bad action for behaviour function
E_CNT_STATE|bad control state
E_CNT_PART|bad control part code
E_CNT_FLAGS|bad flags
E_CNT_MINMAX|bad minimum/maximum/value
E_CNT_TYPE|bad control type
E_CLIP_DEV|clipboard device not in preferences
E_CLIP_FULL|clipboard full
E_CLIP_TYPE|type not in clipboard
E_CLIP_ACC|clipboard not open for access
E_CLIP_CNT|type offset larger than type count
E_CLIP_OPEN|clipboard not open
E_CLIP_INIT|clipboard not initialised
E_CLIP_CLOSE|clipboard not closed
E_CLIP_RW|rewrite impossible, type missing
E_HNDLR_UNKNOWN|unknown handler
E_ATABL_NOENTRY|no entry found
E_BOX_TABLE|line table overflows
E_BOX_COUNT|text too long (max 65535)
E_BOX_TYPE|bad or unimplemented type
E_BOX_MAXL|line too long
E_BOX_NOTAB|line table required
E_BOX_NOFONT|font not set in drawmap
E_BOX_RECT|bad rectangle
E_INIT_VARERROR|error in global variable
E_INTER_NOMOD|no preference module
E_INTER_ILLARG|illegal argument
E_OVL_BADRECT|bad overlay rectangle
E_OVL_NOTTOP|overlay not on top of stack
E_OVL_UNKNOWN|unknown overlay
E_IND_DEFID|bad definition id
E_IND_DEFACT|bad definition action
E_IND_MINMAX|bad minimum/maximum/value
E_IND_BADCOORDS|bad coordinates
E_IND_NOCREATE|indicator not created
E_IND_BADFLAGS|bad flags
E_IND_BADPTR|bad pointer
E_IND_BADDISP|bad displacement
EWOULDBLOCK|I/O would block
EINPROGRESS|I/O in progress
EALREADY|operation already in progress
EDESTADDRREQ|destination address required
EMSGSIZE|message too long
EPROTOTYPE|wrong protocol for socket
ENOPROTOOPT|bad protocol option
EPROTONOSUPPORT|protocol not supported
ESOCKNOSUPPORT|socket type not supported
EOPNOTSUPPORT|operation not supported on socket
EPFNOSUPP|protocol family not supported
EAFNOSUPPORT|address family not supported
EADDRINUSE|address in use
EADDRNOTAVAIL|address not assignable
ENETDOWN|network down
ENETUNREACH|network unreachable
ENETRESET|network dropped connection on reset
ECONNABORTED|connection aborted by software
ECONNRESET|connection reset by peer
ENOBUFS|no buffer space
EISCONN|socket already connected
ENOTCONN|socket not connected
ESHUTDOWN|cannot send after shutdown
ETOOMANYREFS|too many references
ETIMEDOUT|connection timed out
ECONNREFUSED|connection refused
EBUFTOOSMALL|buffer too small (memory-buffer call)
ESMODEXISTS|socket module already attached
ENOTSOCK|path is not a socket
EHOSTUNREACH|no route to host
EHOSTDOWN|host down
E$LnkDwn|link down / layer-1 error on attach
E$Conn|connection error
E$RxThread|error in receive thread
E$ME|management entity error
E$SAPI|unknown SAPI
E$TEI|TEI error
E$Max_TEI|maximum terminal endpoints in use
E$TState|illegal layer-2 state
E$TEI_Denied|TEI initialisation denied
E$Primitive|unknown primitive
E$L2In|layer-2 error on incoming message
E$Peer_Busy|peer busy
E$K|maximum outstanding messages
E$MaxCRef|maximum call references in use
E$CRef|call reference does not exist
E$CallProg|call progress error
E$Rcvr|receiver allocation/removal error
E$REQDENIED|request denied by peer
E$RXSTART|receive thread not started
E$NOSTACK|last driver in path stack
E$BTMSTK|attempt to remove last driver
E$NPBNULL|notify parameter block is NULL
E$PPS_NOTFND|per-path storage not found
E$STKFULL|path stack full
E$MBNOTINST|memory-buffer module not installed
E$TMRNTFND|timer not found
E$GETIME|time error
E$TIMERINT|timer interrupt
E$RXMB_NODEVENTRY|no device entry in buffer
E$PGM+TBLBSY|program/service table in use
E$TBLOVF|too many tables
E$PGM_TBLNFND|table not found
E$PGM_NFND|program not found
E$NOPLAY|no program running
E$NODNDRVR|no down driver
E$RXMB_ERR|receive data error (base)
""".strip().splitlines()
