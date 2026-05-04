#ifndef X64_EMIT_C_COMPILER
#define X64_EMIT_C_COMPILER

#include "common.h"
#include "generator_x64.h"

// Emit file-level preamble (syntax, etc.) for x86_64 backend
void x64_emit_file_preamble(void);

// Emit textual assembly for a single function (scaffold: prologue/labels only)
void x64_emit_function(IRFunc* func_ir, X64_FUNC* func_x64);

#endif
