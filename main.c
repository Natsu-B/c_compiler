#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <string.h>
#endif

#include "include/analyzer.h"
#include "include/error.h"
#include "include/file.h"
#include "include/generator.h"
#include "include/parser.h"
#include "include/preprocessor.h"
#include "include/tokenizer.h"

bool output_preprocess;
bool gcc_compatible;

// Argument processing
// -E: Execute preprocessor and output
// -g: Import gcc predefined macros in preprocessor
// -o: Specify output file
// -i: Specify input file
// -I: Use standard input after this argument as input
int main(int argc, char **argv)
{
  fprintf(stdout, "c_compiler\n");
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
  // Process arguments
  for (int i = 1; i < argc; i++)
  {
    if (argv[i][0] == '-')
    {
      if (!strcmp(argv[i], "-E"))
        output_preprocess = true;
      else if (!strcmp(argv[i], "-g"))
        gcc_compatible = true;
      else if (!strcmp(argv[i], "-o") && ++i < argc)
        output_file_name = argv[i];
      else if (!strcmp(argv[i], "-i") && ++i < argc)
        input_file_name = argv[i];
      else if (!strcmp(argv[i], "-I"))
      {
        input = argv[++i];
        break;
      }
      else
        error_exit("Invalid arguments.");
    }
  }
  if (input_file_name)
    input = openfile(input_file_name);
  if (!input || !output_file_name || (gcc_compatible && !output_preprocess))
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
  // Analyzer (semantic analysis)
  FuncBlock *analyze_result = analyzer(parse_result);
  // Code generator
  generator(analyze_result, output_file_name);

  return 0;
}