// ------------------------------------------------------------------------------------
// generator
// ------------------------------------------------------------------------------------

#include "include/generator.h"

#include "include/common.h"
#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "include/error.h"
#include "include/generator_x64.h"
#include "include/ir_generator.h"
#include "include/vector.h"

FILE *fout;

void generator(IRProgram *program, char *output_filename)
{
  pr_debug("start generator");

  pr_debug("output filename: %.*s", strlen(output_filename), output_filename);
  fout = fopen(output_filename, "w");
  if (fout == NULL)
    error_exit("cannot written to file");
  pr_debug("output file open");
  output_file("    .intel_syntax noprefix\n");

  // Global variables
  for (size_t i = 0; i < vector_size(program->global_vars); i++)
  {
    GlobalVar *gvar = vector_peek_at(program->global_vars, i + 1);
    if (vector_size(gvar->initializer->IRs) == 1 &&
        ((GVarInitializer *)vector_peek(gvar->initializer->IRs))->how2_init ==
            init_zero)
      output_file("    .section .bss");
    else
      output_file("    .section .data");
    if (!gvar->is_static)
      output_file("    .globl %.*s", (int)gvar->var_name_len, gvar->var_name);
    output_file("    .type %.*s, @object", (int)gvar->var_name_len,
                gvar->var_name);
    output_file("    .size %.*s, %lu", (int)gvar->var_name_len, gvar->var_name,
                gvar->var_size);
    output_file("%.*s:", (int)gvar->var_name_len, gvar->var_name);
    for (size_t j = 1; j <= vector_size(gvar->initializer->IRs); j++)
    {
      GVarInitializer *init = vector_peek_at(gvar->initializer->IRs, j);
      switch (init->how2_init)
      {
        case init_zero: output_file("    .zero %zu", init->zero_len); break;
        case init_val:
          switch (init->value.value_size)
          {
            case 1: output_file("    .byte %lld", init->value.init_val); break;
            case 2: output_file("    .word %lld", init->value.init_val); break;
            case 4: output_file("    .long %lld", init->value.init_val); break;
            case 8: output_file("    .quad %lld", init->value.init_val); break;
            default: unreachable();
          }
          break;
        case init_pointer:
          output_file("    .quad %.*s", (int)init->assigned_var.var_name_len,
                      init->assigned_var.var_name);
          break;
        case init_string:
          output_file("    .quad %s", init->literal_name);
          break;
        default: unreachable(); break;
      }
    }
  }

  // strings
  if (vector_size(program->strings))
  {
    output_file("    .section .rodata");
    for (size_t i = 1; i <= vector_size(program->strings); i++)
    {
      Var *string = vector_peek_at(program->strings, i);
      output_file("%.*s:", (int)string->len, string->name);
      output_file("    .string \"%.*s\"", (int)string->token->len,
                  string->token->str);
    }
  }

  // Write functions
  for (size_t i = 0; i < vector_size(program->functions); i++)
  {
    IRFunc *func = vector_peek_at(program->functions, i + 1);
    generate_x64(func);
  }

  fclose(fout);
  pr_debug("Generation complete.");
}