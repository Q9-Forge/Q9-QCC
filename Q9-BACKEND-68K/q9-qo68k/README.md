# q9-qo68k – 68k peephole optimizer

[Deutsch](README_de.md)

`qo68k` optimizes generated 68k assembly text. It preserves the architecture
suffix: `.s68k` becomes `.opt.s68k`.

The optimizer processes the 68k backend's assembly output independently of the
C frontend. Build and invoke it as a standalone program:

```sh
make -C Q9-BACKEND-68K/q9-qo68k
qo68k input.s68k output.opt.s68k
```

## Large inputs and line endings

The input, line table, and synthesized text are allocated dynamically, removing
an earlier fixed 2 MiB limit. The reader handles Q9-System text with CR line
endings as well as LF and CRLF. Output retains the input's line-ending style.
Synthesized lines reside in stable storage blocks so later growth does not
invalidate saved pointers.

A previous verification used `qcp.s68` generated in an emulator (6,247,987
bytes, 323,050 lines). The host and 68030 optimizers produced byte-identical
output (5,129,673 bytes; 52,831 optimizations over two passes). The optimized
host output was then assembled successfully to ROF with `qr68k`. Host and
target also produced byte-identical optimized output for `qom.s68`.

The optimizer still holds the entire input and its line table in memory. The
large test does not replace systematic memory-limit and error-path tests on
each target.
