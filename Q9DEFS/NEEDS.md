# NEEDS: Header-Bedarf der Q9-Tools (System/*, Network/*)

Phase 1 (Bedarfsermittlung). Quelle: 76 vorverarbeitete Verbraucherdateien (qcpp gegen eigene C-Standardheader, alle uebrigen Header als leere Stubs), ausgewertet per Token-Analyse; keine Original-Header gelesen. Zuordnung Bestandteil->Header nur aus den `#include`-Listen der Verbraucher (Schnitt der Include-Mengen ueber alle Dateien, die den Bestandteil brauchen). **Vorsicht:** Ein eindeutiger Schnitt heisst nur "erreichbar ueber diesen Header"; Originalheader binden sich womoeglich gegenseitig ein (z. B. netdb.h -> netinet/in.h), die endgueltige Zuordnung Bestandteil->Header muss die Spezifikationsphase aus der Herstellerdokumentation treffen. Unsere eigenen Std-Header (stdio.h, string.h, ...) sind nur fuer C89-Namen (malloc, free) als Kandidaten zugelassen; alle anderen fehlenden Namen sind nach unserer Header-Lage nicht dort zu finden.

**Keine Werte, Layouts oder Feldreihenfolgen sind hier angegeben** - nur was aus der Nutzung folgt.

## Zusammenfassung

| Header (bzw. Kandidatengruppe) | Anzahl Bestandteile | Anzahl Verbraucherdateien |
|---|---|---|
| MEHRDEUTIG: dir.h | direct.h | modes.h | module.h | 74 | 1 |
| module.h | 35 | 9 |
| SPF/BSD/netdb.h | 27 | 20 |
| process.h | 20 | 2 |
| MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/protocols/bootp.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | 15 | 2 |
| UNIX/stat.h | 14 | 3 |
| RPC/rpc.h | 13 | 3 |
| MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | modes.h | 12 | 5 |
| MEHRDEUTIG: SPF/BSD/net/if.h | SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/ioctl.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/sockio.h | SPF/BSD/sys/types.h | modes.h | 10 | 1 |
| MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | 10 | 15 |
| MEHRDEUTIG: UNIX/stat.h | dir.h | 10 | 1 |
| MEHRDEUTIG: modes.h | rbf.h | 10 | 2 |
| SPF/BSD/netinet/in.h | 9 | 18 |
| modes.h | 9 | 18 |
| MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/types.h | SPF/RPC/pmap_clnt.h | SPF/RPC/pmap_prot.h | SPF/RPC/rpc.h | 8 | 2 |
| MEHRDEUTIG: RPC/rpc.h | SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/types.h | SPF/RPC/pmap_clnt.h | SPF/RPC/pmap_prot.h | SPF/RPC/rpc.h | 7 | 6 |
| MEHRDEUTIG: SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | modes.h | 5 | 7 |
| MEHRDEUTIG: memory.h | module.h | 5 | 1 |
| MEHRDEUTIG: modes.h | module.h | 5 | 3 |
| MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | 4 | 8 |
| MEHRDEUTIG: SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | 4 | 11 |
| SPF/RPC/rpc.h | 4 | 4 |
| MEHRDEUTIG: modes.h | sg_codes.h | 4 | 2 |
| MEHRDEUTIG: RPC/rpc.h | SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/protocols/bootp.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | SPF/RPC/pmap_clnt.h | SPF/RPC/pmap_prot.h | SPF/RPC/rpc.h | 3 | 7 |
| MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/types.h | SPF/RPC/pmap_clnt.h | SPF/RPC/rpc.h | 3 | 4 |
| setsys.h | 3 | 1 |
| KEIN KANDIDAT | 2 | 2 |
| MEHRDEUTIG: stdio.h | stdlib.h | string.h | 2 | 7 |
| MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | dir.h | direct.h | memory.h | modes.h | module.h | process.h | rbf.h | sg_codes.h | types.h | 1 | 12 |
| MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | dir.h | direct.h | events.h | memory.h | modes.h | module.h | process.h | rbf.h | sg_codes.h | types.h | 1 | 18 |
| events.h | 1 | 1 |

Gesamt: 330 Bestandteile in 31 Gruppen (eindeutig zugeordnet: 10 Header mit 135 Bestandteilen).


## MEHRDEUTIG: dir.h | direct.h | modes.h | module.h

74 Bestandteile, 1 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct dirent` | Typ (struct-Tag) | qid | Struktur-Tag; benutzte Felder: dir_addr, dir_name |
| `struct modhcom` | Typ (struct-Tag) | qid | Struktur-Tag; benutzte Felder: _maccess, _mattrev, _medit, _mname, _msize, _msync, _mtylan |
| `mh_fman` | Typ (typedef-Name) | qid | Typname; als Struktur benutzt, Felder: _mexcpt, _mexec |
| `mod_config` | Typ (typedef-Name) | qid | Typname; als Struktur benutzt, Felder: _mclock, _mconsol, _mcputyp, _mioman, _mmdirsz, _mpaths, _mprocs, _mslice, _mstacksz, _msysconf, _msysdrive, _msysgo, _msyspri |
| `mod_dev` | Typ (typedef-Name) | qid | Typname; als Struktur benutzt, Felder: _mdevcon, _mdtype, _mfmgr, _mirqlvl, _mmode, _mopt, _mpdev, _mport, _mpriority, _mvector |
| `mod_dir` | Typ (typedef-Name) | qid | Typname; als Struktur benutzt, Felder: md_mptr |
| `mod_driver` | Typ (typedef-Name) | qid | Typname; als Struktur benutzt, Felder: _mdata, _mderror, _mdgetstat, _mdinit, _mdread, _mdsetstt, _mdterm, _mdwrite, _mexcpt, _mexec |
| `mh_fman._mexcpt` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mh_fman._mexec` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_config._mclock` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_config._mconsol` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_config._mcputyp` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf long |
| `mod_config._mioman` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_config._mmdirsz` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_config._mpaths` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_config._mprocs` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_config._mslice` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_config._mstacksz` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_config._msysconf` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_config._msysdrive` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_config._msysgo` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_config._msyspri` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_dev._mdevcon` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned short |
| `mod_dev._mdtype` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_dev._mfmgr` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned short |
| `mod_dev._mirqlvl` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_dev._mmode` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_dev._mopt` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_dev._mpdev` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned short |
| `mod_dev._mport` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned long |
| `mod_dev._mpriority` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_dev._mvector` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_dir.md_mptr` | Struct.Feld | qid | Zugriff mit "." auf Wert; Ausdruck; Zugriff mit "." auf Wert; verglichen == mit int |
| `mod_driver._mdata` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_driver._mderror` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_driver._mdgetstat` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_driver._mdinit` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_driver._mdread` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_driver._mdsetstt` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_driver._mdterm` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_driver._mdwrite` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_driver._mexcpt` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_driver._mexec` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `struct dirent.dir_addr` | Struct.Feld | qid | Zugriff mit "->" auf Zeiger; verglichen == mit int; Bedingung |
| `struct dirent.dir_name` | Struct.Feld | qid | Zugriff mit "->" auf Zeiger; Argument 1 von decode_dirname |
| `struct modhcom._maccess` | Struct.Feld | qid | Zugriff mit "->" auf Zeiger; gecastet auf unsigned int |
| `struct modhcom._mattrev` | Struct.Feld | qid | Zugriff mit "->" auf Zeiger; Operand von ">>"; gecastet auf unsigned int; Zugriff mit "->" auf Zeiger; Operand von "&"; gecastet auf unsigned int |
| `struct modhcom._medit` | Struct.Feld | qid | Zugriff mit "->" auf Zeiger; gecastet auf unsigned int |
| `struct modhcom._mname` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `struct modhcom._msize` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32; Zugriff mit "->" auf Zeiger; gecastet auf u_int32 |
| `struct modhcom._msync` | Struct.Feld | qid | Zugriff mit "." auf Wert; verglichen != mit const MODSYNC; Bedingung; Zugriff mit "." auf Wert; verglichen != mit const MODSYNC |
| `struct modhcom._mtylan` | Struct.Feld | qid | Zugriff mit "->" auf Zeiger; Operand von ">>"; gecastet auf unsigned int; Zugriff mit "->" auf Zeiger; Operand von "&"; gecastet auf unsigned int; Zugriff mit "." auf Wert; Operand von ">>"; gecastet auf unsigned int |
| `DIRBLKSIZ` | Konstante/Makro | qid | Ausdruck |
| `MA_REENT` | Konstante/Makro | qid | Bitmaske |
| `MA_SUPER` | Konstante/Makro | qid | Bitmaske |
| `ML_ANY` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `ML_CBLCODE` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `ML_CCODE` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `ML_FRTNCODE` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `ML_ICODE` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `ML_JAVACODE` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `ML_OBJECT` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `ML_PCODE` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_ANY` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_CSDDATA` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_DATA` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_DEVDESC` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_DEVDRVR` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_FILEMAN` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_MULTI` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_PROGRAM` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_SUBROUT` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_SYSTEM` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |
| `MT_TRAPLIB` | Konstante/Makro | qid | case-Marke (ganzzahlig konstant) |

## module.h

35 Bestandteile, 9 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct mod_dir` | Typ (struct-Tag) | mdir | Struktur-Tag; benutzte Felder: md_group, md_link, md_mchk, md_mptr, md_static |
| `mh_com` | Typ (typedef-Name) | dump, fixmod, link, load, save, unlink | Typname; als Struktur benutzt, Felder: _maccess, _mattrev, _medit, _msize, _msync |
| `u_int16` | Typ (typedef-Name) | dump, link, load, qid, save, unlink | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `mod_exec` | Typ (typedef-Name) | ident, qid | Typname; als Struktur benutzt, Felder: _maccess, _mattrev, _mdata, _medit, _mexcpt, _mexec, _mh, _midata, _midref, _mname, _msize, _mstack, _msysrev, _mtylan |
| `mh_exec` | Typ (typedef-Name) | fixmod | Typname; als Struktur benutzt, Felder: _mstack |
| `mh_com._msize` | Struct.Feld | dump, save | Zugriff mit "->" auf Zeiger; gecastet auf size_t; Zugriff mit "->" auf Zeiger; Ausdruck; Zugriff mit "->" auf Zeiger; Operand von "-"; gecastet auf long |
| `mh_com._maccess` | Struct.Feld | fixmod | Zugriff mit "->" auf Zeiger; zugewiesen: short |
| `mh_com._mattrev` | Struct.Feld | fixmod | Zugriff mit "->" auf Zeiger; zugewiesen: ?expr; Zugriff mit "->" auf Zeiger; Operand von "&" |
| `mh_com._medit` | Struct.Feld | fixmod | Zugriff mit "->" auf Zeiger; zugewiesen: short |
| `mh_com._msync` | Struct.Feld | fixmod | Zugriff mit "->" auf Zeiger; verglichen != mit const MODSYNC; Bedingung |
| `mh_exec._mstack` | Struct.Feld | fixmod | Zugriff mit "->" auf Zeiger; zugewiesen: long |
| `mod_exec._mdata` | Struct.Feld | ident, qid | Zugriff mit "." auf Wert; zugewiesen: int; Zugriff mit "." auf Wert; gecastet auf unsigned long; Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_exec._mexcpt` | Struct.Feld | ident, qid | Zugriff mit "." auf Wert; zugewiesen: int; Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_exec._mexec` | Struct.Feld | ident, qid | Zugriff mit "." auf Wert; zugewiesen: int; Zugriff mit "." auf Wert; gecastet auf unsigned long; Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_exec._mstack` | Struct.Feld | ident, qid | Zugriff mit "." auf Wert; zugewiesen: int; Zugriff mit "." auf Wert; gecastet auf unsigned long; Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_exec._mh` | Struct.Feld | ident | Zugriff mit "." auf Wert; Struktur (weiterer Zugriff .); Zugriff mit "." auf Wert; Struktur (weiterer Zugriff .); gecastet auf unsigned int; Zugriff mit "." auf Wert; Struktur (weiterer Zugriff .); gecastet auf unsigned long |
| `mod_exec._midata` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_exec._midref` | Struct.Feld | qid | Zugriff mit "." auf Wert; gecastet auf u_int32 |
| `mod_exec._mh._maccess` | Struct.Feld | ident | Zugriff mit "." auf Wert; zugewiesen: short; Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_exec._mh._mattrev` | Struct.Feld | ident | Zugriff mit "." auf Wert; zugewiesen: short |
| `mod_exec._mh._medit` | Struct.Feld | ident | Zugriff mit "." auf Wert; zugewiesen: short; Zugriff mit "." auf Wert; gecastet auf unsigned int |
| `mod_exec._mh._mname` | Struct.Feld | ident | Zugriff mit "." auf Wert; zugewiesen: int; Zugriff mit "." auf Wert; zugewiesen an: long |
| `mod_exec._mh._msize` | Struct.Feld | ident | Zugriff mit "." auf Wert; gecastet auf unsigned long; Zugriff mit "." auf Wert; zugewiesen: int |
| `mod_exec._mh._msysrev` | Struct.Feld | ident | Zugriff mit "." auf Wert; zugewiesen: short |
| `mod_exec._mh._mtylan` | Struct.Feld | ident | Zugriff mit "." auf Wert; zugewiesen: short; Zugriff mit "." auf Wert; Argument 4 von printf; Zugriff mit "." auf Wert; Operand von ">>"; gecastet auf unsigned int |
| `struct mod_dir.md_group` | Struct.Feld | mdir | Zugriff mit "->" auf Zeiger; gecastet auf unsigned long |
| `struct mod_dir.md_link` | Struct.Feld | mdir | Zugriff mit "->" auf Zeiger; Argument 6 von printf; Zugriff mit "->" auf Zeiger; Argument 7 von printf; Zugriff mit "->" auf Zeiger; Argument 4 von printf |
| `struct mod_dir.md_mchk` | Struct.Feld | mdir | Zugriff mit "->" auf Zeiger; Argument 5 von printf |
| `struct mod_dir.md_mptr` | Struct.Feld | mdir | Zugriff mit "->" auf Zeiger; gecastet auf unsigned long; Zugriff mit "->" auf Zeiger; verglichen == mit int; Bedingung |
| `struct mod_dir.md_static` | Struct.Feld | mdir | Zugriff mit "->" auf Zeiger; gecastet auf long |
| `MA_GHOST` | Konstante/Makro | load, qid | Operand von "<<"; Bitmaske |
| `MODSYNC` | Konstante/Makro | fixmod, qid | Ausdruck |
| `_os_link` | Funktion | dump, link, save | _os_link(char **, mh_com **, void **, u_int16*, u_int16*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |
| `_os_get_moddir` | Funktion | mdir, qid | _os_get_moddir(struct mod_dir */mod_dir *, u_int32*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code; Bedingung; verglichen != mit int |
| `_os_setcrc` | Funktion | fixmod | _os_setcrc(mh_com *) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |

## SPF/BSD/netdb.h

27 Bestandteile, 20 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct hostent` | Typ (struct-Tag) | beam, bootptest, host, ping, rpcinfo, rpcmap, rpcport, rpcscan (+3) | Struktur-Tag; benutzte Felder: h_addr |
| `struct in_addr` | Typ (struct-Tag) | bootpwait, host | Struktur-Tag; benutzte Felder:  |
| `struct hostconfent` | Typ (struct-Tag) | hostconf | Struktur-Tag; benutzte Felder: key, value |
| `struct inetdent` | Typ (struct-Tag) | inetd | Struktur-Tag; benutzte Felder: protocol, service_name, socket_type, wait_status |
| `struct protoent` | Typ (struct-Tag) | proto | Struktur-Tag; benutzte Felder: p_name, p_proto |
| `struct servent` | Typ (struct-Tag) | service | Struktur-Tag; benutzte Felder: s_name, s_port, s_proto |
| `struct hostconfent.key` | Struct.Feld | hostconf | Zugriff mit "->" auf Zeiger; Argument 2 von printf; Zugriff mit "->" auf Zeiger; Ausdruck |
| `struct hostconfent.value` | Struct.Feld | hostconf | Zugriff mit "->" auf Zeiger; Argument 3 von printf; Zugriff mit "->" auf Zeiger; Ausdruck |
| `struct hostent.h_addr` | Struct.Feld | beam, bootptest, host, ping, rpcinfo, rpcmap, rpcport, rpcscan (+3) | Zugriff mit "->" auf Zeiger; Argument 2 von memcpy; Zugriff mit "->" auf Zeiger; gecastet auf struct in_addr * |
| `struct inetdent.protocol` | Struct.Feld | inetd | Zugriff mit "->" auf Zeiger; Argument 3 von printf; Zugriff mit "->" auf Zeiger; Ausdruck |
| `struct inetdent.service_name` | Struct.Feld | inetd | Zugriff mit "->" auf Zeiger; Argument 2 von printf; Zugriff mit "->" auf Zeiger; Ausdruck |
| `struct inetdent.socket_type` | Struct.Feld | inetd | Zugriff mit "->" auf Zeiger; Argument 4 von printf |
| `struct inetdent.wait_status` | Struct.Feld | inetd | Zugriff mit "->" auf Zeiger; Argument 5 von printf |
| `struct protoent.p_name` | Struct.Feld | proto | Zugriff mit "->" auf Zeiger; Argument 2 von printf |
| `struct protoent.p_proto` | Struct.Feld | proto | Zugriff mit "->" auf Zeiger; Argument 3 von printf |
| `struct servent.s_name` | Struct.Feld | service | Zugriff mit "->" auf Zeiger; Argument 2 von printf |
| `struct servent.s_port` | Struct.Feld | service | Zugriff mit "->" auf Zeiger; Argument 4 von printf |
| `struct servent.s_proto` | Struct.Feld | service | Zugriff mit "->" auf Zeiger; Argument 3 von printf |
| `gethostbyname` | Funktion | beam, bootptest, host, ping, rpcinfo, rpcmap, rpcport, rpcscan (+3) | gethostbyname(char *) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: struct hostent * |
| `inet_ntoa` | Funktion | bootpwait, host, ifconfig, netstat, ping, route | inet_ntoa(struct sockaddr_in.sin_addr/struct in_addr) -> Rueckgabe aus Nutzung: Ausdruck |
| `endhostconfent` | Funktion | hostconf | endhostconfent() -> Rueckgabe aus Nutzung: Ergebnis ignoriert |
| `endintent` | Funktion | intent | endintent() -> Rueckgabe aus Nutzung: Ergebnis ignoriert |
| `gethostconfent` | Funktion | hostconf | gethostconfent() -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: struct hostconfent * |
| `getinetdent` | Funktion | inetd | getinetdent() -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: struct inetdent * |
| `getintent` | Funktion | intent | getintent() -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: char * |
| `getprotobyname` | Funktion | proto | getprotobyname(?) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: struct protoent * |
| `getservbyname` | Funktion | service | getservbyname(?, ?) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: struct servent * |

## process.h

20 Bestandteile, 2 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `Pr_desc` | Typ (typedef-Name) | procs | Typname; als Struktur benutzt, Felder: _age, _fcalls, _group, _icalls, _id, _pagcnt, _pid, _pmodul, _prior, _rbytes, _state, _user, _wbytes |
| `pr_desc` | Typ (typedef-Name) | procs | Typname; als Struktur benutzt, Felder: _id |
| `process_id` | Typ (typedef-Name) | procs | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `Pr_desc._age` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 9 von printf; Zugriff mit "->" auf Wert; Argument 2 von printf |
| `Pr_desc._fcalls` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 3 von printf |
| `Pr_desc._group` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 4 von printf |
| `Pr_desc._icalls` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 4 von printf |
| `Pr_desc._id` | Struct.Feld | procs | Zugriff mit "->" auf Wert; verglichen == mit int; Zugriff mit "->" auf Wert; Argument 2 von printf |
| `Pr_desc._pagcnt` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 8 von printf |
| `Pr_desc._pid` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 3 von printf |
| `Pr_desc._pmodul` | Struct.Feld | procs | Zugriff mit "->" auf Wert; gecastet auf unsigned long |
| `Pr_desc._prior` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 6 von printf |
| `Pr_desc._rbytes` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 5 von printf |
| `Pr_desc._state` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 7 von printf |
| `Pr_desc._user` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 5 von printf |
| `Pr_desc._wbytes` | Struct.Feld | procs | Zugriff mit "->" auf Wert; Argument 6 von printf |
| `pr_desc._id` | Struct.Feld | procs | Zugriff mit "." auf Wert; verglichen == mit int |
| `_os_get_prtbl` | Funktion | procs | _os_get_prtbl(unsigned char *, u_int32*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |
| `_os_gprdsc` | Funktion | procs | _os_gprdsc(process_id, pr_desc*, u_int32*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |
| `_os_sysdbg` | Funktion | break | _os_sysdbg(void *, void *) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |

## MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/protocols/bootp.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h

15 Bestandteile, 2 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct bootp` | Typ (struct-Tag) | bootptest, bootpwait | Struktur-Tag; benutzte Felder: bp_file, bp_flags, bp_hlen, bp_htype, bp_op, bp_xid, bp_yiaddr |
| `struct bootp.bp_file` | Struct.Feld | bootpwait | Zugriff mit "." auf Wert; indiziert (Feld/Zeiger); Bedingung; Zugriff mit "." auf Wert; Argument 2 von printf |
| `struct bootp.bp_flags` | Struct.Feld | bootptest | Zugriff mit "." auf Wert; zugewiesen: int |
| `struct bootp.bp_hlen` | Struct.Feld | bootptest | Zugriff mit "." auf Wert; zugewiesen: int |
| `struct bootp.bp_htype` | Struct.Feld | bootptest | Zugriff mit "." auf Wert; zugewiesen: const HTYPE_ETHERNET |
| `struct bootp.bp_op` | Struct.Feld | bootptest | Zugriff mit "." auf Wert; zugewiesen: const BOOTREQUEST |
| `struct bootp.bp_xid` | Struct.Feld | bootptest | Zugriff mit "." auf Wert; zugewiesen: long |
| `struct bootp.bp_yiaddr` | Struct.Feld | bootpwait | Zugriff mit "." auf Wert; zugewiesen an: struct in_addr |
| `SOL_SOCKET` | Konstante/Makro | bootptest, bootpwait | Argument 2 von setsockopt |
| `BOOTREQUEST` | Konstante/Makro | bootptest | zugewiesen an: struct bootp.bp_op |
| `HTYPE_ETHERNET` | Konstante/Makro | bootptest | zugewiesen an: struct bootp.bp_htype |
| `IPPORT_BOOTPC` | Konstante/Makro | bootpwait | Argument 1 von htons |
| `IPPORT_BOOTPS` | Konstante/Makro | bootptest | Argument 1 von htons |
| `SO_BROADCAST` | Konstante/Makro | bootptest | Argument 3 von setsockopt |
| `SO_RCVTIMEO` | Konstante/Makro | bootpwait | Argument 3 von setsockopt |

## UNIX/stat.h

14 Bestandteile, 3 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct stat` | Typ (struct-Tag) | attr, dir, makdir | Struktur-Tag; benutzte Felder: st_gid, st_mode, st_mtime, st_size, st_uid |
| `struct stat.st_mode` | Struct.Feld | attr, dir, makdir | Zugriff mit "." auf Wert; Operand von "&"; Zugriff mit "." auf Wert; zugewiesen an: unsigned int; Zugriff mit "." auf Wert; Argument 1 von format_attributes |
| `struct stat.st_gid` | Struct.Feld | dir | Zugriff mit "." auf Wert; Argument 3 von printf |
| `struct stat.st_mtime` | Struct.Feld | dir | Zugriff mit "." auf Wert; verglichen == mit ?expr; zugewiesen an: struct tm *; Zugriff mit "." auf Wert; Adresse genommen |
| `struct stat.st_size` | Struct.Feld | dir | Zugriff mit "." auf Wert; Argument 4 von printf |
| `struct stat.st_uid` | Struct.Feld | dir | Zugriff mit "." auf Wert; Argument 2 von printf |
| `S_IEXEC` | Konstante/Makro | attr, dir, makdir | Bitmaske; Ausdruck; Operand von "~"; Bitmaske |
| `S_IFDIR` | Konstante/Makro | attr, dir, makdir | Bitmaske; Ausdruck; Operand von "~"; Bitmaske |
| `S_IOEXEC` | Konstante/Makro | attr, dir, makdir | Bitmaske; Ausdruck; Operand von "~"; Bitmaske |
| `S_IOREAD` | Konstante/Makro | attr, dir, makdir | Bitmaske; Ausdruck; Operand von "~"; Bitmaske |
| `S_IOWRITE` | Konstante/Makro | attr, dir, makdir | Bitmaske; Ausdruck; Operand von "~"; Bitmaske |
| `S_IREAD` | Konstante/Makro | attr, dir, makdir | Bitmaske; Ausdruck; Operand von "~"; Bitmaske |
| `S_IWRITE` | Konstante/Makro | attr, dir, makdir | Bitmaske; Ausdruck; Operand von "~"; Bitmaske |
| `S_ISHARE` | Konstante/Makro | attr, dir | Bitmaske; Ausdruck; Operand von "~"; Bitmaske |

## RPC/rpc.h

13 Bestandteile, 3 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct q9namenode` | Typ (struct-Tag) | rdir_bundle | Struktur-Tag; benutzte Felder:  |
| `XDR` | Typ (typedef-Name) | rdir_bundle, rsort_bundle | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `bool_t` | Typ (typedef-Name) | rdir_bundle, rsort_bundle | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `u_int` | Typ (typedef-Name) | rsort_bundle | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `FALSE` | Konstante/Makro | rdir_bundle | Rueckgabewert |
| `TRUE` | Konstante/Makro | rdir_bundle | Rueckgabewert |
| `xdr_int` | Symbol (Funktion/Variable, als Wert benutzt) | rmsg_bundle | Argument 5 von clnt_call |
| `xdr_wrapstring` | Symbol (Funktion/Variable, als Wert benutzt) | rmsg_bundle | Argument 3 von clnt_call |
| `xdr_string` | Funktion | rdir_bundle, rsort_bundle | xdr_string(XDR *, nametype */q9str *, int) -> Rueckgabe aus Nutzung: Ergebnis zurueckgegeben |
| `bzero` | Funktion | rdir_bundle | bzero(struct q9readdir_res*, size(sizeof)) -> Rueckgabe aus Nutzung: Ergebnis ignoriert |
| `xdr_array` | Funktion | rsort_bundle | xdr_array(XDR *, caddr_t *, u_int*, int, size(sizeof), bool_t) -> Rueckgabe aus Nutzung: Ergebnis zurueckgegeben |
| `xdr_int` | Funktion | rdir_bundle | xdr_int(XDR *, int*) -> Rueckgabe aus Nutzung: logisch negiert |
| `xdr_pointer` | Funktion | rdir_bundle | xdr_pointer(XDR *, char **, size(sizeof), bool_t) -> Rueckgabe aus Nutzung: Ergebnis zurueckgegeben |

## MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | modes.h

12 Bestandteile, 5 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct ip_mreq` | Typ (struct-Tag) | mrecv | Struktur-Tag; benutzte Felder: imr_interface, imr_multiaddr |
| `struct ip_mreq.imr_interface` | Struct.Feld | mrecv | Zugriff mit "." auf Wert; Struktur (weiterer Zugriff .) |
| `struct ip_mreq.imr_multiaddr` | Struct.Feld | mrecv | Zugriff mit "." auf Wert; Struktur (weiterer Zugriff .) |
| `struct ip_mreq.imr_interface.s_addr` | Struct.Feld | mrecv | Zugriff mit "." auf Wert; zugewiesen: const INADDR_ANY |
| `struct ip_mreq.imr_multiaddr.s_addr` | Struct.Feld | mrecv | Zugriff mit "." auf Wert; zugewiesen: unsigned long |
| `IPPROTO_ICMP` | Konstante/Makro | ping | Argument 3 von socket |
| `IPPROTO_IP` | Konstante/Makro | mrecv | Argument 2 von setsockopt |
| `IP_ADD_MEMBERSHIP` | Konstante/Makro | mrecv | Argument 3 von setsockopt |
| `SOCK_RAW` | Konstante/Makro | ping | Argument 2 von socket |
| `connect` | Funktion | tcpsend, telnet | connect(int, struct sockaddr *, size(sizeof)) -> Rueckgabe aus Nutzung: Bedingung; verglichen < mit int |
| `send` | Funktion | tcpsend, telnet | send(int, char *, ret(strlen), int) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: int |
| `htonl` | Funktion | beam | htonl(int/u_int32) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: u_int32 |

## MEHRDEUTIG: SPF/BSD/net/if.h | SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/ioctl.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/sockio.h | SPF/BSD/sys/types.h | modes.h

10 Bestandteile, 1 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct ifreq` | Typ (struct-Tag) | ifconfig | Struktur-Tag; benutzte Felder: ifr_addr, ifr_flags, ifr_mtu, ifr_name |
| `struct ifreq.ifr_addr` | Struct.Feld | ifconfig | Zugriff mit "." auf Wert; Bitmaske |
| `struct ifreq.ifr_flags` | Struct.Feld | ifconfig | Zugriff mit "." auf Wert; gecastet auf unsigned short |
| `struct ifreq.ifr_mtu` | Struct.Feld | ifconfig | Zugriff mit "." auf Wert; gecastet auf unsigned long |
| `struct ifreq.ifr_name` | Struct.Feld | ifconfig | Zugriff mit "." auf Wert; Argument 1 von strncpy |
| `IFNAMSIZ` | Konstante/Makro | ifconfig | Operand von "-"; Argument 3 von strncpy |
| `SIOCGIFADDR` | Konstante/Makro | ifconfig | Argument 2 von ioctl |
| `SIOCGIFFLAGS` | Konstante/Makro | ifconfig | Argument 2 von ioctl |
| `SIOCGIFMTU` | Konstante/Makro | ifconfig | Argument 2 von ioctl |
| `ioctl` | Funktion | ifconfig | ioctl(int, const SIOCGIFADDR/const SIOCGIFFLAGS/const SIOCGIFMTU, char *) -> Rueckgabe aus Nutzung: Bedingung; verglichen >= mit int; Ergebnis zugewiesen an: int |

## MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h

10 Bestandteile, 15 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct rtreq` | Typ (struct-Tag) | netstat, route, routectl | Struktur-Tag; benutzte Felder: dst, flags, gateway, netmask |
| `struct rtreq.dst` | Struct.Feld | netstat, route, routectl | Zugriff mit "->" auf Zeiger; Adresse genommen; Zugriff mit "." auf Wert; Adresse genommen |
| `struct rtreq.gateway` | Struct.Feld | netstat, route, routectl | Zugriff mit "->" auf Zeiger; Adresse genommen; Zugriff mit "." auf Wert; Adresse genommen |
| `struct rtreq.netmask` | Struct.Feld | netstat, route, routectl | Zugriff mit "->" auf Zeiger; Adresse genommen; Zugriff mit "." auf Wert; Adresse genommen |
| `struct rtreq.flags` | Struct.Feld | netstat, route | Zugriff mit "->" auf Zeiger; Argument 5 von printf |
| `inet_addr` | Funktion | beam, bootptest, mrecv, msend, ping, routectl, rpcinfo, rpcmap (+5) | inet_addr(char *) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: unsigned long; Ergebnis zugewiesen an: struct sockaddr_in.sin_addr.s_addr |
| `endroutent` | Funktion | netstat, route | endroutent() -> Rueckgabe aus Nutzung: Ergebnis ignoriert |
| `getroutent` | Funktion | netstat, route | getroutent() -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: struct rtreq * |
| `delroutent` | Funktion | routectl | delroutent(struct rtreq*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |
| `putroutent` | Funktion | routectl | putroutent(struct rtreq*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |

## MEHRDEUTIG: UNIX/stat.h | dir.h

10 Bestandteile, 1 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct direct` | Typ (struct-Tag) | dir | Struktur-Tag; benutzte Felder: d_addr, d_name |
| `struct name_node` | Typ (struct-Tag) | dir | Struktur-Tag; benutzte Felder:  |
| `DIR` | Typ (typedef-Name) | dir | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `?.name` | Struct.Feld | dir | Basisausdruck nicht typisierbar ()): Ausdruck |
| `?.next` | Struct.Feld | dir | Basisausdruck nicht typisierbar ()): Ausdruck |
| `struct direct.d_addr` | Struct.Feld | dir | Zugriff mit "->" auf Zeiger; Argument 3 von add_name |
| `struct direct.d_name` | Struct.Feld | dir | Zugriff mit "->" auf Zeiger; Argument 1 von strcmp; Zugriff mit "->" auf Zeiger; indiziert (Feld/Zeiger); Zugriff mit "->" auf Zeiger; Argument 2 von add_name |
| `closedir` | Funktion | dir | closedir(DIR *) -> Rueckgabe aus Nutzung: Ergebnis ignoriert |
| `opendir` | Funktion | dir | opendir(char *) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: DIR * |
| `readdir` | Funktion | dir | readdir(DIR *) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: struct direct * |

## MEHRDEUTIG: modes.h | rbf.h

10 Bestandteile, 2 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `u_char` | Typ (typedef-Name) | dcheck, touch | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `Sector0` | Typ (typedef-Name) | dcheck | Typname; als Struktur benutzt, Felder: dd_bit, dd_map, dd_name, dd_tot |
| `fd_stats` | Typ (typedef-Name) | touch | Typname; als Struktur benutzt, Felder: fd_date |
| `Sector0.dd_bit` | Struct.Feld | dcheck | Zugriff mit "->" auf Wert; gecastet auf unsigned; Zugriff mit "->" auf Wert; verglichen == mit int |
| `Sector0.dd_map` | Struct.Feld | dcheck | Zugriff mit "->" auf Wert; gecastet auf unsigned; Zugriff mit "->" auf Wert; verglichen == mit int |
| `Sector0.dd_name` | Struct.Feld | dcheck | Zugriff mit "->" auf Wert; indiziert (Feld/Zeiger); Zugriff mit "->" auf Wert; indiziert (Feld/Zeiger); Argument 1 von putchar |
| `Sector0.dd_tot` | Struct.Feld | dcheck | Zugriff mit "->" auf Wert; Argument 1 von be24 |
| `fd_stats.fd_date` | Struct.Feld | touch | Zugriff mit "." auf Wert; indiziert (Feld/Zeiger) |
| `_os_gs_fd` | Funktion | touch | _os_gs_fd(path_id, int, fd_stats*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |
| `_os_ss_fd` | Funktion | touch | _os_ss_fd(path_id, fd_stats*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |

## SPF/BSD/netinet/in.h

9 Bestandteile, 18 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct sockaddr_in` | Typ (struct-Tag) | beam, bootptest, bootpwait, ifconfig, mrecv, msend, netstat, ping (+10) | Struktur-Tag; benutzte Felder: sin_addr, sin_family, sin_port |
| `struct sockaddr` | Typ (struct-Tag) | beam, bootptest, bootpwait, mrecv, msend, netstat, ping, route (+5) | Struktur-Tag; benutzte Felder: sa_family, sa_len |
| `struct sockaddr.sa_family` | Struct.Feld | netstat, route, routectl | Zugriff mit "->" auf Zeiger; verglichen == mit const AF_INET; Bedingung; Zugriff mit "->" auf Zeiger; zugewiesen: const AF_INET |
| `struct sockaddr.sa_len` | Struct.Feld | routectl | Zugriff mit "->" auf Zeiger; zugewiesen: size(sizeof) |
| `struct sockaddr_in.sin_addr` | Struct.Feld | beam, bootptest, bootpwait, ifconfig, mrecv, msend, netstat, ping (+10) | Zugriff mit "." auf Wert; Struktur (weiterer Zugriff .); Zugriff mit "." auf Wert; Struktur (weiterer Zugriff .); Adresse genommen; Zugriff mit "->" auf Zeiger; Argument 1 von inet_ntoa |
| `struct sockaddr_in.sin_family` | Struct.Feld | beam, bootptest, bootpwait, mrecv, msend, ping, rpcinfo, rpcmap (+6) | Zugriff mit "." auf Wert; zugewiesen: const AF_INET; Zugriff mit "->" auf Zeiger; zugewiesen: const AF_INET |
| `struct sockaddr_in.sin_port` | Struct.Feld | beam, bootptest, bootpwait, mrecv, msend, tcprecv, tcpsend, telnet (+1) | Zugriff mit "." auf Wert; zugewiesen: ?expr |
| `struct sockaddr_in.sin_addr.s_addr` | Struct.Feld | beam, bootptest, bootpwait, mrecv, msend, ping, routectl, rpcinfo (+7) | Zugriff mit "." auf Wert; zugewiesen: unsigned long; Zugriff mit "." auf Wert; Adresse genommen; Zugriff mit "." auf Wert; zugewiesen: const INADDR_ANY |
| `AF_INET` | Konstante/Makro | beam, bootptest, bootpwait, ifconfig, mrecv, msend, netstat, ping (+10) | zugewiesen an: struct sockaddr_in.sin_family; Argument 1 von socket; Ausdruck |

## modes.h

9 Bestandteile, 18 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `path_id` | Typ (typedef-Name) | dcheck, dump, free, paths, qid, save, touch | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `FAM_READ` | Konstante/Makro | dcheck, dump, free, paths, qid, save, touch | Argument 2 von _os_open; Operand von "/"; Argument 2 von _os_open; Operand von "/"; zugewiesen an: u_int32 |
| `FAM_EXEC` | Konstante/Makro | del, deldir, dump, qid, save | Ausdruck; Operand von "/"; Bitmaske; Argument 2 von _os_delete |
| `FAM_DIR` | Konstante/Makro | deldir, qid | Operand von "/"; Argument 2 von _os_delete; Operand von "/"; Operand von "/"; Bitmaske |
| `FAM_WRITE` | Konstante/Makro | save, touch | Operand von "/"; Bitmaske; Operand von "/"; Operand von "/"; Bitmaske |
| `_os_close` | Funktion | beam, dcheck, dump, free, ifconfig, mrecv, msend, paths (+8) | _os_close(int/path_id) -> Rueckgabe aus Nutzung: Ergebnis ignoriert |
| `_os_open` | Funktion | dcheck, dump, free, paths, qid, save, touch | _os_open(char *, const FAM_READ/u_int32, path_id*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code; Bedingung; verglichen != mit int |
| `_os_read` | Funktion | dcheck, dump, qid | _os_read(path_id, unsigned char */u_char */void *, u_int32*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code; Ausdruck; Bedingung |
| `_os_create` | Funktion | save, touch | _os_create(char *, ?, path_id*, int) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |

## MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/types.h | SPF/RPC/pmap_clnt.h | SPF/RPC/pmap_prot.h | SPF/RPC/rpc.h

8 Bestandteile, 2 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct pmaplist` | Typ (struct-Tag) | rpcinfo, rpcmap | Struktur-Tag; benutzte Felder: pml_map, pml_next |
| `struct pmaplist.pml_map` | Struct.Feld | rpcinfo, rpcmap | Zugriff mit "->" auf Zeiger; Struktur (weiterer Zugriff .); gecastet auf unsigned long; Zugriff mit "->" auf Zeiger; Struktur (weiterer Zugriff .); Argument 4 von printf; Zugriff mit "->" auf Zeiger; Struktur (weiterer Zugriff .); gecastet auf unsigned |
| `struct pmaplist.pml_next` | Struct.Feld | rpcinfo, rpcmap | Zugriff mit "->" auf Zeiger; zugewiesen an: struct pmaplist * |
| `struct pmaplist.pml_map.pm_port` | Struct.Feld | rpcinfo, rpcmap | Zugriff mit "." auf Wert; gecastet auf unsigned; Zugriff mit "." auf Wert; Argument 5 von printf |
| `struct pmaplist.pml_map.pm_prog` | Struct.Feld | rpcinfo, rpcmap | Zugriff mit "." auf Wert; gecastet auf unsigned long; Zugriff mit "." auf Wert; Argument 2 von printf |
| `struct pmaplist.pml_map.pm_prot` | Struct.Feld | rpcinfo, rpcmap | Zugriff mit "." auf Wert; verglichen == mit ?expr; Argument 4 von printf |
| `struct pmaplist.pml_map.pm_vers` | Struct.Feld | rpcinfo, rpcmap | Zugriff mit "." auf Wert; gecastet auf unsigned long; Zugriff mit "." auf Wert; Argument 3 von printf |
| `pmap_getmaps` | Funktion | rpcinfo, rpcmap | pmap_getmaps(struct sockaddr_in*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: struct pmaplist * |

## MEHRDEUTIG: RPC/rpc.h | SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/types.h | SPF/RPC/pmap_clnt.h | SPF/RPC/pmap_prot.h | SPF/RPC/rpc.h

7 Bestandteile, 6 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `CLIENT` | Typ (typedef-Name) | rdir_bundle, rmsg_bundle, rpcinfo, rpcnull, rpcudp, rsort_bundle | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `caddr_t` | Typ (typedef-Name) | rpcinfo, rpcnull, rpcudp, rsort_bundle | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `RPC_SUCCESS` | Konstante/Makro | rdir_bundle, rmsg_bundle, rpcinfo, rpcnull, rpcudp, rsort_bundle | Ausdruck |
| `clnt_call` | Funktion | rdir_bundle, rmsg_bundle, rpcinfo, rpcnull, rpcudp, rsort_bundle | clnt_call(CLIENT *, int/const NULLPROC, const xdr_void/bool_t/const xdr_wrapstring, caddr_t/nametype */char **, const xdr_void/bool_t/const xdr_int, caddr_t/struct q9readdir_res*/int **, struct timeval) -> Rueckgabe aus Nutzung: Bedingung; verglichen != mit const RPC_SUCCESS; Ergebnis zugewiesen an: enum clnt_stat |
| `clnt_create` | Funktion | rdir_bundle, rmsg_bundle, rpcinfo, rpcnull, rpcudp, rsort_bundle | clnt_create(char *, int/u_long, int/u_long, char *) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: CLIENT * |
| `clnt_pcreateerror` | Funktion | rdir_bundle, rmsg_bundle, rpcinfo, rpcnull, rpcudp, rsort_bundle | clnt_pcreateerror(char *) -> Rueckgabe aus Nutzung: Ergebnis ignoriert |
| `clnt_perror` | Funktion | rdir_bundle, rmsg_bundle, rpcinfo, rpcnull, rpcudp, rsort_bundle | clnt_perror(CLIENT *, char *) -> Rueckgabe aus Nutzung: Ergebnis ignoriert |

## MEHRDEUTIG: SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | modes.h

5 Bestandteile, 7 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `u_short` | Typ (typedef-Name) | beam, mrecv, msend, tcprecv, tcpsend, telnet, tftp | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `SOCK_STREAM` | Konstante/Makro | tcprecv, tcpsend, telnet | Argument 2 von socket |
| `recv` | Funktion | mrecv, tcprecv, telnet | recv(int, char *, size(sizeof), int) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: int |
| `accept` | Funktion | tcprecv | accept(int, struct sockaddr *, int *) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: int |
| `listen` | Funktion | tcprecv | listen(int, int) -> Rueckgabe aus Nutzung: verglichen < mit int |

## MEHRDEUTIG: memory.h | module.h

5 Bestandteile, 1 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `MEM_ANY` | Konstante/Makro | load | Rueckgabewert; zugewiesen an: u_int32 |
| `SYSRAM` | Konstante/Makro | load | Rueckgabewert |
| `VIDEO1` | Konstante/Makro | load | Rueckgabewert |
| `VIDEO2` | Konstante/Makro | load | Rueckgabewert |
| `_os_load` | Funktion | load | _os_load(char *, mh_com **, void **, u_int16, u_int16*, u_int16*, u_int32) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |

## MEHRDEUTIG: modes.h | module.h

5 Bestandteile, 3 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `FAP_READ` | Konstante/Makro | save | Operand von "/"; Argument 4 von _os_create |
| `FAP_WRITE` | Konstante/Makro | save | Operand von "/"; Bitmaske |
| `_os_seek` | Funktion | dump, qid | _os_seek(path_id, u_int32) -> Rueckgabe aus Nutzung: verglichen != mit int; Bedingung |
| `_os_delete` | Funktion | save | _os_delete(char *, const FAM_EXEC) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |
| `_os_write` | Funktion | save | _os_write(path_id, void *, u_int32*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |

## MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h

4 Bestandteile, 8 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `SOCK_DGRAM` | Konstante/Makro | beam, bootptest, bootpwait, ifconfig, mrecv, msend, tftp | Argument 2 von socket |
| `sendto` | Funktion | beam, bootptest, msend, ping, tftp | sendto(int, char *, int/size(sizeof)/ret(strlen), int, struct sockaddr *, size(sizeof)/int) -> Rueckgabe aus Nutzung: Bedingung; verglichen < mit int; Ergebnis zugewiesen an: int |
| `recvfrom` | Funktion | bootpwait, ping, tftp | recvfrom(int, char *, size(sizeof), int, struct sockaddr *, int*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: int; Bedingung; verglichen < mit int |
| `setsockopt` | Funktion | bootptest, bootpwait, mrecv | setsockopt(int, const SOL_SOCKET/const IPPROTO_IP, const SO_BROADCAST/const SO_RCVTIMEO/const IP_ADD_MEMBERSHIP, char *, size(sizeof)) -> Rueckgabe aus Nutzung: Bedingung; verglichen < mit int; Ergebnis ignoriert |

## MEHRDEUTIG: SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h

4 Bestandteile, 11 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `INADDR_ANY` | Konstante/Makro | bootpwait, mrecv, tcprecv | zugewiesen an: struct sockaddr_in.sin_addr.s_addr; zugewiesen an: struct ip_mreq.imr_interface.s_addr |
| `socket` | Funktion | beam, bootptest, bootpwait, ifconfig, mrecv, msend, ping, tcprecv (+3) | socket(const AF_INET, const SOCK_DGRAM/const SOCK_STREAM/const SOCK_RAW, int/const IPPROTO_ICMP) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: int |
| `htons` | Funktion | beam, bootptest, bootpwait, mrecv, msend, tcprecv, tcpsend, telnet (+1) | htons(u_short/const IPPORT_BOOTPS/const IPPORT_BOOTPC) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: struct sockaddr_in.sin_port |
| `bind` | Funktion | bootpwait, mrecv, tcprecv | bind(int, struct sockaddr *, size(sizeof)) -> Rueckgabe aus Nutzung: Bedingung; verglichen < mit int |

## SPF/RPC/rpc.h

4 Bestandteile, 4 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `u_long` | Typ (typedef-Name) | rpcinfo, rpcnull, rpcport, rpcudp | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |
| `enum clnt_stat` | Typ (enum-Tag) | rpcinfo, rpcnull, rpcudp | Enum-Tag (benutzt (nicht definiert)) |
| `NULLPROC` | Konstante/Makro | rpcinfo, rpcnull, rpcudp | Argument 2 von clnt_call |
| `xdr_void` | Symbol (Funktion/Variable, als Wert benutzt) | rpcinfo, rpcnull, rpcudp | Argument 3 von clnt_call; Argument 5 von clnt_call |

## MEHRDEUTIG: modes.h | sg_codes.h

4 Bestandteile, 2 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `_os9_gs_free` | Funktion | free | _os9_gs_free(path_id, u_int32*) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |
| `_os_gs_devnm` | Funktion | paths | _os_gs_devnm(path_id, char *) -> Rueckgabe aus Nutzung: Ergebnis ignoriert |
| `_os_gs_pos` | Funktion | paths | _os_gs_pos(path_id, u_int32*) -> Rueckgabe aus Nutzung: Ergebnis ignoriert |
| `_os_gs_size` | Funktion | paths | _os_gs_size(path_id, u_int32*) -> Rueckgabe aus Nutzung: Ergebnis ignoriert |

## MEHRDEUTIG: RPC/rpc.h | SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/protocols/bootp.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | SPF/RPC/pmap_clnt.h | SPF/RPC/pmap_prot.h | SPF/RPC/rpc.h

3 Bestandteile, 7 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `struct timeval` | Typ (struct-Tag) | bootpwait, rdir_bundle, rmsg_bundle, rpcinfo, rpcnull, rpcudp, rsort_bundle | Struktur-Tag; benutzte Felder: tv_sec, tv_usec |
| `struct timeval.tv_sec` | Struct.Feld | bootpwait | Zugriff mit "." auf Wert; zugewiesen: int |
| `struct timeval.tv_usec` | Struct.Feld | bootpwait | Zugriff mit "." auf Wert; zugewiesen: int |

## MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/types.h | SPF/RPC/pmap_clnt.h | SPF/RPC/rpc.h

3 Bestandteile, 4 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `IPPROTO_TCP` | Konstante/Makro | rpcinfo, rpcmap, rpcport, rpcscan | Ausdruck; zugewiesen an: u_long; Argument 4 von pmap_getport |
| `IPPROTO_UDP` | Konstante/Makro | rpcport, rpcscan | zugewiesen an: u_long; Argument 4 von pmap_getport |
| `pmap_getport` | Funktion | rpcport, rpcscan | pmap_getport(struct sockaddr_in*, unsigned long/u_long, int/u_long, u_long/const IPPROTO_TCP/const IPPROTO_UDP) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: unsigned long; Ergebnis zugewiesen an: u_long |

## setsys.h

3 Bestandteile, 1 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `D_DevCnt` | Symbol (Funktion/Variable, als Wert benutzt) | devs | Argument 1 von _getsys |
| `D_DevSiz` | Symbol (Funktion/Variable, als Wert benutzt) | devs | Argument 1 von _getsys |
| `D_DevTbl` | Symbol (Funktion/Variable, als Wert benutzt) | devs | Argument 1 von _getsys |

## KEIN KANDIDAT

2 Bestandteile, 2 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `_os9_sleep` | Funktion | sleep | _os9_sleep(u_int32*) -> Rueckgabe aus Nutzung: Bedingung; verglichen != mit int |
| `_os_setime` | Funktion | setime | _os_setime(time_t) -> Rueckgabe aus Nutzung: Bedingung; verglichen != mit int |

## MEHRDEUTIG: stdio.h | stdlib.h | string.h

2 Bestandteile, 7 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `malloc` | Funktion | cmp, copy, dir, fixmod, grep, ping, qsort | malloc(size_t/size(sizeof)) -> Rueckgabe aus Nutzung: Ergebnis gecastet/verwendet; Ergebnis zugewiesen an: void * |
| `free` | Funktion | cmp, copy, dir, fixmod, ping, qsort | free(unsigned char */void */char *) -> Rueckgabe aus Nutzung: Ergebnis ignoriert; Ergebnis gecastet/verwendet |

## MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | SPF/BSD/sys/socket.h | SPF/BSD/sys/types.h | dir.h | direct.h | memory.h | modes.h | module.h | process.h | rbf.h | sg_codes.h | types.h

1 Bestandteile, 12 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `u_int32` | Typ (typedef-Name) | beam, dcheck, dump, free, load, mdir, mfree, paths (+4) | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |

## MEHRDEUTIG: SPF/BSD/netdb.h | SPF/BSD/netinet/in.h | dir.h | direct.h | events.h | memory.h | modes.h | module.h | process.h | rbf.h | sg_codes.h | types.h

1 Bestandteile, 18 Verbraucherdateien (Zuordnung aus den Include-Listen der Verbraucher nicht eindeutig; Kandidaten ueber Schnitt der Include-Mengen)

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `error_code` | Typ (typedef-Name) | break, dcheck, deldir, dump, events, fixmod, free, link (+10) | Typname; Skalar/undurchsichtig (keine Feldzugriffe) |

## events.h

1 Bestandteile, 1 Verbraucherdateien

| Bestandteil | Art | benutzt in | aus der Nutzung ableitbare Signatur / Typ |
|---|---|---|---|
| `_os_ev_delete` | Funktion | events | _os_ev_delete(char *) -> Rueckgabe aus Nutzung: Ergebnis zugewiesen an: error_code |

## Anhang A: von den Tools selbst per `extern` deklarierte Systemfunktionen (kein Header-Bedarf, aber Hinweis auf Bibliotheks-Schnittstelle)

| Tool | Deklaration im Quelltext |
|---|---|
| attr | `int stat(const char *, struct stat *)` |
| attr | `int chmod(const char *, int)` |
| beam | `int getstat(int, int, void *)` |
| beam | `int setstat(int, int, void *)` |
| chown | `int chown(const char *,int)` |
| deiniz | `void *attach(const char *, int)` |
| deiniz | `int detach(void *)` |
| del | `int unlinkx(const char *, int)` |
| deldir | `error_code _os_delete(const char *, u_int32)` |
| devs | `int _getsys(int, int)` |
| dir | `int stat(const char *, struct stat *)` |
| hostname | `int gethostname(char *, int)` |
| hostname | `int sethostname(char *, int)` |
| ifconfig | `int getstat(int,int,void*)` |
| iniz | `void *attach(const char *, int)` |
| makdir | `int makdir(const char *, int, int, int)` |
| makdir | `int stat(const char *, struct stat *)` |
| mfree | `error_code q9_gblkmp(void *, struct q9_gblkmp_info *)` |
| mrecv | `int getstat(int,int,void*)` |
| msend | `int getstat(int,int,void*)` |
| pd | `int _gs_devn(int, char *)` |
| ping | `int getstat(int, int, void *)` |
| ping | `int setstat(int, int, void *)` |
| qid | `error_code _qid_cpymem(u_int16 owner_pid, u_int32 count, void *src, void *dst)` |
| tcprecv | `int getstat(int,int,void*)` |
| tcpsend | `int getstat(int,int,void*)` |
| telnet | `int getstat(int,int,void*)` |
| tftp | `int getstat(int,int,void*)` |
| unlink | `error_code _os_link(char **, mh_com **, void **, u_int16 *, u_int16 *)` |
| unlink | `error_code _os_unlink(mh_com *)` |

## Anhang B: Verfahren und Grenzen

- Werkzeuge: qcpp (Q9-SDK/macOS/CMDS_CLANG), Aufruf `qcpp -Iinc datei.c datei.i`; qcir nimmt den Quelltext als Argument (`qcir "$(cat x.i)"`), meldet Fehler fortlaufend, bricht aber bei Syntaxfehlern ab. Die Q-Tools bauen regulaer mit xcc (Makefiles in c/); diese Kette wurde nicht benutzt.
- inc/ enthielt anfangs nur die eigenen 11 C-Standardheader; qcpp bricht beim ersten fehlenden Header ab, daher wurden 24 leere Stubs schrittweise angelegt (dir.h direct.h events.h memory.h modes.h module.h process.h rbf.h RPC/rpc.h setsys.h sg_codes.h types.h UNIX/stat.h sowie SPF/BSD/{net/if,netdb,netinet/in,protocols/bootp,sys/ioctl,sys/socket,sys/sockio,sys/types}.h und SPF/RPC/{pmap_clnt,pmap_prot,rpc}.h). Eine _probe.h wurde nicht benoetigt.
- qcir-Fehlerlisten: 561 Meldungen (unknown type name 107, unknown function 91, unknown variable 73, unknown struct 21, Rest Folgefehler). 34 von 76 Dateien brechen in qcir mit Syntaxfehler ab (K&R-Kopf main(n,v), Casts, chained member access); 17 Dateien meldeten keinen Fehler. Deshalb wurde der Bedarf zusaetzlich mit einer eigenen Token-Analyse (analyze.py) der vorverarbeiteten .i-Dateien ermittelt: Typnamen, struct-Tags, Felder (mit Basistyp-Aufloesung), Funktionen, Konstanten.
- Gegenprobe direkt im Quelltext (Grossbuchstaben-Namen, Funktionsaufrufe, #if-Namen): keine weiteren unaufgeloesten Namen; die Quelltexte enthalten keine #if/#ifdef-Bedingungen.
- Nicht analysierbar: rdir_server.c, rmsg_server.c, rsort_server.c bestehen nur aus #include-Zeilen auf Dateien unter MWOS/SRC (verbotene Quelle) und wurden ausgeschlossen; die *_bundle.c sind eigenstaendig und wurden analysiert. Damit 76 von 79 Verbrauchern ausgewertet.
- Grenzen: Feldtypen unbekannter Strukturen sind nur aus Zuweisungs-/Argumentkontext ableitbar; Felder verschachtelter Strukturen erscheinen als Pfad (z. B. struct sockaddr_in.sin_addr.s_addr).
