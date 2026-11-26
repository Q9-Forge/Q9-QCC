# qost — Q9 Stack-IR optimizer

[Deutsch](README_de.md)

`qost` is a standalone, backend-independent optimization pass for Q9
Stack-IR (`.ir`). To build and run it:

```sh
make -C Q9-OPTIMIZER/q9-qost
Q9-OPTIMIZER/q9-qost/build/qost input.ir output.opt.ir
```

The first pass folds constant 32-bit integer expressions when the operands
are adjacent `PUSH` instructions. It supports addition, subtraction,
multiplication without signed overflow, bitwise operations, comparisons,
`NEG`, `NOT`, `NOTBIT`, and `NARROWC`. Because `PUSH` carries no signedness
or type attribute, the optimizer leaves positive constants above `INT32_MAX`
unchanged instead of guessing whether they are signed or unsigned. Arithmetic
overflow is also left unchanged, avoiding assumptions about C overflow
semantics in the IR. Pointer operations, memory accesses, calls, division by
zero, labels, and control flow are not rewritten. Unknown or malformed lines
are preserved.

The input remains structured by line: comments and line endings act as
barriers and are not skipped during optimization. The output is valid Stack-IR.
`make test` compares expected folds and runs QCCVM before and after optimization.

Control-flow and data-flow analysis, variable constant propagation, dead-code
elimination, multiple optimization levels, and integration with the `qcc`
driver are not implemented yet. Invoke `qost` explicitly to test it in
isolation.
