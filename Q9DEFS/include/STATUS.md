# BUILD_REPORT: Phase 3 (Implementierung der Systemheader)

Ergebnis: `scratchpad/hdr/inc_new/` (gespiegelt nach `/tmp/hdr_work/inc_new/` auf dem Mac). Pruefskript: `scratchpad/hdr/build.sh` (laeuft auf dem Mac):
`clang -fsyntax-only -std=gnu89 -w -nostdinc -I inc_new -I q9-qcpp/include datei.c`.

## Zahlen

- 32 Header-Dateien: 22 mit Definitionen, 10 Weiterleiter/leere Huellen (direct.h, SPF/BSD/sys/ioctl.h, SPF/RPC/{rpc,pmap_clnt,pmap_prot}.h, sockio.h, setsys.h, net/if.h (nur IFNAMSIZ), sowie die Std-Erweiterungen time.h/signal.h/stdlib.h).
- Definitionen: 52 Makros/Konstanten, 64 Prototypen, 41 Typ-/Struct-Definitionen (typedef und struct-Koepfe; dazu die Struct-Member).
- Verbraucher: 76 geprueft (3 *_server.c ausgenommen): **58 fehlerfrei, 18 mit Restfehlern**.
- Alle 18 Restfehler haben ausschliesslich offene Bestandteile ('Handbuch nennt nichts') als Ursache; nach der Fehlerkorrektur bleibt kein Fehler uebrig, der sich aus HDR_SPEC.md ergaebe.

## Restfehler nach Header und Ursache (alle: offen)

| Header | fehlende Bestandteile | betroffene Verbraucher |
|---|---|---|
| modes.h | S_IFDIR, S_ISHARE, S_IOEXEC, S_IOWRITE, S_IOREAD | attr, dir, makdir |
| modes.h | FAP_READ, FAP_WRITE | save |
| module.h | struct mod_dir / mod_dir (+ md_mptr ...) | mdir, qid |
| module.h | MA_GHOST | load, qid |
| module.h | ML_JAVACODE | qid |
| dir.h | DIRBLKSIZ | qid |
| dir.h | struct direct.d_addr | dir |
| process.h | Pr_desc, pr_desc (+ Members) | procs |
| setsys.h | D_DevCnt, D_DevSiz, D_DevTbl | devs |
| SPF/BSD/sys/socket.h | SOL_SOCKET, SO_BROADCAST, SO_RCVTIMEO | bootptest, bootpwait |
| SPF/BSD/netinet/in.h | IP_ADD_MEMBERSHIP | mrecv |
| SPF/BSD/netdb.h | struct hostconfent, gethostconfent, endhostconfent | hostconf |
| SPF/BSD/netdb.h | struct inetdent, getinetdent | inetd |
| SPF/BSD/net/if.h + sys/sockio.h | struct ifreq (ganz offen), SIOCGIFADDR/FLAGS/MTU | ifconfig |
| RPC/rpc.h | enum clnt_stat (clnt_call nicht deklarierbar) | rpcinfo, rpcnull, rpcudp |

Hinweis: qid, dir, attr, makdir haben mehrere offene Ursachen; sobald ein Bestandteil gemessen ist, tritt der naechste zutage.

## Aufloesungen, die nicht woertlich in HDR_SPEC.md stehen (Luecken der Spezifikation, keine erfundenen Werte)

1. `Sector0` ist im Verbraucher (dcheck) ein Zeiger-typedef (`Sector0 s; s = (Sector0)buf; s->dd_name`): als `typedef struct {...} *Sector0;` geschrieben. fd_stats bleibt Werttyp (touch: `fd.fd_date[i]`).
2. Erreichbarkeit ohne eigenes #include beim Verbraucher (Spezifikation nennt nur den Ursprungsheader):
   - `u_char` (dcheck, touch): types.h bindet SPF/BSD/sys/types.h ein.
   - `struct in_addr` (host): netdb.h bindet netinet/in.h ein.
   - `struct timeval` (bootpwait): sys/socket.h bindet UNIX/os9time.h ein (Header-Wahl geraten; die Zuordnung ist offen).
   - `htonl/htons`: netinet/in.h bindet sys/endian.h ein; `bzero`: RPC/rpc.h bindet UNIX/os9def.h ein (rdir_bundle).
3. Include-Namen der Verbraucher ohne eigenen Spezifikationsabschnitt sind reine Weiterleiter: direct.h -> dir.h, SPF/BSD/sys/ioctl.h -> ioctl.h, SPF/RPC/rpc.h und pmap_clnt.h -> RPC/rpc.h, SPF/RPC/pmap_prot.h -> RPC/pmap_prot.h.
4. `struct dirent`: dir_name[28] + `u_int32 dir_addr` @28 (Null-Byte + 3-Byte-LSN, big-endian gelesen ergibt die LSN); Spezifikation sagt "3 Byte @29, C-Typ unklar". `struct direct` hat nur `char d_name[1]` (Kapazitaet nicht dokumentiert).
5. `mod_driver`: die sieben Einsprungtabellen-Worte (_mdinit ... _mderror) stehen als Members direkt nach _mdata (@60ff), weil der Verbraucher sie aus der Struktur liest; die Spezifikation legt ihre Struct-Position nicht fest (Tabelle liegt laut Handbuch hinter dem Kopf). Unsicher.
6. `putroutent`: als `error_code` (ohne den Stern des Handbuchprototyps) deklariert; `getroutent` abgeleitet.
7. XDR: `x_op` als int (enum xdr_op ohne dokumentierte Enumeratoren), `struct xdr_ops`/`clnt_ops`/`AUTH` unvollstaendig; `xdrproc_t` als `bool_t (*)()`.
8. `error_code` = int, `path_id`/`process_id` = u_int16 (abgeleitet).
9. time.h/signal.h/stdlib.h in inc_new sind Ueberlagerungen der eigenen Std-Header (per `#include_next`) und fuegen nur `_os_setime`, `_os9_sleep`, `malloc`, `free` hinzu; unsere eigenen Std-Header (q9-qcpp/include) deklarieren diese absichtlich nicht. `#include_next` ist clang-spezifisch; fuer qcpp muessen die Prototypen stattdessen direkt in die Std-Header uebernommen werden. Ausserdem fehlt in unserem stdlib.h `atol` (consumer benutzt es; nur implizite Deklaration, kein Fehler).

## Nur-Warnung-Beobachtungen (kompilieren, aber nicht sauber)

- `clnt_call` ist wegen des offenen Rueckgabetyps nicht deklariert (rdir/rmsg/rsort_bundle: implizite Deklaration).
- `const char *` an `char *`-Parameter der belegten Handbuch-Prototypen (inet_addr, clnt_create, xdr_pointer ...): nur Warnungen.
- Host-Compiler ist 64 Bit: `unsigned long`/`long` sind dort 8 Byte; Struct-Layouts wurden daher nur gedanklich gegen die 68K-Offsets geprueft, nicht per sizeof.
