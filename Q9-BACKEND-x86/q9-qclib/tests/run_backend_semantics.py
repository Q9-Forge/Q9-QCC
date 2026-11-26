#!/usr/bin/env python3
"""Run real QIRX86/QRX86/QLX86 code in QEMU; no OS-9000 ABI claim."""
from pathlib import Path
import re
import struct
import sys
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SYS = "--syscalls" in sys.argv
CLIB = "--clib" in sys.argv or SYS
SOURCE = HERE / ("os9000_blocks.c" if SYS else "clib_integration.c" if CLIB else "backend_semantics.c")

def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)

with tempfile.TemporaryDirectory(prefix="q9-x86-semantics-") as directory:
    work = Path(directory)
    run(ROOT / "Q9-QCC/build/qcc", "--target", "OS9-x86", "-S", "--tmpdir", work / "tmp",
        "-o", work / "check.sx86", SOURCE, cwd=ROOT / "Q9-QCC")
    for name, source in (("check", work / "check.sx86"), ("external", HERE / "external_pair.sx86")):
        run(ROOT / "Q9-BACKEND-x86/q9-qrx86/build/qrx86", source, work / (name + ".r"))
    obj = (work / "check.r").read_bytes()
    at = obj.index(b"\0", 56) + 1
    count = struct.unpack_from(">I", obj, at)[0]
    at += 4
    entry = None
    for _ in range(count):
        end = obj.index(b"\0", at)
        name = obj[at:end]
        kind, value = struct.unpack_from(">HI", obj, end + 1)
        at = end + 7
        if name == b"main":
            assert kind == 4
            entry = value
    assert entry is not None
    if SYS:
        for name in ("path_os9000", "syscalls_os9000", "process_os9000", "time_os9000", "fd_os9000"):
            obj += (ROOT / "Q9-BACKEND-x86/q9-qclib/build" / (name + ".r")).read_bytes()
    (work / "all.r").write_bytes(obj + ((ROOT / "Q9-BACKEND-x86/q9-qclib/build/qclib.r").read_bytes() if CLIB else (work / "external.r").read_bytes()))
    run(ROOT / "Q9-BACKEND-x86/q9-qlx86/build/qlx86", "--emit-prototype", work / "all.r",
        "-O=" + str(work / "all.qmod"))
    linked = (work / "all.qmod").read_bytes()
    # Append zeroed process storage to code and initialized data. QIRX86's
    # call/pop references remain valid at this code load address. This does
    # not exercise the OS-9000 container or its loader relocation lists.
    bss = struct.unpack_from(">I", linked, 16)[0]
    if not CLIB:
        assert bss == 0
    (work / "code.bin").write_bytes(linked[56:] + bytes(bss))
    boot = f""".section .multiboot,"a"
.align 4
.long 0x1badb002,3,-(0x1badb002+3)
.section .text
.globl _start
_start:
 cli
 mov $0x200000,%esp
 call qcc_main
 mov %eax,%ebx
halt_result:
 hlt
 jmp halt_result
qcc_code:
.incbin "{work / 'code.bin'}"
.set qcc_main,qcc_code+{entry}
"""
    (work / "boot.S").write_text(boot)
    (work / "link.ld").write_text("ENTRY(_start)\nSECTIONS { . = 0x100000; .multiboot : { *(.multiboot) } .text : { *(.text*) } }\n")
    run("clang", "-target", "i386-unknown-elf", "-c", work / "boot.S", "-o", work / "boot.o")
    run("ld.lld", "-m", "elf_i386", "-T", work / "link.ld", work / "boot.o", "-o", work / "check.elf")
    qemu = subprocess.Popen(["qemu-system-i386", "-kernel", str(work / "check.elf"),
                            "-display", "none", "-no-reboot", "-no-shutdown", "-monitor", "stdio",
                            "-serial", "none", "-nic", "none"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        time.sleep(2)
        output, _ = qemu.communicate("info registers\nquit\n", timeout=10)
    finally:
        if qemu.poll() is None:
            qemu.kill()
            qemu.wait()
    registers = re.search(r"EAX=([0-9a-f]+) EBX=([0-9a-f]+).*?ESP=([0-9a-f]+).*?EIP=([0-9a-f]+).*?HLT=1", output, re.I | re.S)
    assert registers, output
    eax, ebx, esp, eip = (int(x, 16) for x in registers.groups())
    # _start follows the 12-byte multiboot header; halt follows CLI (1),
    # MOV ESP (5), CALL (5), MOV EBX,EAX (2). EIP after HLT is +14.
    assert (eax, ebx, esp, eip) == (0, 0, 0x200000, 0x10001a), registers.group(0)
    print("PASS: " + ("OS-9000 block builders with mock trap" if SYS else "CLib integration" if CLIB else "backend semantics") + " through QCC/QRX86/QLX86 under QEMU")
