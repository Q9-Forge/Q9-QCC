#!/usr/bin/env python3
"""Split one QCC IR translation unit into linkable backend parts.

This is deliberately different from qcc_merge.py: it divides one already
parsed source unit to keep native Microware r68 below its practical input-size
limit.  All parts share one source-level static namespace through
qcc_backend's ``-unit=<name>`` option.

The first output part owns every global definition.  Other parts receive
GLOBALDECL records; every part also receives FUNCDECL records for functions it
does not define.  Thus it is legal to make a direct call or access a global in
another part without duplicating storage.
"""
import argparse
import json
from pathlib import Path

from qccvm import parse_ir


def is_global(op):
    return op in ("GLOBAL", "GARRAY", "GINIT")


def read_program(path):
    program = parse_ir(Path(path).read_text())
    globals_, functions = [], []
    index = 0
    while index < len(program):
        op, args = program[index]
        if is_global(op):
            globals_.append((op, args))
            index += 1
            continue
        if op == "FUNCDECL" or op == "GLOBALDECL":
            # The generated declarations below are authoritative and include
            # the static flag needed by an artificial split.
            index += 1
            continue
        if op != "FUNC":
            raise ValueError("top-level opcode %s is not splittable" % op)
        start = index
        index += 1
        while index < len(program) and program[index][0] != "ENDFUNC":
            index += 1
        if index == len(program):
            raise ValueError("FUNC %s has no ENDFUNC" % args[0])
        functions.append(program[start:index + 1])
        index += 1
    if not functions:
        raise ValueError("IR contains no functions")
    return globals_, functions


def global_definitions(records):
    result = []
    for op, args in records:
        if op == "GLOBAL":
            result.append((args[0], args[2] if len(args) >= 3 else "i",
                           args[3] if len(args) >= 4 else "0"))
        elif op == "GARRAY":
            result.append((args[0], args[1], args[3] if len(args) >= 4 else "0"))
    return result


def globals_in_functions(functions):
    """The frontend represents static locals as GLOBAL/GARRAY inside FUNC.

    They still allocate one process-global object, so every other split part
    needs a declaration for them just like for a file-scope global.
    """
    result = []
    for fn in functions:
        result.extend(global_definitions(fn))
    return result


def function_definitions(functions):
    return [(fn[0][1][0], fn[0][1][1], fn[0][1][2] if len(fn[0][1]) >= 3 else "0")
            for fn in functions]


def partition(functions, limit):
    chunks, current, count = [], [], 0
    for fn in functions:
        fn_size = len(fn)
        if current and count + fn_size > limit:
            chunks.append(current)
            current, count = [], 0
        current.append(fn)
        count += fn_size
    if current:
        chunks.append(current)
    return chunks


def write_part(path, globals_, chunk, all_functions, all_globals):
    owned = {fn[0][1][0] for fn in chunk}
    chunk_records = [record for fn in chunk for record in fn]
    owned_globals = {name for name, _, _ in global_definitions(chunk_records)}
    lines = []
    if globals_ is not None:
        lines.extend(" ".join([op] + list(args)) for op, args in globals_)
        owned_globals.update(name for name, _, _ in global_definitions(globals_))
    lines.extend("GLOBALDECL %s %s %s" % item for item in all_globals
                 if item[0] not in owned_globals)
    for name, nargs, static in all_functions:
        if name not in owned:
            lines.append("FUNCDECL %s %s %s" % (name, nargs, static))
    for fn in chunk:
        lines.extend(" ".join([op] + list(args)) for op, args in fn)
    path.write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="complete QCC IR input")
    parser.add_argument("output_dir", help="directory for .partNN.ir files")
    parser.add_argument("--max-ir-lines", type=int, default=8000,
                        help="maximum function-body IR lines per part (default: 8000)")
    parser.add_argument("--unit", default="q9bootstrap",
                        help="shared qcc_backend -unit value (default: q9bootstrap)")
    args = parser.parse_args()
    if args.max_ir_lines < 1:
        parser.error("--max-ir-lines must be positive")

    globals_, functions = read_program(args.input)
    chunks = partition(functions, args.max_ir_lines)
    outdir = Path(args.output_dir)
    outdir.mkdir(parents=True, exist_ok=True)
    prefix = Path(args.input).stem
    all_functions = function_definitions(functions)
    all_globals = global_definitions(globals_) + globals_in_functions(functions)
    manifest = {"input": str(Path(args.input)), "unit": args.unit, "parts": []}
    for part_no, chunk in enumerate(chunks):
        path = outdir / ("%s.part%02d.ir" % (prefix, part_no))
        write_part(path, globals_ if part_no == 0 else None, chunk,
                   all_functions, all_globals)
        names = [fn[0][1][0] for fn in chunk]
        manifest["parts"].append({"file": path.name, "functions": names,
                                  "ir_lines": sum(len(fn) for fn in chunk),
                                  "owns_globals": part_no == 0,
                                  "runtime": "main" in names})
    (outdir / ("%s.parts.json" % prefix)).write_text(json.dumps(manifest, indent=2) + "\n")
    print("qcc_split_ir: %d functions -> %d parts, unit=%s" %
          (len(functions), len(chunks), args.unit))


if __name__ == "__main__":
    main()
