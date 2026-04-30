#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <string.h>
#endif

#include "include/analyzer.h"
#include "include/debug.h"
#include "include/error.h"
#include "include/file.h"
#include "include/generator.h"
#include "include/ir_generator.h"
#include "include/ir_optimizer.h"
#include "include/optimizer.h"
#include "include/parser.h"
#include "include/preprocessor.h"
#include "include/tokenizer.h"
#include "include/target.h"

bool output_preprocess;
bool gcc_compatible;
bool output_ir;
bool output_mermaid;
uint8_t optimize_level = 1;
target_arch g_target_arch = TGT_X86_64; // default

static char *copy_arg_string(char *arg)
{
  size_t len = strlen(arg);
  char *copy = calloc(1, len + 1);
  memcpy(copy, arg, len + 1);
  return copy;
}

// Argument processing
// -E: Execute preprocessor and output
// -g: Import gcc predefined macros in preprocessor
// -o: Specify output file
// -i: Specify input file
// -I: Use standard input after this argument as input
// -emit-ir: Output IR
// -emit-mermaid: Output AST in Mermaid format
// -O0: non optimized
// -O1: optimized(default)
int main(int argc, char **argv)
{
  fprintf(stdout, "\e[32mc_compiler\e[37m\n");
  if (argc < 3)
    error_exit("Invalid arguments.");

#ifndef SELF_HOST
  // Handle abort
  struct sigaction sa;
  sa.sa_handler = (__sighandler_t)(void *)handle_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGSEGV, &sa, NULL) == -1)
    fprintf(stderr, "Failed to set signal handler\n");
#endif

  char *input_file_name = NULL;
  char *output_file_name = NULL;
  char *input = NULL;
  int has_input_file = 0;
  int has_output_file = 0;
  int has_inline_input = 0;
  // Process arguments (simplified to avoid miscompilation of complex ||/&&)
  for (int i = 1; i < argc; i++)
  {
    char *a = argv[i];
    if (a[0] != '-') continue;

    // Single-letter options
    if (a[1] == 'E' && a[2] == '\0')
      output_preprocess = true;
    else if (a[1] == 'g' && a[2] == '\0')
      gcc_compatible = true;
    else if (a[1] == 'o' && a[2] == '\0')
    {
      if (++i < argc)
      {
        output_file_name = copy_arg_string(argv[i]);
        has_output_file = 1;
      }
      else
        error_exit("Invalid arguments.");
    }
    else if (a[1] == 'i' && a[2] == '\0')
    {
      if (++i < argc)
      {
        input_file_name = copy_arg_string(argv[i]);
        input = openfile(input_file_name);
        has_input_file = 1;
      }
      else
        error_exit("Invalid arguments.");
    }
    else if (a[1] == 'I' && a[2] == '\0')
    {
      if (++i < argc)
      {
        input = argv[i];
        has_inline_input = 1;
        break;
      }
      else
        error_exit("Invalid arguments.");
    }
    // Multi-letter options
    else if (!strcmp(a, "-emit-ir"))
      output_ir = true;
    else if (!strcmp(a, "-emit-mermaid"))
      output_mermaid = true;
    else if (!strcmp(a, "-O0") && !(optimize_level & 1 << 7))
      optimize_level = 0 | 1 << 7;
    else if (!strcmp(a, "-O1") && !(optimize_level & 1 << 7))
      optimize_level = 1 | 1 << 7;
    else if (!strcmp(a, "-target"))
    {
      if (++i < argc)
      {
        if (!strcmp(argv[i], "x86_64"))
          g_target_arch = TGT_X86_64;
        else if (!strcmp(argv[i], "aarch64"))
          g_target_arch = TGT_AARCH64;
        else
          error_exit("Invalid arguments.");
      }
      else
        error_exit("Invalid arguments.");
    }
    else
      error_exit("Invalid arguments.");
  }

  // Debug: print parsed arg state before validation (helps self-host debug)
  fprintf(stderr, "[argcheck] input=%p, output=%p, gcc=%d, pp=%d, tgt=%d\n",
          (void *)input, (void *)output_file_name, (int)gcc_compatible,
          (int)output_preprocess, (int)g_target_arch);
  // Validate required arguments separately to avoid depending on
  // complex short-circuit expressions that our early self-hosted
  // backend may miscompile.
  if (has_input_file == 0 && has_inline_input == 0)
    error_exit("Invalid arguments.");
  if (input == NULL)
    error_exit("Invalid arguments.");
  if (has_output_file == 0)
    error_exit("Invalid arguments.");
  if (output_file_name == NULL)
    error_exit("Invalid arguments.");
  if (gcc_compatible && !output_preprocess)
    error_exit("Invalid arguments.");
  pr_debug("output_file_name: %s", output_file_name);
  pr_debug("input_file_name: %s", input_file_name);

  init_preprocessor();
  // Preprocessor (includes tokenizer)
  Token *token = preprocess(input, NULL, input_file_name, NULL);
  if (output_preprocess)
    preprocessed_file_writer(token, output_file_name);
  fix_token_head();  // Adjust the token head to not be IGNORABLE or LINEBREAK
  // Parser
  FuncBlock *parse_result = parser();
  if (output_mermaid)
  {
    print_mermaid_result(parse_result, output_file_name);
    return 0;
  }
  // Analyzer (semantic analysis)
  FuncBlock *analyze_result = analyzer(parse_result);

  // Optimizer
  if ((optimize_level & ~(1 << 7)) == 1)
  {
    pr_debug("Start high-level optimizer");
    analyze_result = optimizer(analyze_result);
    pr_debug("High-level optimizer done");
  }

  // IR generator
  pr_debug("Start IR generator");
  IRProgram *ir_program = gen_ir(analyze_result);
  pr_debug("IR gen done");
  // IR optimizer
  ir_program = optimize_ir(ir_program);
  pr_debug("IR optimize done");

  if (output_ir)
  {
    dump_ir(ir_program, output_file_name);
    return 0;
  }
  // Code generator
  pr_debug("Start code generator");
  generator(ir_program, output_file_name);
  pr_debug("Code generator done");

  return 0;
}
