#include "include/error.h"
#include "include/file.h"
#include "include/tokenizer.h"
#include "include/parser.h"
#include "include/generator.h"

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        error_exit("引数が正しくありません");
    }
    char *input = openfile(argv[1]);
    error_init(input);

    // トークナイザ
    tokenizer(input);
    // パーサ
    Node *node = parser();
    // コードジェネレーター
    generator(node, argv[2]);

    return 0;
}