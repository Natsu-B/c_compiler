#ifndef X64_VALIDATE_C_COMPILER
#define X64_VALIDATE_C_COMPILER

#include "common.h"
#include "generator_x64.h"

// Validate target-specific invariants on MIR before emission
// - Stack alignment: ensure RSP is 16B-aligned at every CALL site
void x64_validate(IRFunc* func_ir, X64_FUNC* func_x64);

#endif

