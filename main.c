#include "include/error.h"
#include "include/file.h"
#include "include/tokenizer.h"
#include "include/parser.h"
#include "include/generator.h"
#include "include/analyzer.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    fprintf(stdout, "\e[32mc_compiler\e[37m\n");
    if (argc != 3)
    {
        error_exit("引数が正しくありません");
    }
    char *input = openfile(argv[1]);
    error_init(input);

    // トークナイザ
    tokenizer(input);
    // パーサ
    FuncBlock *parse_result = parser();
    // アナライザ(意味解析)
    FuncBlock *analyze_result = analyzer(parse_result);
    // コードジェネレーター
    generator(analyze_result, argv[2]);

    return 0;
}