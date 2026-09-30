# data: name -> (english meaning). numbers come from NUMS.md
CALLS = """
AProc|put a process into the active queue (system)
Alarm|set, cancel or query an alarm
AllBit|mark bits as allocated in a bitmap
AllPD|allocate a process/path descriptor (system)
AllPrc|allocate a process descriptor (system)
AllTsk|prepare protection hardware for a task (memory-protection unit)
CCtl|cache control
CRC|compute a CRC over a data block
Chain|replace the calling process by a new primary module (no return)
ChkMem|check access rights to a memory area (memory-protection unit)
CmpNam|compare two names
CpyMem|copy memory of another process
DExec|run a debugged child for some instructions
DExit|terminate a debugged child
DFork|create a process under debugger control
DatMod|create a data module
DelBit|mark bits as free in a bitmap
DelPrc|release a process descriptor (system)
DelTsk|release protection structures of a task (memory-protection unit)
Event|create, use or delete an event (sub-code QEv$...)
Exit|terminate the calling process
FModul|module directory lookup (listed in the call table only)
FindPD|find a process/path descriptor (system)
Fork|create a child process
GBlkMp|get a map of free memory blocks
GModDr|copy the module directory
GPrDBT|copy the process descriptor block table
GPrDsc|copy a process descriptor
GProcP|get a process descriptor pointer (system)
GSPUMp|get access-rights status (memory-protection unit)
Gregor|Julian to Gregorian date conversion
ID|get process id, user id and priority
IODel|check whether an I/O module is in use (system)
IOQu|enter a process into an I/O queue (system)
IRQ|add or remove an interrupt polling entry (system)
Icpt|set up a signal intercept routine
Julian|Gregorian to Julian date conversion
Link|link to a module in memory by name
Load|load modules from a file
Mem|grow or shrink the data area
Move|copy data between address spaces (system)
NProc|start the next process (system, no return)
PErr|print an error message
Panic|report a system catastrophe (system)
Permit|grant a process access to memory
Protect|revoke access to memory
PrsNam|parse a path name
RTE|return from an interrupt exception
RetPD|return a process/path descriptor (system)
SPrior|set a process priority
SRqCMem|request coloured system memory
SRqMem|request system memory
SRtMem|return system memory
SSpd|suspend a process
SSvc|install service-request table entries (system)
STime|set system date and time
STrap|set the error-trap handler
SUser|set the user id
SchBit|search a bitmap for a free area
Send|send a signal
SetCRC|write a valid CRC into a module
SetSys|set or read a system global variable
Sleep|put the process to sleep
SysDbg|call the system debugger
SysID|get system identification data
TLink|install a user-trap handler module
Time|get system date and time
Trans|translate a memory address
UAcct|user accounting hook
UnLink|unlink a module by address
UnLoad|unload a module by name
VModul|validate a module (system)
Wait|wait for a child process
""".strip().splitlines()
IOCALLS = """
Attach|attach a device to the system
ChgDir|change the working directory
Close|close a path
Create|create a new file
Delete|delete a file
Detach|detach a device from the system
Dup|duplicate a path
GetStt|get file/device status
MakDir|create a directory
Open|open a path
Read|read data
ReadLn|read a text line with editing
SGetSt|listed in the call table only (no description)
Seek|position the file pointer
SetStt|set file/device status
WritLn|write a text line with editing
Write|write data
""".strip().splitlines()
