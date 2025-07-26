#ifndef IR_GENERATOR_C_COMPILER
#define IR_GENERATOR_C_COMPILER

#include "common.h"

IRProgram *gen_ir(FuncBlock *parsed);
void dump_ir(IRProgram *program, char *path);

#endif