#!/usr/bin/env python3
# ================================================================================
# s68sim.py -- Mini-Simulator fuer die von ebnf erzeugten 68k-Parser (.s68)
#
# Zweck: den ERZEUGTEN Assembler-Text wirklich ausfuehren, ohne 68k-Toolchain.
# Unterstuetzt genau die Instruktions-Teilmenge, die codegen.cpp emittiert.
# Der Stack (a7) wird typisiert modelliert (Ruecksprungadresse vs. gesicherter
# a0-Wert) -- ein unbalancierter Stack im generierten Code fuehrt damit sofort
# zu einem harten Simulationsfehler statt zu stillem Fehlverhalten.
#
# Aufruf:  s68sim.py <datei.s68> "<eingabe>"
# Ausgabe: OK (vollstaendig erkannt) / FAIL, Exit-Code 0/1, 3 bei Sim-Fehler.
# ================================================================================
import re
import sys

MAX_STEPS = 2_000_000


class SimError(Exception):
    pass


def load(path):
    """liest die .s68, liefert (instr-Liste, label->index)"""
    instrs = []
    labels = {}
    with open(path) as f:
        for raw in f:
            line = raw.split(';', 1)[0].rstrip()
            if not line.strip():
                continue
            m = re.match(r'^(\w+):\s*(.*)$', line)
            if m:
                labels[m.group(1)] = len(instrs)
                line = m.group(2)
            # Whitespace normalisieren (Generator trennt mit Tabs): genau ein
            # Leerzeichen zwischen Mnemonic und Operanden, keins in Operanden
            parts = line.split(None, 1)
            if not parts:
                continue
            if len(parts) == 1:
                line = parts[0]
            else:
                line = parts[0] + ' ' + re.sub(r'\s+', '', parts[1])
            instrs.append(line)
    return instrs, labels


def run(instrs, labels, text):
    inp = text.encode('latin-1')

    def mem(i):
        return inp[i] if 0 <= i < len(inp) else 0

    a0 = 0
    d0 = 0
    d1 = 0
    d2 = 0
    stack = []            # Eintraege: ('ret', pc) | ('val', a0wert)
    flag_eq = False
    flag_lo = False       # dst < imm (unsigned), fuer blo/bhi nach cmpi
    flag_hi = False

    if 'parse' not in labels:
        raise SimError("Label 'parse' fehlt")
    pc = labels['parse']
    stack.append(('ret', -1))         # Ruecksprung "nach draussen"
    steps = 0

    while True:
        steps += 1
        if steps > MAX_STEPS:
            raise SimError('Schrittlimit erreicht (Endlosschleife?)')
        if pc < 0 or pc >= len(instrs):
            raise SimError(f'PC ausserhalb des Programms: {pc}')
        ins = instrs[pc]
        pc += 1

        m = re.match(r'cmpi\.b\s+#\$([0-9A-Fa-f]+),(.+)$', ins)
        if m:
            imm = int(m.group(1), 16)
            dst = m.group(2).strip()
            if dst == '(a0)':
                val = mem(a0)
            elif dst == 'd1':
                val = d1
            else:
                m2 = re.match(r'(\d+)\(a0\)$', dst)
                if not m2:
                    raise SimError(f'cmpi-Ziel unbekannt: {ins}')
                val = mem(a0 + int(m2.group(1)))
            flag_eq = (val == imm)
            flag_lo = (val < imm)
            flag_hi = (val > imm)
            continue
        if ins == 'move.b (a0),d1':
            d1 = mem(a0)
            flag_eq = (d1 == 0)          # MOVE setzt Z/N -- der Generator nutzt beq danach
            continue
        m = re.match(r'move\.b\s+(\d+)\(a0\),d1$', ins)
        if m:
            d1 = mem(a0 + int(m.group(1)))
            flag_eq = (d1 == 0)
            continue
        if ins == 'move.l a0,-(a7)':
            stack.append(('val', a0))
            continue
        if ins == 'move.l (a7),a0':
            kind, v = stack[-1]
            if kind != 'val':
                raise SimError('move.l (a7),a0: oben liegt keine gesicherte Position')
            a0 = v
            continue
        if ins == 'move.l (a7)+,a0':
            kind, v = stack.pop()
            if kind != 'val':
                raise SimError('move.l (a7)+,a0: oben liegt keine gesicherte Position')
            a0 = v
            continue
        m = re.match(r'addq\.l\s+#(\d+),(a0|a7)$', ins)
        if m:
            n = int(m.group(1))
            if m.group(2) == 'a0':
                a0 += n
            else:
                if n % 4:
                    raise SimError(f'addq auf a7 nicht longword-aligned: {ins}')
                for _ in range(n // 4):
                    kind, _v = stack.pop()
                    if kind != 'val':
                        raise SimError('addq a7 verwirft eine Ruecksprungadresse!')
            continue
        m = re.match(r'lea\s+(\d+)\(a0\),a0$', ins)
        if m:
            a0 += int(m.group(1))
            continue
        m = re.match(r'(bne|beq|blo|bhi|bls|bhs|bra)\s+(\w+)$', ins)
        if m:
            op, target = m.groups()
            if target not in labels:
                raise SimError(f'unbekanntes Label: {target}')
            take = {'bne': not flag_eq, 'beq': flag_eq,
                    'blo': flag_lo, 'bhi': flag_hi,
                    'bls': flag_lo or flag_eq, 'bhs': flag_hi or flag_eq,
                    'bra': True}[op]
            if take:
                pc = labels[target]
            continue
        m = re.match(r'bsr\s+(\w+)$', ins)
        if m:
            target = m.group(1)
            if target not in labels:
                raise SimError(f'unbekanntes Unterprogramm: {target}')
            stack.append(('ret', pc))
            pc = labels[target]
            continue
        if ins == 'rts':
            kind, v = stack.pop()
            if kind != 'ret':
                raise SimError('rts: oben liegt keine Ruecksprungadresse (Stack-Leck!)')
            if v == -1:
                return d0, a0, stack
            pc = v
            continue
        m = re.match(r'moveq\s+#(-?\d+),(d0|d2)$', ins)
        if m:
            val = int(m.group(1)) & 0xFF
            if m.group(2) == 'd0':
                d0 = val
            else:
                d2 = val
            flag_eq = (val == 0)
            continue
        m = re.match(r'(addq|subq)\.l\s+#(\d+),d2$', ins)
        if m:
            n = int(m.group(2))
            d2 = (d2 + n) if m.group(1) == 'addq' else (d2 - n)
            d2 &= 0xFFFFFFFF
            flag_eq = (d2 == 0)          # Generator verlaesst sich auf beq nach subq
            continue
        if ins == 'tst.b d0':
            flag_eq = (d0 & 0xFF) == 0
            continue
        raise SimError(f'Instruktion nicht unterstuetzt: {ins}')


def main():
    if len(sys.argv) < 3:
        print('usage: s68sim.py <datei.s68> "<eingabe>"', file=sys.stderr)
        return 2
    instrs, labels = load(sys.argv[1])
    try:
        d0, a0, stack = run(instrs, labels, sys.argv[2])
    except SimError as e:
        print(f'SIMFEHLER: {e}', file=sys.stderr)
        return 3
    if stack:
        print(f'SIMFEHLER: Stack nicht leer am Ende: {stack}', file=sys.stderr)
        return 3
    if d0 == 1 and a0 == len(sys.argv[2].encode("latin-1")):
        print('OK')
        return 0
    print('FAIL')
    return 1


if __name__ == '__main__':
    sys.exit(main())
