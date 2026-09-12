# Q9 68k architecture

`Q9-68k` was planned as an architecture umbrella for the Motorola 68000
family. The productive 68k tools are maintained in this repository under
`Q9-BACKEND-68K`; this document keeps the shared architecture context here.

```text
Q9-QCC (shared Q9 IR)
       |
       v
Q9-BACKEND-68K
       |
Q9-Flux-68k
```

The architecture-specific projects provide the 68k backend, peephole
optimizer, assembler, linker, runtime and ABI-compatible libraries. Shared
frontends and the Q9 intermediate representation remain architecture-neutral.

## Edition history

- 2026-09-12: Consolidated the useful content from the planned standalone
  `Q9-68k` umbrella directory.
