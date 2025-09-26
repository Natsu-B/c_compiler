#ifndef A64_EMIT_C_COMPILER
#define A64_EMIT_C_COMPILER

#include "common.h"

// Forward decl to avoid include cycle
struct A64_FUNC;

// Emit file-level preamble for AArch64 (currently none required)
void a64_emit_file_preamble(void);

// Emit textual assembly for a single function from A64 MIR
void a64_emit_function(IRFunc* func_ir, struct A64_FUNC* func_a64);

#endif // A64_EMIT_C_COMPILER
