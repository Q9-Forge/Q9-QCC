#!/usr/bin/env python3
# QCC Stack-IR Interpreter + Test-Orakel (siehe docs/ARCHITEKTUR.md Kapitel 10).
# Liest IR-Text (stdin oder Dateiargument), fuehrt ihn auf einer Operanden-Stack-
# Maschine mit Aufruf-Stack aus, startet bei Funktion 'main'. Ausgabe: PRINT-Werte.
import re
import struct
import sys


class SemanticReject(Exception):
    """Das Frontend hat die Eingabe erkannt, aber semantisch beanstandet.

    Kennzeichen ist das Schlusswort SEMERR auf stdout. Die vorangehende IR ist
    dann nicht vertrauenswuerdig (siehe parse_ir).
    """


# ZEIGERGROESSE DIESES ORAKELS.
# Groessen und Offsets, die einen Zeigeranteil haben, gibt das Frontend als
# "k+nP" aus (tcEmitNum in qcc.lextab): k ist der zeigerfreie Anteil in Byte,
# n die Zahl der Zeigergroessen darin. Jeder Konsument setzt sein eigenes P
# ein -- qir68k 4, qirarm64 8 -- damit dieselbe IR fuer beide Ziele gilt.
PTR_SIZE = 8

_PTR_TOKEN = re.compile(r"^(-?\d+)\+(\d+)P$")


def resolve_ptr_expr(tok):
    """'k+nP' zu seiner Zahl aufloesen; jedes andere Token unveraendert lassen.

    Die Aufloesung sitzt bewusst HIER, beim Einlesen, und nicht an den
    Verwendungsstellen: sonst muesste jedes int(args[i]) im Interpreter davon
    wissen, und eine vergessene Stelle waere ein stiller Rechenfehler.
    """
    m = _PTR_TOKEN.match(tok)
    if not m:
        return tok
    return str(int(m.group(1)) + PTR_SIZE * int(m.group(2)))


def parse_ir(text):
    prog = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        # Schlusswort des Frontends (siehe Kopf des erzeugten Parsers):
        # OK = uebersetzt, FAIL = Grammatik hat nicht erkannt,
        # SEMERR = erkannt, aber semantisch beanstandet. Bei SEMERR ist die
        # vorangehende IR ausdruecklich NICHT vertrauenswuerdig -- sie
        # auszufuehren wuerde ein falsches Ergebnis als Messwert ausgeben,
        # deshalb bricht dieses Orakel hier ab statt weiterzurechnen.
        if line == "SEMERR":
            raise SemanticReject()
        if line in ("OK", "FAIL"):
            continue
        parts = [resolve_ptr_expr(t) for t in line.split()]
        prog.append((parts[0], parts[1:]))
    return prog


def cdiv(a, b):
    # C-Semantik: Division schneidet Richtung 0 ab (nicht floor wie Python //).
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def cmod(a, b):
    return a if b == 0 else a - cdiv(a, b) * b


def u32(a):
    return a & 0xffffffff


class Pointer:
    """Zielneutrale Byteadresse in einen VM-Speicherblock."""
    __slots__ = ("block", "offset")

    def __init__(self, block, offset=0):
        self.block = block
        self.offset = offset

    def shifted(self, count, size):
        return Pointer(self.block, self.offset + count * size)


class FnRef:
    """Wert eines Funktionszeigers: der Name der Zielfunktion.

    Bewusst eine eigene Klasse und keine nackte Zeichenkette, damit CALLIND
    einen falsch typisierten Operanden erkennen kann statt ihn zu deuten.
    LOADP/STOREP/LOADG/STOREG behandeln Werte undurchsichtig, ein FnRef
    ueberlebt Variablen, Arrayelemente und struct-Felder daher unveraendert.
    """
    __slots__ = ("name",)

    def __init__(self, name):
        self.name = name


def type_size(tag):
    # 2026-09-09: 'h' (short) dazu -- echte 2 Byte, wie im 68k-Backend.
    return 1 if tag in ("c", "b") else 2 if tag == "h" else 8 if tag in ("p", "d") else 4


def mask_for(tag, value):
    # Nullerweiterung wie im 68k-Backend (moveq #0,d0 / move.b bzw. move.w) --
    # dieselbe Maske an vier Stellen (LOADIDX/STOREIDX/LOADIND/STOREIND).
    if isinstance(value, float):
        # MODELLGRENZE (2026-09-16): diese VM bildet Speicher als Liste
        # TYPISIERTER Zellen ab, nicht als Bytes. Eine byteweise struct-Kopie
        # (LOADIND c / STOREIND c, s. tcEmitStructCopy) trifft deshalb eine
        # double-Zelle als GANZES statt acht einzelne Bytes. Auf dem 68k ist
        # dieselbe Kopie echt byteweise und damit richtig -- das Bitmuster
        # wandert unveraendert. Hier wird die Zelle durchgereicht, statt an
        # "float & 0xff" mit einem TypeError abzubrechen; ohne das stuerzte
        # jedes struct mit double-Feld ab, sobald es kopiert wurde
        # (Zuweisung, Argument, Rueckgabewert).
        return value
    if tag in ("c", "b"):
        return value & 0xff
    if tag == "h":
        return value & 0xffff
    return value


def pointer(value, what="pointer operation"):
    if not isinstance(value, Pointer):
        raise RuntimeError("qccvm: %s requires a pointer" % what)
    return value


def pointer_index(value, tag):
    p = pointer(value, "dereference")
    size = type_size(tag)
    if p.offset % size:
        if tag == "d":
            # MODELLGRENZE (2026-09-16), keine Compilerfehler: diese VM bildet
            # einen Block als Liste TYPISIERTER Zellen ab und rechnet den Index
            # als offset//groesse. Ein double-Feld, das NICHT auf acht Byte
            # liegt -- etwa in "struct { int n; double d; }", wo d bei Offset 4
            # beginnt --, laesst sich darin nicht von einem int unterscheiden.
            # Auf dem Ziel ist genau dieses Layout RICHTIG: xcc richtet double
            # auf zwei Byte aus (68k-Wortausrichtung), und QCC stimmt damit
            # ueberein ("{int i; double d;}" = 12 Byte in beiden, s.
            # docs/FLOAT_PLAN_de.md). Geprueft wird der Fall deshalb auf echter
            # Hardware, in Q9-BACKEND-68K/q9-qclib/tests/double68k.sh.
            raise RuntimeError(
                "qccvm: double-Feld bei Offset %d (kein Vielfaches von 8) -- "
                "diese VM kann gemischte structs mit double nicht abbilden; "
                "auf dem Ziel ist das Layout korrekt, s. double68k.sh"
                % p.offset)
        raise RuntimeError("qccvm: unaligned pointer")
    index = p.offset // size
    if index < 0 or index >= len(p.block):
        raise RuntimeError("qccvm: pointer outside object")
    return p.block, index


def pointer_equal(a, b):
    if a == 0 or b == 0:
        return a == 0 and b == 0
    a = pointer(a, "comparison"); b = pointer(b, "comparison")
    return a.block is b.block and a.offset == b.offset


def run(prog):
    func_start = {}
    label_at = {}
    globals_ = {}
    for i, (op, args) in enumerate(prog):
        if op == "GLOBAL":
            globals_[args[0]] = [int(args[1]) if len(args) >= 2 else 0]
        elif op == "GARRAY":
            globals_[args[0]] = [0] * int(args[2])
        elif op == "GINIT":
            globals_[args[0]][int(args[1])] = int(args[2])
        elif op == "GINITD":
            # Anfangswert eines globalen double, als zwei 32-Bit-Haelften
            # (hi zuerst) -- dieselbe Darstellung wie PUSHD, weil die IR von
            # Werkzeugen gelesen wird, die selbst kein Gleitkomma haben.
            ghi = int(args[2]) & 0xffffffff
            glo = int(args[3]) & 0xffffffff
            globals_[args[0]][int(args[1])] = struct.unpack(">d", struct.pack(">II", ghi, glo))[0]
        elif op == "GINITADDR":
            # Die ADRESSE eines anderen Globalen als Anfangswert -- entsteht aus
            # einem String-Literal in einer Initialisiererliste
            # (char *tab[] = {"a","b"}). Auf dem Ziel steht dieser Wert erst zur
            # Ladezeit fest, OS-9 relokiert ihn ueber M$IRefs; hier ist er ein
            # gewoehnlicher Zeiger auf den Block des Ziels.
            globals_[args[0]][int(args[1])] = Pointer(globals_[args[2]])
        elif op == "FUNC":
            func_start[args[0]] = i + 1
        elif op == "LABEL":
            label_at[args[0]] = i
    if "main" not in func_start:
        sys.stderr.write("qccvm: keine Funktion 'main'\n")
        return 1

    opstack = []
    frames = [(-1, {}, {})]      # (return_ip, locals, arrays); -1 = Programmende
    ip = func_start["main"]
    steps = 0
    while True:
        steps += 1
        if steps > 20000000:
            sys.stderr.write("qccvm: Schrittlimit (Endlosschleife?)\n")
            return 2
        op, args = prog[ip]
        if op == "PUSH":
            opstack.append(int(args[0])); ip += 1
        elif op == "LOADL":
            opstack.append(frames[-1][1].setdefault(int(args[0]), [0])[0]); ip += 1
        elif op == "STOREL":
            frames[-1][1].setdefault(int(args[0]), [0])[0] = opstack.pop(); ip += 1
        elif op == "LOADC":
            opstack.append(frames[-1][1].setdefault(int(args[0]), [0])[0] & 0xff); ip += 1
        elif op == "STOREC":
            frames[-1][1].setdefault(int(args[0]), [0])[0] = opstack.pop() & 0xff; ip += 1
        elif op == "LOADLH":
            opstack.append(frames[-1][1].setdefault(int(args[0]), [0])[0] & 0xffff); ip += 1
        elif op == "STORELH":
            frames[-1][1].setdefault(int(args[0]), [0])[0] = opstack.pop() & 0xffff; ip += 1
        elif op == "LOADP":
            opstack.append(frames[-1][1].setdefault(int(args[0]), [0])[0]); ip += 1
        elif op == "STOREP":
            frames[-1][1].setdefault(int(args[0]), [0])[0] = opstack.pop(); ip += 1
        elif op == "LOADG":
            opstack.append(globals_[args[0]][0]); ip += 1
        elif op == "STOREG":
            globals_[args[0]][0] = opstack.pop(); ip += 1
        elif op == "LOADGC":
            opstack.append(globals_[args[0]][0] & 0xff); ip += 1
        elif op == "STOREGC":
            globals_[args[0]][0] = opstack.pop() & 0xff; ip += 1
        elif op == "LOADGH":
            opstack.append(globals_[args[0]][0] & 0xffff); ip += 1
        elif op == "STOREGH":
            globals_[args[0]][0] = opstack.pop() & 0xffff; ip += 1
        elif op == "LOADGP":
            opstack.append(globals_[args[0]][0]); ip += 1
        elif op == "STOREGP":
            globals_[args[0]][0] = opstack.pop(); ip += 1
        # ---- Gleitkomma (2026-09-16) -------------------------------------
        # Ein double liegt NICHT in einem Slot, sondern als Block -- die
        # Slots sind zielabhaengig breit (68k 4 Byte, ARM64 16), ein Block
        # ist es nicht. Siehe docs/FLOAT_IR_ENTWURF_de.md.
        #
        # ACHTUNG Genauigkeit: Python rechnet mit 64 Bit, die 68k-FPU
        # intern mit 80 (fadd.x). Vergleichstests gegen echte Hardware
        # duerfen deshalb nur Werte verwenden, die in beiden exakt sind.
        elif op == "PUSHD":
            hi = int(args[0]) & 0xffffffff
            lo = int(args[1]) & 0xffffffff
            opstack.append(struct.unpack(">d", struct.pack(">II", hi, lo))[0])
            ip += 1
        elif op == "LOADD":
            opstack.append(frames[-1][2][int(args[0])][0]); ip += 1
        elif op == "STORED":
            frames[-1][2][int(args[0])][0] = float(opstack.pop()); ip += 1
        elif op == "LOADGD":
            opstack.append(globals_[args[0]][0]); ip += 1
        elif op == "STOREGD":
            globals_[args[0]][0] = float(opstack.pop()); ip += 1
        elif op == "DADD":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a + b); ip += 1
        elif op == "DSUB":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a - b); ip += 1
        elif op == "DMUL":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a * b); ip += 1
        elif op == "DDIV":
            b = opstack.pop(); a = opstack.pop()
            if b == 0.0:
                sys.stderr.write("qccvm: Division durch null (double)\n")
                return 2
            opstack.append(a / b); ip += 1
        elif op == "DNEG":
            opstack.append(-opstack.pop()); ip += 1
        elif op in ("DCMPEQ", "DCMPNE", "DCMPLT", "DCMPLE", "DCMPGT", "DCMPGE"):
            b = opstack.pop(); a = opstack.pop()
            r = {"DCMPEQ": a == b, "DCMPNE": a != b, "DCMPLT": a < b,
                 "DCMPLE": a <= b, "DCMPGT": a > b, "DCMPGE": a >= b}[op]
            opstack.append(1 if r else 0); ip += 1
        elif op == "I2D":
            opstack.append(float(opstack.pop())); ip += 1
        elif op == "I2DUNDER":
            # Wandelt den Wert UNTER dem obersten um. Gebraucht fuer "1 + a":
            # da liegt die Ganzzahl schon unter dem double, und ein I2D auf
            # das oberste Element traefe den falschen Operanden.
            opstack[-2] = float(opstack[-2]); ip += 1
        elif op == "D2I":
            # C schneidet Richtung null ab, rundet nicht -- wie fintrz.
            opstack.append(int(opstack.pop())); ip += 1
        elif op == "DDUP":
            opstack.append(opstack[-1]); ip += 1
        elif op == "DDROP":
            opstack.pop(); ip += 1
        elif op == "LARRAY":
            frames[-1][2][int(args[0]) if args[0].isdigit() else args[0]] = [0] * int(args[2]); ip += 1
        elif op == "PUSHADDR":
            if args[0] == "P":
                opstack.append(frames[-1][1][int(args[1])][0])
            else:
                block = frames[-1][2][int(args[1])] if args[0] == "L" else globals_[args[1]]
                opstack.append(Pointer(block))
            ip += 1
        elif op == "ADDRL":
            opstack.append(Pointer(frames[-1][1].setdefault(int(args[0]), [0]))); ip += 1
        elif op == "ADDRG":
            opstack.append(Pointer(globals_[args[0]])); ip += 1
        elif op == "LOADIDX":
            index = opstack.pop()
            values = frames[-1][2][int(args[1])] if args[0] == "L" else frames[-1][1][int(args[1])] if args[0] == "P" else globals_[args[1]]
            if index < 0 or index >= len(values):
                sys.stderr.write("qccvm: array index %d out of range (length %d)\n" % (index, len(values))); return 4
            value = values[index]
            opstack.append(mask_for(args[2], value)); ip += 1
        elif op in ("STOREIDX", "STOREIDXKEEP"):
            value = opstack.pop(); index = opstack.pop()
            values = frames[-1][2][int(args[1])] if args[0] == "L" else frames[-1][1][int(args[1])] if args[0] == "P" else globals_[args[1]]
            if index < 0 or index >= len(values):
                sys.stderr.write("qccvm: array index %d out of range (length %d)\n" % (index, len(values))); return 4
            value = mask_for(args[2], value)
            values[index] = value
            if op == "STOREIDXKEEP": opstack.append(value)
            ip += 1
        elif op == "PTRINDEX":
            p = pointer(opstack.pop(), "indexing"); index = opstack.pop()
            opstack.append(p.shifted(index, type_size(args[0]))); ip += 1
        elif op == "LOADIND":
            block, index = pointer_index(opstack.pop(), args[0]); value = block[index]
            opstack.append(mask_for(args[0], value)); ip += 1
        elif op in ("STOREIND", "STOREINDKEEP"):
            value = opstack.pop(); block, index = pointer_index(opstack.pop(), args[0])
            value = mask_for(args[0], value)
            block[index] = value
            if op == "STOREINDKEEP": opstack.append(value)
            ip += 1
        elif op == "PADD":
            count = opstack.pop(); p = pointer(opstack.pop(), "addition")
            opstack.append(p.shifted(count, type_size(args[0]))); ip += 1
        elif op == "IPADD":
            p = pointer(opstack.pop(), "addition"); count = opstack.pop()
            opstack.append(p.shifted(count, type_size(args[0]))); ip += 1
        elif op == "IPADDN":
            # wie IPADD, aber Skalierung um eine LAUFZEIT-Byte-Groesse (z.B. structByteSize)
            # statt einer festen Typtag-Groesse -- gebraucht fuer arr[i].feld (Array von structs).
            p = pointer(opstack.pop(), "addition"); count = opstack.pop()
            opstack.append(p.shifted(count, int(args[0]))); ip += 1
        elif op == "PSUB":
            count = opstack.pop(); p = pointer(opstack.pop(), "subtraction")
            opstack.append(p.shifted(-count, type_size(args[0]))); ip += 1
        elif op == "PDIFF":
            b = pointer(opstack.pop(), "subtraction"); a = pointer(opstack.pop(), "subtraction")
            if a.block is not b.block: raise RuntimeError("qccvm: subtraction of unrelated pointers")
            opstack.append(cdiv(a.offset - b.offset, type_size(args[0]))); ip += 1
        elif op == "ADD":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a + b); ip += 1
        elif op == "SUB":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a - b); ip += 1
        elif op == "MUL":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a * b); ip += 1
        elif op == "DIV":
            b = opstack.pop(); a = opstack.pop(); opstack.append(cdiv(a, b)); ip += 1
        elif op == "UDIV":
            b = u32(opstack.pop()); a = u32(opstack.pop()); opstack.append(0 if b == 0 else a // b); ip += 1
        elif op == "MOD":
            b = opstack.pop(); a = opstack.pop(); opstack.append(cmod(a, b)); ip += 1
        elif op == "UMOD":
            b = u32(opstack.pop()); a = u32(opstack.pop()); opstack.append(a if b == 0 else a % b); ip += 1
        elif op == "NEG":
            opstack.append(-opstack.pop()); ip += 1
        elif op == "NOT":
            opstack.append(0 if opstack.pop() else 1); ip += 1
        elif op == "NOTBIT":
            opstack.append(u32(~opstack.pop())); ip += 1
        elif op == "BAND":
            b = opstack.pop(); a = opstack.pop(); opstack.append(u32(a & b)); ip += 1
        elif op == "BXOR":
            b = opstack.pop(); a = opstack.pop(); opstack.append(u32(a ^ b)); ip += 1
        elif op == "BOR":
            b = opstack.pop(); a = opstack.pop(); opstack.append(u32(a | b)); ip += 1
        elif op == "SHL":
            b = opstack.pop(); a = opstack.pop(); opstack.append(u32(a << b)); ip += 1
        elif op == "SHR":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a >> b); ip += 1
        elif op == "USHR":
            b = opstack.pop(); a = opstack.pop(); opstack.append(u32(a) >> b); ip += 1
        elif op == "NARROWC":
            opstack.append(opstack.pop() & 0xff); ip += 1
        elif op == "NARROWH":
            opstack.append(opstack.pop() & 0xffff); ip += 1
        elif op == "DUP":
            opstack.append(opstack[-1]); ip += 1
        elif op == "DUPP":
            opstack.append(opstack[-1]); ip += 1
        elif op == "SWAP":
            b = opstack.pop(); a = opstack.pop(); opstack.append(b); opstack.append(a); ip += 1
        elif op == "CMPLT":
            b = opstack.pop(); a = opstack.pop(); opstack.append(1 if a < b else 0); ip += 1
        elif op == "CMPGT":
            b = opstack.pop(); a = opstack.pop(); opstack.append(1 if a > b else 0); ip += 1
        elif op == "CMPLE":
            b = opstack.pop(); a = opstack.pop(); opstack.append(1 if a <= b else 0); ip += 1
        elif op == "CMPGE":
            b = opstack.pop(); a = opstack.pop(); opstack.append(1 if a >= b else 0); ip += 1
        elif op == "CMPEQ":
            b = opstack.pop(); a = opstack.pop(); opstack.append(1 if a == b else 0); ip += 1
        elif op == "CMPNE":
            b = opstack.pop(); a = opstack.pop(); opstack.append(1 if a != b else 0); ip += 1
        elif op == "CMPULT":
            b = u32(opstack.pop()); a = u32(opstack.pop()); opstack.append(1 if a < b else 0); ip += 1
        elif op == "CMPUGT":
            b = u32(opstack.pop()); a = u32(opstack.pop()); opstack.append(1 if a > b else 0); ip += 1
        elif op == "CMPULE":
            b = u32(opstack.pop()); a = u32(opstack.pop()); opstack.append(1 if a <= b else 0); ip += 1
        elif op == "CMPUGE":
            b = u32(opstack.pop()); a = u32(opstack.pop()); opstack.append(1 if a >= b else 0); ip += 1
        elif op.startswith("PCMP"):
            b = opstack.pop(); a = opstack.pop()
            if op == "PCMPEQ": result = pointer_equal(a, b)
            elif op == "PCMPNE": result = not pointer_equal(a, b)
            else:
                a = pointer(a, "comparison"); b = pointer(b, "comparison")
                if a.block is not b.block: raise RuntimeError("qccvm: comparison of unrelated pointers")
                result = a.offset < b.offset if op == "PCMPLT" else a.offset <= b.offset if op == "PCMPLE" else a.offset > b.offset if op == "PCMPGT" else a.offset >= b.offset
            opstack.append(1 if result else 0); ip += 1
        elif op == "GLOBAL" or op == "GARRAY" or op == "GINIT" or op == "GINITD" or op == "GINITADDR" or op == "LABEL" or op == "FUNC" or op == "ENDFUNC":
            ip += 1
        elif op == "JMP":
            ip = label_at[args[0]]
        elif op == "JZ":
            ip = label_at[args[0]] if opstack.pop() == 0 else ip + 1
        elif op == "JNZ":
            ip = label_at[args[0]] if opstack.pop() != 0 else ip + 1
        elif op == "CALL" or op == "CALLP":
            name = args[0]; n = int(args[1])
            callargs = [opstack.pop() for _ in range(n)][::-1]
            newlocals = {k: [callargs[k]] for k in range(n)}
            frames.append((ip + 1, newlocals, {}))
            ip = func_start[name]
        elif op == "PUSHFN":
            opstack.append(FnRef(args[0])); ip += 1
        elif op == "CALLIND" or op == "CALLINDP":
            # Stapelbelegung wie beim 68k-Backend (siehe dessen CALLIND-Zweig):
            # ZUERST der Funktionszeiger, DARUEBER arg1..argN -- diese Reihenfolge
            # ergibt sich aus dem Parsen, weil der Callee-Ausdruck vor den
            # Argumenten ausgewertet wird. Also erst die Argumente abheben,
            # danach den Zeiger.
            n = int(args[0])
            callargs = [opstack.pop() for _ in range(n)][::-1]
            fnref = opstack.pop()
            if not isinstance(fnref, FnRef):
                print("qccvm: CALLIND ueber einen Wert, der kein Funktionszeiger ist",
                      file=sys.stderr)
                return 1
            if fnref.name not in func_start:
                print("qccvm: CALLIND auf unbekannte Funktion '%s'" % fnref.name,
                      file=sys.stderr)
                return 1
            newlocals = {k: [callargs[k]] for k in range(n)}
            frames.append((ip + 1, newlocals, {}))
            ip = func_start[fnref.name]
        elif op == "RET" or op == "RETP":
            retval = opstack.pop()
            ret_ip, _, _ = frames.pop()
            if not frames:
                return 0                # main zurueck -> Programmende
            opstack.append(retval)
            ip = ret_ip
        elif op == "DROP":
            opstack.pop(); ip += 1
        elif op == "PRINT":
            print(opstack.pop()); ip += 1
        elif op == "PRINTU":
            print(opstack.pop() & 0xffffffff); ip += 1
        elif op == "PRINTC":
            sys.stdout.write(chr(opstack.pop() & 0xff)); ip += 1
        else:
            sys.stderr.write("qccvm: unbekannter Opcode %r\n" % op)
            return 3


def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1]) as f:
            text = f.read()
    else:
        text = sys.stdin.read()
    try:
        prog = parse_ir(text)
    except SemanticReject:
        sys.stderr.write("qccvm: Eingabe war semantisch beanstandet (SEMERR) -- "
                         "die IR wird nicht ausgefuehrt\n")
        return 1
    if not any(op == "FUNC" for op, _ in prog):
        sys.stderr.write("qccvm: keine IR (Parse fehlgeschlagen?)\n")
        return 1
    return run(prog)


if __name__ == "__main__":
    sys.exit(main())
