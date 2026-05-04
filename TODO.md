# TODO

## Known Unsupported C Constructs

These are current front-end / IR gaps confirmed with minimal repros against `./main`
on 2026-04-30. They are also tracked as expected compile-fail cases in `test.sh`.

- Block-scope `static` variables are rejected in `add_variables()`.
  - Repro: `int main() { static int x; return x; }`
  - Current failure: `type.c:455:add_variables(): unimplemented`
  - Note: `a64_emit.c` no longer depends on this; its local static buffer was
    moved to file scope.
- Designated initializers are not parsed for local aggregate initialization.
  - Repro: `typedef struct { int x; int y; } S; int main() { S s = { .x = 1, .y = 2 }; return s.x + s.y; }`
  - Current failure: `parser.c:init_declarator -> invalid initializer`
- Brace aggregate initialization for local structs is still rejected.
  - Repro: `typedef struct { int x; int y; } S; int main() { S s = {0}; return s.x + s.y; }`
  - Current failure: `parser.c:init_declarator -> invalid initializer`
  - Note: this blocks a common zero-init pattern used as a workaround for designated init.
- GNU nested functions are not parsed in block scope.
  - Repro: `int main() { auto int helper(int x) { return x + 1; } return helper(2); }`
  - Current failure: parser desynchronizes and later hits `parser.c:1180:primary_expression(): unreachable`
  - Impact: `x64_regalloc.c` had to be rewritten to use file-scope helpers instead of local helper functions.
- Compound literals are not parsed as expressions.
  - Repro: `struct S { int x; int y; }; int main() { return ((struct S){1, 2}).x; }`
  - Current failure: parser expects `;` at the opening `{` of the compound literal.
  - Impact: `x64_regalloc.c` had to be rewritten to avoid `(LiveSpill){...}` and `(Active){...}`.
- Function designators cannot be used as ordinary expression values.
  - Repro: `int foo() { return 7; } int call(int (*fn)()) { return fn(); } int main() { return call(foo); }`
  - Current failure: parser falls through to `parser.c:1180:primary_expression(): unreachable`
  - Impact: `x64_regalloc.c` had to stop passing `cmp_start` to `qsort` and sort inline instead.
- Direct subscripting of string literal expressions is rejected.
  - Repro: `int main() { return "abc"[0]; }`
  - Current failure: `analyzer.c:778:add_type_internal(): invalid array`
  - Impact: this also breaks direct indexing of builtin string macros such as `__FILE__[0]`.
- `va_list`-based code from the self-host runtime header is still unsupported.
  - Repro: `#include "test/compiler_header.h" int main() { va_list ap; return 0; }`
  - Current failure: `parser.c:1202:primary_expression(): variables va_list is not defined`
  - Impact: general `va_start` / `va_arg` support is still missing.
  - Note: `error.c` uses a reduced `SELF_HOST` diagnostics path that does not
    require `va_list`.
- General user-defined variadic functions are not stage2-stable yet.
  - Repro: `int variadic(char *fmt, ...) { return 7; } int main(){ return variadic("x", 1, 2); }`
  - Current failure: `./main2` can hang while compiling this even though `./main`
    accepts it.
  - Current partial support: `ND_VARIABLE_ARGS` is typed as `TYPE_VARIABLES` and
    skipped for fixed-argument stack stores, enough for `error.c` self-hosting.
- Negative integer pointer addition is known risky in self-hosted output.
  - Repro: `char *p = s + 1; p = p + (-1);`
  - Observed failure: self-hosted `error.c` generated `ptr + 0xffffffff` for
    `start_line[-1]`, causing a bad address in stage2 warning diagnostics.
  - Workaround used: rewrite that code to decrement a temporary pointer with
    `prev--`.
- Empty string literals previously crashed token concatenation / escape rewriting.
  - Repro: `int main(){ char *s = ""; return s[0]; }`
  - Status: fixed on 2026-04-28 and covered by a positive regression in `test.sh`.
  - Root cause: `consume_string()` iterated with `return_token->len - 1`, which
    underflowed for zero-length string tokens.
- `continue` inside a `for` loop body can be miscompiled in self-hosted stage2
  output so that it jumps to the loop condition instead of the increment clause.
  - Repro: `for (int i = 0; i < 1; i = i + 1) { continue; }`
  - Observed impact: `find_jmp_target()` could hang while stage2 compiled
    `conditional_inclusion.c`.
  - Workaround used: avoid `continue` in compiler implementation loops that
    must be compiled by `./main2`.

## Current Self-Host Status

- `make test` is green on 2026-04-30.
- `make self1` is green on 2026-04-30.
- `make self1-test` is green on 2026-04-30.
- Current gcc fallback set is empty. All compiler source files are compiled by
  `./main` during `make self1`.
- `a64_emit.c` is self-hosted again as of 2026-04-28.
  - Its only front-end blocker was a block-scope `static` buffer; this was moved
    to file scope rather than implementing general local static storage.
  - A follow-up x64 emission bug was fixed for 32-bit to 64-bit zero-extension:
    `movzx r64, r32` is invalid, so the emitter now uses `mov dst32, src32`.
- `error.c` is self-hosted again as of 2026-04-28.
  - The self-host path intentionally prints format strings literally instead of
    using `va_start` / `vfprintf`.
  - `start_line[-1]` was rewritten to avoid a self-hosted negative pointer-add
    wrong-code path.
- `debug.c` is self-hosted again as of 2026-04-28.
  - The earlier unstable run was caused by generated artifact handling in
    `out/`, not by the self-hosted `debug.c` object.
  - Self-host artifacts now use `SELF_OUT` (default: `.selfhost-out`) and avoid
    `out/*.o` glob expansion, so a stale/bad `out/` entry cannot block `self1`.
- `define.c` and `preprocessor.c` are self-hosted again.
  - `define.c` fix: string literals are no longer fed into macro expansion, so
    `"__FILE__"` / `"__LINE__"` are preserved instead of being replaced by the
    builtin preprocessor macros.
  - `preprocessor.c` fix: `TK_STRING` is emitted with a consistent internal
    representation and `-E` now writes quoted string literals correctly.
- Stage2 is green with no forced x64 fallback.
  - `test/selfhost_smoke.sh` now covers:
    - `minimal`: `int main(){return 0;}`
    - `ret18`: `int main(){return 9+8+5-4;}`
    - `div_nested`: nested-expression division that returns `2`
    - `fibloop`: call+loop heavy case that returns `13`
    - `call8`: eight-argument x86_64 call path that returns `36`
  - `./test.sh ./main2` is green with self-hosted `debug.c`.
- There are currently no temporary gcc fallback files in `make self1`.
- Recent self-host regressions fixed:
  - `a64_emit.c` is self-hosted again.
    - Local `static char buf[32]` was replaced by a file-scope buffer.
    - 32-bit to 64-bit zero-extension emission now avoids invalid
      `movzx r64, r32` assembly; covered by a cast regression in `test.sh`.
  - `error.c` is self-hosted again.
    - `SELF_HOST` diagnostics avoid `va_list`.
    - `ND_VARIABLE_ARGS` no longer trips analyzer/codegen for function
      definitions that only need fixed parameters.
    - `start_line[-1]` was replaced with `prev--` after it exposed a negative
      pointer-add wrong-code path in stage2.
  - `debug.c` is self-hosted again.
    - The empty-string tokenizer crash was fixed first.
    - The remaining apparent `debug.c` instability was an `out/` generated
      artifact/glob problem; moving self-host intermediates to `SELF_OUT`
      restored stable `./test.sh ./main2` runs.
  - `x64_regalloc.c` is self-hosted again and its stage2 output passes
    `./test.sh ./main2`.
    - The fix was to avoid a self-host wrong-code path around the local
      `int reg_taken[16]` array by storing the allocation bitmap as a heap
      `size_t` array.
  - `x64_emit.c` compiles successfully during `make self1`.
    - The former stage2 crashes were avoided by keeping final instruction
      formatting out of vararg-heavy `fprintf` calls that also evaluated helper
      calls such as `reg_name()`.
    - Repros now covered: nested division and an eight-argument call.
  - The `x64_emit.c` stage2 hang came from `sync_split_vregs_before_insn()`
    carrying pointer-typed `vector_peek_at()` results across later calls. The
    current implementation copies those results into scalar locals immediately,
    avoiding the self-hosted wrong-code path.
  - `a64_opt.c` and `generator_aarch64.c` are self-hosted as of 2026-04-30.
    - Their blocker was struct assignment being routed through scalar
      `size_of_real()`.
    - `ND_ASSIGN` now lowers struct copies as chunked memory loads/stores.
    - x64 MIR liveness/regalloc now treats the address side of `mov [mem], reg`
      as a use, preventing the copied value register from clobbering the store
      address.
  - `a64_regalloc.c` is self-hosted as of 2026-04-30.
    - The remaining blockers were unsupported GNU nested functions, compound
      literals, and passing a function designator to `qsort`.
    - The allocator now uses file-scope helpers, parallel arrays, and an inline
      interval selection loop like the x64 allocator. This removes the fallback
      without requiring new front-end support for those constructs.
- Recent regressions fixed and now covered by positive tests in `test.sh`:
  - Global 1D array initialization such as `int arr[] = {1, 2, 3};`
  - Global multi-dimensional array initialization with nested zero padding such as
    `int arr[2][3] = {{1}, {4, 5}};`
  - Global integer constant expressions such as `long long x = -1;`
    and `int arr[] = {-1, 2};`
  - Global char-array / pointer initializers such as
    `char s[] = "ab"; char *p[] = {s};`
  - Local partial zero-initialization with odd-byte tails such as
    `_Bool a[9] = {1};`
  - Larger local struct field assignment on `struct tmp` with mixed-width members.
  - Function-call arguments containing string literals that spell builtin macro
    names, such as `call("__FILE__")`.
  - Preprocessor `-E` output for ordinary and builtin-spelled string literals,
    such as `char *s = "x";` and `char *s = "__FILE__";`.
  - Empty string literals such as `char *s = "";`.
  - Struct assignment / copy such as
    `typedef struct { int x; int y; } S; ... b = a;`.

## x86_64 MIR-based Register Allocation and Codegen Plan

This document tracks the plan to move register allocation and lowering to an x86_64 machine IR (MIR) layer, handle target-specific constraints (IDIV/CQO, CALL clobbers, return in RAX, 2-operand form), and keep the architecture-independent IR minimal without sacrificing performance.

## Goals
- Perform register allocation at MIR (x86_64-aware) with correct handling of fixed/clobbered registers and calling convention.
- Lower SSA/phi to parallel copies before allocation; minimize moves via coalescing.
- Use IR-level liveness for early optimizations and as a seed/hint, but compute final liveness on MIR.
- Generate high-quality code with linear-scan (with splitting, coalescing, rematerialization). Consider advanced allocators later.

## Pipeline Overview
1. IR (arch-agnostic)
   - Optional: IR-level DCE, trivial phi elimination, copy propagation.
   - Compute IR instruction indices and block CFG (already present) and, if desired, instruction-level liveness for early passes and hints.
2. MIR selection (generator_x64)
   - Emit x64-like ops to `X64_ASM` using virtual registers only.
   - Attach target constraints as metadata, do NOT fix real registers here.
   - Lower `IR_PHI` to parallel copy sets at predecessor ends; remove `IR_PHI` from stream.
3. MIR liveness
   - Number program points, build live intervals (multi-segment), account for implicit uses/defs and clobbers.
   - Compute next-use info for each virtual reg.
4. Register allocation (x86_64)
   - Linear-scan with interval splitting and copy coalescing.
   - Precolor/fixed intervals for constrained ops (IDIV/CQO: use/def RAX/RDX; CALL: caller-saved clobber; RET: RAX; SETcc/CMP pairing).
   - Spill heuristics (loop-depth-weighted cost); rematerialize immediates/LEA where possible.
   - Split around CALLs; prefer moving live values to callee-saved or spill.
5. Prologue/Epilogue & Calls
   - Track used callee-saved regs and save/restore only those.
   - Compute frame size including spill slots; keep 16B alignment across calls.
   - Arrange call arguments (rdi, rsi, rdx, rcx, r8, r9, then stack) and minimize moves via coalescing with allocated regs.
6. Emit assembly
   - Lower `X64_ASM` to text; honor 2-operand form and choose memory operands when legal to reduce loads/stores.

## Where IR Liveness Helps
- Early DCE/ADCE and copy-propagation to shrink MIR input and reduce register pressure.
- Trivial phi elimination and expression ordering heuristics.
- Seed/hints for MIR liveness: map IR program points/intervals to MIR, then apply constraint-based corrections and run one MIR liveness pass to finalize.

## x86_64 Constraints to Model in MIR
- IDIV/DIV/MUL/CQO: implicit use/def of RAX/RDX; fixed result regs.
- CALL: clobbers caller-saved regs (rax, rcx, rdx, rsi, rdi, r8–r11); return in RAX.
- RET: returns via RAX.
- SETcc + CMP/EFLAGS: model as a pair (don’t schedule unrelated ops between them) or treat SETcc dest as byte reg.
- Reserved regs: always reserve RSP; optionally reserve RBP as frame pointer (decide policy).

## Code Changes (Checklist)
- generator_x64.c
  - [x] Stop prematurely assigning real regs (e.g., RET forcing RAX, DIV path forcing RAX/RDX). Replace with constraint metadata.
  - [x] Implement `IR_CALL`, `IR_STORE_ARG`, sign/zero-extend ops.
  - [x] Implement phi lowering to parallel copies at predecessor ends; remove `IR_PHI` from streams.
  - [x] Ensure `generate_x64_block` pushes created `X64_Blocks` into `func_x64.asm_blocks` for later passes.
- include/generator_x64.h / X64_ASM
  - [x] Add constraint metadata:
    - [x] per-instruction clobber mask (reuse/rename `implicit_used_registers`).
    - [x] per-operand flags: requires register / fixed real register / register class.
  - [x] Optionally add instruction/program-point IDs to support interval mapping and dumps.
- New MIR passes (new files, e.g., `x64_liveness.c`, `x64_regalloc.c`)
  - [x] MIR CFG construction (reuse blocks) and program point numbering.
  - [x] MIR liveness (use/def with implicit ops, clobbers), interval builder with next-use info.  (block-level in/out; intervals TBD)
  - [x] Linear-scan register allocator with splitting, caller boundary splits, coalescing, rematerialization.
  - [x] Minimal regalloc: assign callee-saved regs, pre-reserve clobbered regs; stack slots for spills.
  - [x] Spill slot assignment with slot reuse for non-overlapping live ranges.
- Prologue/Epilogue and Calls
  - [x] Compute used callee-saved set; emit save/restore and frame allocation.
  - [x] Maintain 16B stack alignment at calls.
  - [x] Generate arg moves to ABI regs/stack with basic coalescing (peephole removes redundant MOVs).
- Emission
  - [x] Lower final `X64_ASM` to text (initial pass); ensure op-size correctness; use memory operands when legal; handle 2-operand form.

## Short-Term (Phase 1)
- [x] Phi lowering + MIR instruction-level liveness (without splitting).
- [x] Constraint-aware linear scan (no interval splitting yet), handling IDIV/CQO, CALL clobbers, RET, SETcc.
- [x] Prologue/Epilogue save/restore based on used callee-saved; frame size computation.
- [x] Emit assembly for existing IR features; add tests for returns, calls, div/rem, ternary (phi), loops.

## Phase 2
- [x] Interval splitting and caller boundary splits (loop-weighted cost TBD).
- [x] Copy coalescing prioritization (mov and phi-derived copies).
- [x] Rematerialization of immediates/LEA.

## Phase 3 (Optional/Future)
- [ ] SSA-based optimistic coloring / IRC / PBQP allocator.
- [ ] Scheduler aware of register pressure.
- [ ] XMM/SSE register class support for floating-point.

## Open Decisions
- Keep frame pointer (RBP) or omit when possible?
- Tail calls / varargs support priority?
- How to represent constraints in `X64_ASM` (metadata fields vs. opcode-specific knowledge)?

## Validation & Tooling
- [x] MIR dumps: CFG, intervals, allocated regs, spill events.
- [x] Check 16B stack alignment across all calls.
- [ ] Unit tests: small call chains, nested loops with calls, division/remainder, short-circuit/ternary, phi convergence.

---
Notes:
- IR liveness remains valuable for early passes and as an initial approximation. Final correctness and quality require MIR-level liveness and allocation.
- Aim to minimize IR changes; concentrate complexity in x86_64 MIR passes.
