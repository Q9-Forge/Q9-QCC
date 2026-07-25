#!/usr/bin/env python3
"""Test-Simulator fuer den vom Tiny-C-C++-Backend erzeugten 68000-Assembler.

Kein Teil der auszuliefernden Toolchain: Er prueft die feste Backend-Schablonen
gegen tools/tinyvm.py, solange die OS-9/Q9-Runtime noch nicht vorhanden ist.
"""
import re
import sys

MAX_STEPS = 2_000_000
MASK = 0xFFFFFFFF


class SimError(Exception):
    pass


def u32(value):
    return value & MASK


def s32(value):
    value &= MASK
    return value - 0x100000000 if value & 0x80000000 else value


def load(path):
    instructions, labels, global_initials = [], {}, {}
    current_global, current_offset = None, 0
    with open(path) as source:
        for raw in source:
            line = raw.split(";", 1)[0].strip()
            if not line:
                continue
            match = re.match(r"^(\w+):\s*(.*)$", line)
            if match:
                labels[match.group(1)] = len(instructions)
                line = match.group(2).strip()
                if match.group(1).startswith("tc_g_"):
                    current_global, current_offset = match.group(1), 0
            if current_global and (line.startswith("dc.l") or line.startswith("dc.b")):
                value = int(line.split()[1], 0)
                global_initials[(current_global, current_offset)] = value
                if current_offset == 0:
                    global_initials[current_global] = value
                current_offset += 1 if line.startswith("dc.b") else 4
                line = ""
            if line:
                instructions.append(re.sub(r"\s+", " ", line))
    return instructions, labels, global_initials


def run(instructions, labels, global_initials=None):
    if "tc_start" not in labels:
        raise SimError("Label tc_start fehlt")
    memory = {}
    # tc_extcall_tmp: festes Scratch-Feld fuer CALLEXT/CALLEXTP (siehe tinyc_backend_c.cpp),
    # bewusst OHNE "tc_g_"-Praefix (kollisionsfrei zu echten Tiny-C-Globalen), daher hier
    # explizit mit aufgenommen statt ueber das generische "tc_g_"-Praefixmuster.
    global_addresses = {name: 0x200000 + i * 0x10000 for i, name in enumerate(
        name for name in labels if name.startswith("tc_g_") or name == "tc_extcall_tmp")}
    for key, value in (global_initials or {}).items():
        if isinstance(key, tuple):
            name, offset = key; memory[global_addresses[name] + offset] = u32(value)
        elif key in global_addresses:
            memory[global_addresses[key]] = u32(value)
    d = [0] * 8
    a6 = 0
    a7 = 0x100000
    a0_global = None
    output = []
    flags = {"z": False, "n": False, "v": False, "c": False, "x": False}

    def push(value):
        nonlocal a7
        a7 -= 4
        memory[a7] = value if value is None else u32(value)

    def pop():
        nonlocal a7
        if a7 not in memory:
            raise SimError("Stack-Unterlauf")
        value = memory.pop(a7)
        a7 += 4
        return value

    def read_operand(text):
        text = text.strip()
        match = re.match(r"#(-?\d+)$", text)
        if match: return int(match.group(1))
        match = re.match(r"d([0-7])$", text)
        if match: return d[int(match.group(1))]
        if text == "a6": return a6
        if text == "a7": return a7
        if text == "a0":
            if a0_global is None: raise SimError("a0 zeigt auf keine Adresse")
            return a0_global
        if text == "(a7)":
            if a7 not in memory: raise SimError("Stack-Unterlauf")
            return memory[a7]
        match = re.match(r"(-?\d+)\(a7\)$", text)
        if match:
            return memory.get(a7 + int(match.group(1)), 0)
        match = re.match(r"(-?\d+)\(a6\)$", text)
        if match:
            return memory.get(a6 + int(match.group(1)), 0)
        match = re.match(r"(tc_g_\w+|tc_extcall_tmp)\(pc\)$", text)
        if match:
            return memory.get(global_addresses[match.group(1)], 0)
        if text == "(a0)":
            if a0_global is None:
                raise SimError("a0 zeigt auf keine Adresse")
            return memory.get(a0_global, 0)
        match = re.match(r"(-?\d+)\(a0\)$", text)
        if match:
            if a0_global is None:
                raise SimError("a0 zeigt auf keine Adresse")
            return memory.get(a0_global + int(match.group(1)), 0)
        raise SimError("unbekannter Operand: " + text)

    def write_operand(text, value):
        nonlocal a0_global, a6, a7
        value = u32(value)
        text = text.strip()
        match = re.match(r"d([0-7])$", text)
        if match:
            d[int(match.group(1))] = value
            return
        if text == "a6": a6 = value; return
        if text == "a7": a7 = value; return
        if text == "a0": a0_global = value; return
        if text == "(a7)":
            if a7 not in memory: raise SimError("Stack-Unterlauf")
            memory[a7] = value
            return
        match = re.match(r"(-?\d+)\(a7\)$", text)
        if match:
            memory[a7 + int(match.group(1))] = value
            return
        match = re.match(r"(-?\d+)\(a6\)$", text)
        if match:
            memory[a6 + int(match.group(1))] = value
            return
        match = re.match(r"(tc_g_\w+|tc_extcall_tmp)\(pc\)$", text)
        if match:
            memory[global_addresses[match.group(1)]] = value
            return
        if text == "(a0)":
            if a0_global is None:
                raise SimError("a0 zeigt auf keine Adresse")
            memory[a0_global] = value
            return
        match = re.match(r"(-?\d+)\(a0\)$", text)
        if match:
            if a0_global is None:
                raise SimError("a0 zeigt auf keine Adresse")
            memory[a0_global + int(match.group(1))] = value
            return
        raise SimError("unbekanntes Ziel: " + text)

    # tc_start wird wie von einer Host-/OS-Runtime aufgerufen.
    push(None)
    pc = labels["tc_start"]
    steps = 0
    while True:
        steps += 1
        if steps > MAX_STEPS:
            raise SimError("Schrittlimit (Endlosschleife?)")
        if pc < 0 or pc >= len(instructions):
            raise SimError("PC ausserhalb des Programms")
        ins = instructions[pc]
        pc += 1

        match = re.match(r"bsr (\w+)$", ins)
        if match:
            target = match.group(1)
            # Die M4a-Ausgabe enthaelt PIC-Stubs; der Testsimulator ersetzt sie
            # durch die beabsichtigte Runtime-Semantik.
            if target == "tc_putint":
                output.append(str(s32(d[0])) + "\n")
                continue
            if target == "tc_putuint":
                output.append(str(u32(d[0])) + "\n")
                continue
            if target == "tc_putchar":
                output.append(chr(d[0] & 0xff))
                continue
            if target not in labels:
                raise SimError("unbekanntes Unterprogramm: " + target)
            push(pc)
            pc = labels[target]
            continue
        match = re.match(r"jsr (\w+)$", ins)
        if match:
            # CALLEXT/CALLEXTP (siehe tinyc_backend_c.cpp) ruft externe Funktionen per
            # "jsr <name>" auf (kein "tc_"-Praefix wie bei internen Aufrufen). Fuer diesen
            # Test-Simulator identisch zu "bsr" behandelt (push+jump) -- der Zieltext MUSS
            # ein lokal im selben .s68 vorhandenes Label sein (z.B. ein Test-Mock, der eine
            # echte clib-Funktion nachbildet); echte externe Symbolaufloesung passiert erst
            # beim spaeteren Linken gegen die reale clib.l (l68), nicht hier.
            target = match.group(1)
            if target not in labels:
                raise SimError("unbekanntes externes Unterprogramm (kein lokales Test-Mock vorhanden): " + target)
            push(pc)
            pc = labels[target]
            continue
        match = re.match(r"bra (\w+)$", ins)
        if match:
            pc = labels[match.group(1)]
            continue
        match = re.match(r"b(eq|ne|lt|gt|le|ge|pl|mi|cc|cs|hi|ls) (\w+)$", ins)
        if match:
            kind, target = match.groups()
            take = {"eq": flags["z"], "ne": not flags["z"],
                    "lt": flags["n"] != flags["v"], "ge": flags["n"] == flags["v"],
                    "gt": not flags["z"] and flags["n"] == flags["v"],
                    "le": flags["z"] or flags["n"] != flags["v"],
                    "pl": not flags["n"], "mi": flags["n"],
                    "cc": not flags["c"], "cs": flags["c"],
                    "hi": not flags["c"] and not flags["z"],
                    "ls": flags["c"] or flags["z"]}[kind]
            if take: pc = labels[target]
            continue
        if ins == "rts":
            ret = pop()
            if ret is None:
                return "".join(output)
            pc = ret
            continue
        match = re.match(r"link a6,#(-?\d+)$", ins)
        if match:
            push(a6)
            a6 = a7
            a7 += int(match.group(1))
            continue
        if ins == "unlk a6":
            a7 = a6
            a6 = pop()
            continue
        match = re.match(r"move\.l #(-?\d+),-\(a7\)$", ins)
        if match:
            push(int(match.group(1)))
            continue
        match = re.match(r"move\.l (.+),-\(a7\)$", ins)
        if match:
            push(read_operand(match.group(1)))
            continue
        match = re.match(r"move\.b (.+),(d[0-7])$", ins)
        if match:
            write_operand(match.group(2), read_operand(match.group(1)) & 0xff)
            continue
        match = re.match(r"move\.l \(a7\)\+,(.+)$", ins)
        if match:
            write_operand(match.group(1), pop())
            continue
        match = re.match(r"move\.b (d[0-7]),(.+)$", ins)
        if match:
            write_operand(match.group(2), read_operand(match.group(1)) & 0xff)
            continue
        match = re.match(r"move\.l (.+),(d[0-7])$", ins)
        if match:
            write_operand(match.group(2), read_operand(match.group(1)))
            continue
        match = re.match(r"move\.l (.+),a0$", ins)
        if match:
            a0_global = read_operand(match.group(1))
            continue
        match = re.match(r"move\.l (d[0-7]),\(a0\)$", ins)
        if match:
            write_operand("(a0)", read_operand(match.group(1)))
            continue
        match = re.match(r"move\.l (d[0-7]),\(a7\)$", ins)
        if match:
            write_operand("(a7)", read_operand(match.group(1)))
            continue
        match = re.match(r"add\.l \(a7\)\+,(d[0-7])$", ins)
        if match:
            write_operand(match.group(1), read_operand(match.group(1)) + pop())
            continue
        match = re.match(r"add\.l (d[0-7]),(d[0-7])$", ins)
        if match:
            src, dst = match.groups()
            write_operand(dst, read_operand(dst) + read_operand(src))
            continue
        match = re.match(r"adda?\.l (d[0-7]),a0$", ins)
        if match:
            delta = s32(read_operand(match.group(1)))
            a0_global += delta
            continue
        match = re.match(r"lsl\.l #(\d+),(d[0-7])$", ins)
        if match:
            count, dst = match.groups()
            count = int(count)
            old = read_operand(dst)
            carry = bool(old & (1 << (32 - count)))
            flags["c"] = flags["x"] = carry
            write_operand(dst, u32(old << count))
            continue
        match = re.match(r"asr\.l #(\d+),(d[0-7])$", ins)
        if match:
            count, dst = int(match.group(1)), match.group(2)
            write_operand(dst, s32(read_operand(dst)) >> count)
            continue
        match = re.match(r"sub\.l (d[0-7]),(d[0-7])$", ins)
        if match:
            write_operand(match.group(2), read_operand(match.group(2)) - read_operand(match.group(1)))
            continue
        if ins == "neg.l (a7)":
            if a7 not in memory: raise SimError("Stack-Unterlauf bei NEG")
            memory[a7] = u32(-s32(memory[a7]))
            continue
        if ins == "not.l (a7)":
            if a7 not in memory: raise SimError("Stack-Unterlauf bei NOTBIT")
            memory[a7] = u32(~memory[a7])
            continue
        match = re.match(r"neg\.l (d[0-7])$", ins)
        if match:
            write_operand(match.group(1), -s32(read_operand(match.group(1))))
            continue
        match = re.match(r"cmp\.l (d[0-7]),(d[0-7])$", ins)
        if match:
            src, dst = match.groups()
            result = s32(read_operand(dst)) - s32(read_operand(src))
            flags["z"] = result == 0
            flags["n"] = result < 0
            flags["v"] = False
            flags["c"] = read_operand(dst) < read_operand(src)
            continue
        match = re.match(r"tst\.l (d[0-7])$", ins)
        if match:
            value = s32(read_operand(match.group(1)))
            flags["z"], flags["n"], flags["v"] = value == 0, value < 0, False
            continue
        match = re.match(r"seq (d[0-7])$", ins)
        if match:
            write_operand(match.group(1), 0xff if flags["z"] else 0)
            continue
        match = re.match(r"moveq #(-?\d+),(d[0-7])$", ins)
        if match:
            write_operand(match.group(2), int(match.group(1)))
            continue
        match = re.match(r"andi\.l #(\d+),(d[0-7])$", ins)
        if match:
            write_operand(match.group(2), read_operand(match.group(2)) & int(match.group(1)))
            continue
        match = re.match(r"eori\.l #1,(d[0-7])$", ins)
        if match:
            write_operand(match.group(1), read_operand(match.group(1)) ^ 1)
            continue
        match = re.match(r"addq\.l #1,(d[0-7])$", ins)
        if match:
            write_operand(match.group(1), read_operand(match.group(1)) + 1)
            continue
        match = re.match(r"ls[lr]\.l #1,(d[0-7])$", ins)
        if match:
            op, reg = ins.split()[0].split("."), match.group(1)
            old = read_operand(reg)
            if op[0] == "lsr":
                flags["c"] = flags["x"] = bool(old & 1)
                write_operand(reg, old >> 1)
            else:
                flags["c"] = flags["x"] = bool(old & 0x80000000)
                write_operand(reg, old << 1)
            continue
        match = re.match(r"roxl\.l #1,(d[0-7])$", ins)
        if match:
            reg, old = match.group(1), read_operand(match.group(1))
            extend = flags["x"]
            flags["c"] = flags["x"] = bool(old & 0x80000000)
            write_operand(reg, (old << 1) | int(extend))
            continue
        match = re.match(r"dbra (d[0-7]),(\w+)$", ins)
        if match:
            reg, target = match.groups()
            value = (read_operand(reg) - 1) & 0xffff
            write_operand(reg, (read_operand(reg) & 0xffff0000) | value)
            if value != 0xffff:
                pc = labels[target]
            continue
        match = re.match(r"lea (\d+)\(a7\),a7$", ins)
        if match:
            a7 += int(match.group(1))
            continue
        match = re.match(r"lea (-?\d+)\(a6\),a0$", ins)
        if match:
            a0_global = a6 + int(match.group(1))
            continue
        match = re.match(r"lea (tc_g_\w+|tc_extcall_tmp)\(pc\),a0$", ins)
        if match:
            a0_global = global_addresses[match.group(1)]
            continue
        match = re.match(r"addq\.l #4,a7$", ins)
        if match:
            a7 += 4
            continue
        raise SimError("Instruktion nicht unterstuetzt: " + ins)


def main():
    if len(sys.argv) != 2:
        print("usage: tiny68sim.py <program.s68>", file=sys.stderr)
        return 2
    try:
        sys.stdout.write(run(*load(sys.argv[1])))
    except (OSError, SimError) as error:
        print("tiny68sim: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
