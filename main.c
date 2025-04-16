#include <stdio.h>
#include <string.h>

#include "include/analyzer.h"
#include "include/error.h"
#include "include/file.h"
#include "include/generator.h"
#include "include/parser.h"
#include "include/preprocessor.h"
#include "include/tokenizer.h"

bool output_preprocess;

// 引数の処理
// -E:  プリプロセッサを実行し出力する
// -o:  出力ファイルを指定する
// -i:  入力ファイルを指定する
// -I:  入力をこの引数のあとの標準入力とする
int main(int argc, char **argv)
{
  fprintf(stdout, "\e[32mc_compiler\e[37m\n");
  if (argc < 3)
    error_exit("invalid arguments");
  char *input_file_name = NULL;
  char *output_file_name = NULL;
  char *input = NULL;
  // 引数の処理をする
  for (int i = 1; i < argc; i++)
  {
    if (argv[i][0] == '-')
    {
      if (!strcmp(argv[i], "-E"))
        output_preprocess = true;
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
        error_exit("invalid arguments");
    }
  }
  if (input_file_name)
    input = openfile(input_file_name);
  if (!input || !output_file_name)
    error_exit("invalid arguments");
  pr_debug("output_file_name: %s", output_file_name);
  pr_debug("input_file_name: %s", input_file_name);

  init_preprocessor();
  // プリプロセッサ(トークナイザ内包)
  Token *token = preprocess(input, input_file_name, NULL);
  if (output_preprocess)
    preprocessed_file_writer(token, output_file_name);
  // トークナイザ
  re_tokenize(token);
  // パーサ
  FuncBlock *parse_result = parser();
  // アナライザ(意味解析)
  FuncBlock *analyze_result = analyzer(parse_result);
  // コードジェネレーター
  generator(analyze_result, output_file_name);

  return 0;
}