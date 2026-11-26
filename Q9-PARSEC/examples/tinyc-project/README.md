# Tiny-C multi-file example

[Deutsch](README_de.md)

This historical example describes a multi-file pipeline: `main.tc` and
`math.tc` are compiled separately, their IR is merged, and a backend,
assembler and linker produce a module. The sample program is intended to
print `42`.

The scripts still refer to the earlier tool names `tinyc_p`, `tinyc_backend`
and `tinyc_merge.py`. These tools are not supplied at the default paths in
this checkout, so the example is not currently a verified build recipe. The
host script also expects a configured cross-toolchain for the link step.

```sh
./build.sh --no-link
./build.sh -o demo
```

`--no-link` stops after backend assembly generation. With `-o`, the named
output may be overwritten. The host script removes and recreates its
`build/.work` directory on each run. Without an explicit output name, it picks
an available numbered name.

The separate `q9build` script describes a native Q9 workflow using commands in
`/dd/CMDS` and libraries in `/dd/LIB`. Its prerequisites and target execution
also require verification. Its pipeline invokes `tinyc_p`, `merge`,
`tinyc_backend`, `r68`, and `l68`:

```sh
q9build main.tc math.tc
```
