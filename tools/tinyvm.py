#!/usr/bin/env python3
# Tiny-C Stack-IR Interpreter + Test-Orakel (siehe docs/ARCHITEKTUR.md Kapitel 10).
# Liest IR-Text (stdin oder Dateiargument), fuehrt ihn auf einer Operanden-Stack-
# Maschine mit Aufruf-Stack aus, startet bei Funktion 'main'. Ausgabe: PRINT-Werte.
import sys


def parse_ir(text):
    prog = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        if line in ("OK", "FAIL"):
            continue
        parts = line.split()
        prog.append((parts[0], parts[1:]))
    return prog


def cdiv(a, b):
    # C-Semantik: Division schneidet Richtung 0 ab (nicht floor wie Python //).
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def run(prog):
    func_start = {}
    label_at = {}
    for i, (op, args) in enumerate(prog):
        if op == "FUNC":
            func_start[args[0]] = i + 1
        elif op == "LABEL":
            label_at[args[0]] = i
    if "main" not in func_start:
        sys.stderr.write("tinyvm: keine Funktion 'main'\n")
        return 1

    opstack = []
    frames = [(-1, {})]          # (return_ip, locals); -1 = Programmende
    ip = func_start["main"]
    steps = 0
    while True:
        steps += 1
        if steps > 20000000:
            sys.stderr.write("tinyvm: Schrittlimit (Endlosschleife?)\n")
            return 2
        op, args = prog[ip]
        if op == "PUSH":
            opstack.append(int(args[0])); ip += 1
        elif op == "LOADL":
            opstack.append(frames[-1][1].get(int(args[0]), 0)); ip += 1
        elif op == "STOREL":
            frames[-1][1][int(args[0])] = opstack.pop(); ip += 1
        elif op == "ADD":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a + b); ip += 1
        elif op == "SUB":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a - b); ip += 1
        elif op == "MUL":
            b = opstack.pop(); a = opstack.pop(); opstack.append(a * b); ip += 1
        elif op == "DIV":
            b = opstack.pop(); a = opstack.pop(); opstack.append(cdiv(a, b)); ip += 1
        elif op == "NEG":
            opstack.append(-opstack.pop()); ip += 1
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
        elif op == "LABEL" or op == "FUNC" or op == "ENDFUNC":
            ip += 1
        elif op == "JMP":
            ip = label_at[args[0]]
        elif op == "JZ":
            ip = label_at[args[0]] if opstack.pop() == 0 else ip + 1
        elif op == "JNZ":
            ip = label_at[args[0]] if opstack.pop() != 0 else ip + 1
        elif op == "CALL":
            name = args[0]; n = int(args[1])
            callargs = [opstack.pop() for _ in range(n)][::-1]
            newlocals = {k: callargs[k] for k in range(n)}
            frames.append((ip + 1, newlocals))
            ip = func_start[name]
        elif op == "RET":
            retval = opstack.pop()
            ret_ip, _ = frames.pop()
            if not frames:
                return 0                # main zurueck -> Programmende
            opstack.append(retval)
            ip = ret_ip
        elif op == "DROP":
            opstack.pop(); ip += 1
        elif op == "PRINT":
            print(opstack.pop()); ip += 1
        else:
            sys.stderr.write("tinyvm: unbekannter Opcode %r\n" % op)
            return 3


def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1]) as f:
            text = f.read()
    else:
        text = sys.stdin.read()
    prog = parse_ir(text)
    if not any(op == "FUNC" for op, _ in prog):
        sys.stderr.write("tinyvm: keine IR (Parse fehlgeschlagen?)\n")
        return 1
    return run(prog)


if __name__ == "__main__":
    sys.exit(main())
