# q9-qir68k – 68k backend

[Deutsch](README_de.md)

`qir68k` translates Q9 Stack-IR (`.ir`) into Motorola 68000 assembly text
(`.s68k`). Optimization, assembly and linking use separate tools:

```text
.ir -> qir68k -> .s68k -> qo68k -> qr68k -> .r -> ql68k -> module
```

The production sources are in `src/`. Build the host tool with:

```sh
make -C Q9-BACKEND-68K/q9-qir68k
```

`src/qcc_backend_candidate_review.cpp` is a review candidate and is not part
of the build. It requires a separate content comparison before acceptance.
